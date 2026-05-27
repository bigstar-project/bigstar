# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析の精度を優先する。

2026-05-28 の再評価で、`host/client の Game::localPlayerID を両方 0 に固定し、client 側だけ表示/UIを player1 へ寄せる` 方針は本筋から降格する。理由は、試合開始直後の画面/actor構成が自然なMvsL状態になっていない場合に、カメラ、HUD、ストックアイテム、死亡演出を個別patchで補正しても、最終的なLuigi側ゲームプレイとして成立する保証が弱いため。

次の本筋は「各PCのNSMBを、本来のローカル対戦時と同じ host=localPlayerID 0 / client=localPlayerID 1 の役割で起動し、その下の通信境界だけをWAN adapterへ差し替える」形に戻す。

- ROM patch で LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
- host は `Game::localPlayerID=0`、client は `Game::localPlayerID=1` として、NSMB本体のカメラ、HUD、ストックアイテム、勝敗/死亡演出の役割分担をできるだけそのまま使う。
- 試合開始直後に host 画面でMario/Luigiが自然に存在し、client画面でLuigi側として自然にプレイ可能な状態になることを、以降の最優先gateにする。
- 試合中に NSMB が読む packet/input/touch API を WAN adapter に差し替える。localPlayerIDを両方0に揃えるのではなく、NSMBが本来想定するローカル/リモート役割を壊さない。
- actor/state強制同期や表示だけの個別補正は、原因切り分け用に限定し、成功条件として扱わない。
- ローカル実入力は、最終的にはNSMBの通常入力経路に乗せ、peerの入力/touchはWAN packetとしてNSMBの通信境界へ渡す。

NSMB Central の解析どおり、MvsL は接続時に RNG seed を同期し、試合中は主に入力情報 packet を通信している前提で進める。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 現在の最優先課題

ユーザー観察ベースで、順番を次のように修正する。

- 2026-05-28 現在の最優先は、`client localPlayerID=1` で「上画面に地形とlocal playerが自然に出る」状態を作ること。host/clientのactor stateは `player0/player1 lives=5`, dead=0 で揃うが、client側の表示初期化が壊れる。
- `client localPlayerID=1` の緑一色上画面は、overlay0 の StageLayout 系 `Game::localPlayerID` 参照を0読みへaliasすると地形が戻る。したがって、ステージ地形/BG欠落の主因はStageLayout/地形描画側のlocal player camera選択。
- StageLayoutの緑画面は、全layout aliasではなく `0x020BACC0` だけでも消える。さらに狭くすると、StageLayout更新関数末尾の `0x020BAC84` / `0x020BAC90` の view/player 引数を player0 固定にするだけでも地形は戻る。これは「緑画面」はStageLayout最終view反映のplayer1経路に絞れる、という結果。
- 地形を戻してもplayer 3D modelが出ない問題は別。`Player::onRender` / `Player::renderModel` / `NNS_G3dDraw` はclientでも呼ばれるが、表示用X/wrap/camera判定がhostと違い、clientではplayerが上画面に出ない。`Player::onRender` の表示用Xへ `+0x400000` を足し、StageCameraをplayer0側へ戻すとMarioは表示されるため、3D描画自体は壊れていない。
- `stage-layout-final-view-player-id --player-id 0` と `player-render-wrap-x-offset` を組み合わせてもLuigi/player modelは戻らない。したがって次は、StageLayoutの地形view補正とPlayer modelの3D view/projection補正を分けて追う。カメラをplayer0に戻すだけではLuigi側プレイにならないので、これは成功条件ではなく診断結果として扱う。
- 最優先gateは「試合開始直後の画面/actor構成」。host側にMarioだけ、client側にMario/Luigiが出ていない、または地形相対の表示が壊れている状態は失敗として扱う。ここを飛ばしてストックアイテムや死亡演出の検証へ進まない。
- `localPlayerID=0` 両固定 + 表示patch方式は、clientのLuigi側プレイ実現として将来性が低い。次は `localPlayerID=1` client を本筋に戻し、そこで壊れる actor生成/StageStart/packet境界を根本原因として追う。
- clientでLuigi側を実際にプレイできること、つまりカメラ追従、HUD、ストックアイテム、死亡/勝敗演出をNSMB本体のlocal player処理として自然に動かすことを優先する。
- 既存の `StageCamera state + onUpdate + Stage::setZoom` player1 patchや `StageLayout` inventory patch は、表示補正の診断結果として残すが、最終方針の中心には置かない。
- 右下ストックアイテム検証は一時停止する。`PacketBridgePreserveLocalTouch` と packet byte trace で touch header がWAN packetとしてhostへ届くことは確認したが、試合開始状態が不自然なままでは優先順位が低い。
- host/client の上画面で Mario/Luigi の表示位置が違って見える問題は、単なるカメラ差として扱わない。自然なlocalPlayerID役割で再現し、地形、actor、HUD、下画面、演出が同じ役割解釈になっているかを検証する。
- 開始直後に右上Luigi残機が `5 -> 4` になる問題は、Goomba接触ではない。根本経路は `Player::vsPipeTransitState()` 完了後の player1 が `StageActor::isOutOfViewVertical()` で画面外扱いになり、`Player::pitDeathTransitState()` -> `Player::beginDeathTransition()` -> `Game::playerDead[1]=1` -> `PlayerBase::onDefeated()` -> `Game::losePlayerLife(1)` / `Game::addPlayerDeath(1)` と進む流れ。direct entry では `Stage::cameraY[1]` / `Stage::cameraHeight[1]` が 0 のままなので player1 の縦画面外判定だけが壊れる。開始限定の残機/死亡カウンタ補正は最終修正として扱わない。2026-05-27時点で、`StageActor::isOutOfViewVertical` の cameraHeight が 0 の player slot だけ slot0 にfallbackする ROM patch で開始死亡を解消できた。確認ログ `logs/smvl-rootcheck-fallback-host-1300-20260527`, `logs/smvl-rootcheck-fallback-client-1300-20260527` では frame 1290 まで `player0Lives=5`, `player1Lives=5`, `player0Dead=0`, `player1Dead=0`、life call は初期 `setPlayerLives` のみ。
- FPSが低い。JIT OFFが10fps級の主因だった。JIT ON + hash/trace/screenshotなしでは単独hostが約67fps、ローカル2プロセス同時では約47-55fps。実2PCでは1PCあたり1インスタンスなので単独hostの数値が近い。ローカル2プロセス検証速度は引き続き改善対象。
- Luigi死亡中に敵やブロックアニメが止まるように見える。frame 1923以降の trace では Luigi死亡演出中に `player0UpdateLocked=1` も立つため、現時点ではNSMB本体の死亡演出停止である可能性が高い。direct entry/PacketBridgeの副作用かどうかは、通常LocalMP/実機相当ルートとの比較が残る。
- 安定実行の入口は `scripts/run-nsmb-mvl-stable-split.ps1` に寄せる。US原本から stable host/client UI ROM を再生成し、`-RunRole host` / `-RunRole client -Peer <host-ip>` で実2PC相当の分離起動ができる。ローカル別job検証では frame 1800 まで開始残機減少なし、frame 3000 では host/client state一致かつ `-RequirePlayer0Input -RequirePlayer1Input` 通過。

## 実装済み

- US 版 ROM 解析/patch tooling
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
  - `direct-mvl-entry`
  - `--force-ready-progress`
  - `--force-transfer-result`
  - `--clear-actor-category-mask`
  - `--force-scene-settings`
  - `--call-load-mvsl-files`
  - `--call-load-mvsl-files-after`
- direct MvL ROM 生成
  - 生成物: `roms/nsmb-us-direct-mvl-entry-ready-transfer-clear-mask-settings-files.nds`
  - git には含めない。
- PacketBridge
  - US 版の下位 packet 読み取り API hook を移植済み。
  - `Net::getConsoleKeys`
  - `Net::getPacketByte`
  - `Net::getPacketTick`
  - `Net::getPacketAction`
  - direct MvL では Net GGID が `0` になるため、試合中 context 判定を `stageGroup=9 && vsMode=1` でも許可。
  - `-PacketBridgeStartFrame` を ARM hook 側にも反映し、StageStart/Scene 切り替え中に adapter が早すぎて介入しないようにした。
  - local player も remote player と同じ `LookupTickDelay` の packet を読むように修正済み。
  - `-PacketBridgeNeutralizeLocalInput` を追加。ローカル実入力を直接NSMBへ渡さず、送信packetのkeysだけに反映する。
  - `-HostRom` / `-ClientRom` を追加し、role別ROMを検証可能にした。ただし role別 direct ROM は client 側の player actor 生成が安定せず、現時点の本筋から外す。
  - US direct MvL gameplay 中は legacy LocalMP packet slot mirror を止めるように修正済み。`0x0208B040 + player * 0x3E` の player1 slot が `Entrance::spawnEntrance*` (`0x0208B094+`) と重なり、PacketBridge 開始後に `Player::viewTransitState` で data abort していたため。
- 自動検証
  - screenshot / game-state trace / packet replay log / packet bridge trace に対応。
  - player powerup / inventory / dead / character / battle star / collected star などを extended game-state trace に追加済み。
  - `scripts/verify-nsmb-mvl-lan-result.ps1` で actor 座標、死亡状態、スター actor、battle/collected star の host/client 一致を検証可能。
  - verifier に `-RequirePlayer0Input` / `-RequirePlayer1Input` を追加し、双方向入力が実際にtraceへ出ていることを必須化できる。
  - `-RequireStarPickup` / `-RequireStarRespawn` を追加し、スター取得と次スター再生成を状態値で必須チェックできる。
  - `-HostLogDir` / `-ClientLogDir` を追加し、host/client を別々の script invocation で起動したログも比較可能にした。
  - LAN smoke script から `-VsStarSnapFrame` / `-PlayerSnapToStarFrame` / `-PlayerStickToStarStartFrame` を指定可能にした。これは自然操作ではなく、RNG/再生成同期の制御検証用。
  - LAN smoke script に `-RunRole both|host|client`, `-Peer`, `-LanHost` を追加。2PC相当の片側起動が可能。
  - 標準split検証用の `scripts/run-nsmb-mvl-standard-split.ps1` を追加。host/client通常ROM / `-PacketBridgeDirectCapture` / canonical local0 の長い起動条件をまとめた。
  - 標準split helper はデフォルトで `camera-fallback-slot-zero` ROM を `roms/nsmb-us-direct-mvl-entry-entranceff-flag1-camera-fallback.tmp.nds` に生成し、`ForceStageCameraSlot` なしで検証する。`-NoCameraFallbackRom` を渡すと旧ROMを直接使える。
  - helper は `-RunRole both|host|client` に対応。ローカル2ジョブ検証では `both`、実2PCでは host側 `host`、client側 `client -Peer <host-ip>` を使う。
  - helper にFPS切り分け用の `-NoGameStateTrace`, `-NoScreenshots`, `-NoHashLog`, `-NoFrameLimit`, `-FixedFrameTime`, `-TargetFps`, `-AllowJitWithPacketBridge` を追加。
  - default の client ROM は通常ROMに戻した。camera-full-p1 ROMは表示ズレがあるため実験用に降格。
  - `ForcePlayerDeathCounters` と `ForcePlayerLives` を追加したが、これは診断用の暫定補正。標準split helperのデフォルトからは外し、開始残機減少が検証で見える状態へ戻した。
  - `TracePlayerLifeChanges` を追加し、lives/deaths/dead/pipe transition が変化した瞬間だけ player actor の詳細状態を stdout に出せるようにした。
  - `playerActor*TransitionStep` と `playerActor*TransitFunc` の trace offset を修正。`transitionStep=Player+0xBAD`, `transitFunc=Player+0x990` を読む。
  - `ForceStageCameraSlot` を追加。残機補正ではなく、direct entry で未初期化の `Stage::camera*` remote slot を診断的に初期化し、`isOutOfViewVertical()` 起点の false pit death を検証するためのフック。
  - `PacketBridgeForceGameLocalPlayerIDEarly` を追加。`Game::localPlayerID=1` を stage初期化前から固定しても上画面描画が直るかを切り分けるための診断フック。
  - `TraceStageCamera` を追加。`StageCamera` 更新/描画付近の `Game::localPlayerID`, `Stage::camera*`, view matrix hash, DISPCNT/BG scroll を stdout に出し、カメラ、view matrix、描画対象のどこがズレるかを切り分ける。
  - `ForceCameraFocusLoopCount` を追加。overlay0 の camera focus loop が direct route では `player0` で止まるため、診断用に `Game::updatePlayerCameraFocus(1)` まで回せる。role別に host/client 限定も可能。
  - `RamDumpFrames` / `RamDumpInterval` を標準split helperへ通し、client local0/local1 などのMAINRAM差分を同じ手順で採取できるようにした。
  - `ForceStageFXSettings` を追加。`StageFX` actor settings の bit差分が上画面描画崩れの主因かを診断するための一時フック。runtime settings を直すだけでは上画面空表示は直らなかった。
  - `tools/nsmb_localplayer_ref_report.py` を追加。PC相対LDRが `Game::localPlayerID` を読む命令だけを抽出し、近傍シンボルへ紐づける。overlay10 の候補を `logs/nsmb-us-overlay10-localplayer-refs-20260527.csv` に出力済み。
  - `tools/nsmb_screenshot_probe.py` を追加。上画面下部の地形/空ピクセル比率を見て、stateは一致しているが上画面が空、という失敗を自動検出する。
  - `tools/nsmb_screenshot_probe.py` を拡張し、緑一色バックドロップもfailできるようにした。`RequireStageVisibleScreenshots` は `--max-green-backdrop-ratio` / `--max-dominant-ratio` も使う。
  - 標準split helper から `CallTrace` / `CallTraceAddrs` / `CallTraceStartFrame` / `CallTraceEndFrame` を渡せるようにした。静的候補のうち実行中に踏まれるものを短時間で確認するため。
  - verifier に `-RequireNoLifeLossUntilFrame` を追加。開始残機減少を「スクショ目視」ではなく、`player*Lives` / `player*Deaths` / `player*Dead` と `Game::losePlayerLife` / `Game::addPlayerDeath` call trace で fail できる。
  - verifier に `-RequireStageVisibleScreenshots` を追加。最新スクリーンショットを `tools/nsmb_screenshot_probe.py` で確認し、sky-only 画面を成功扱いしない。
  - helper script は `logs/nsmvl-standard-helper-client-right-host-1800-20260527`, `logs/nsmvl-standard-helper-client-right-client-1800-20260527` で smoke と split verifier 通過。
  - `scripts/generate-nsmb-mvl-client-ui-rom.ps1` を追加。安定base ROMから `StageFX + StageLayout inventory HUD` の client UI ROM を再生成できる。
  - `scripts/generate-nsmb-mvl-client-play-rom.ps1` を追加。安定base ROMから `StageFX player1 + StageLayout inventory all-read player1 + StageCamera player1 + Stage::setZoom player1` の client play 候補 ROM を再生成できる。
  - `scripts/generate-nsmb-mvl-stable-roms.ps1` を追加。US原本ROMから stable host ROM と stable client UI ROM を再生成できる。
  - `scripts/run-nsmb-mvl-stable-split.ps1` を追加。stable host/client ROM を使う標準実行ラッパー。`-GenerateRoms` でROM生成から実行まで行える。
  - `scripts/verify-nsmb-mvl-stable-split.ps1` を追加。stable route の成功条件として `-RequirePlayer0Input -RequirePlayer1Input` をデフォルトで必須にする verifier ラッパー。
  - host/client 別入力スクリプトを追加済み。
    - `tests/nsmb_us_direct_mvl_host_right.inputs`
    - `tests/nsmb_us_direct_mvl_client_right.inputs`
    - `tests/nsmb_us_direct_mvl_both_different.inputs`
    - `tests/nsmb_us_direct_mvl_star_collect_left.inputs`
    - `tests/nsmb_us_direct_mvl_client_inventory_touch.inputs`
    - `tests/nsmb_us_direct_mvl_client_inventory_touch_local.inputs`
- 追加した検証フック
  - `ForceMvlPlayerReady` を PowerShell script から指定可能にした。
  - `ForceMvlRuntimeState` を追加し、US direct entry と自然ルートの差分だった MvL runtime state byte `0x020CA6AC` を検証用に強制できるようにした。
  - StageLayout MvL branch のゲート/内部フィールドを trace に追加した。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE` と `MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT` を追加し、StageLayout MvL 初期化を単発で検証できるようにした。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER` を追加し、`StageLayout + 0xA8CC` に診断用 0x2000 byte buffer を差し込めるようにした。
- 開始死亡対策のROM patch
  - `tools/nsmb_us_rom_patch.py camera-fallback-slot-zero` を追加。
  - `direct-mvl-entry --camera-fallback-slot-zero` でも同じ patch を適用できる。
  - `StageActor::isOutOfViewVertical` (`0x020A06DC`, overlay0) から overlay0 内のゼロ埋め code cave `0x020C5298` へ分岐し、参照先 `Stage::cameraHeight[playerID]` が 0 の場合だけ `playerID=0` の camera bounds を使う。
  - overlay0 末尾追記は `0x020CA280` 以降の BSS/global と衝突して direct entry を壊したため不採用。ROMサイズ/overlay RAM範囲を変えない code cave 方式にした。
- 追加のROM patch診断
  - `tools/nsmb_us_rom_patch.py camera-player1-out-of-view-slot0` を追加。`isOutOfViewVertical(player1)` だけ slot0 camera bounds を使い、player1 camera生成と死亡判定を分離する。
  - `tools/nsmb_us_rom_patch.py camera-focus-loop-count --count 2` を追加。overlay0 `0x020BAAE4` / `0x020BAC18` の camera focus loop count取得を `mov r0,#2` に置換し、JIT有効でも `updatePlayerCameraFocus(1)` が走るようにする。
  - `tools/nsmb_us_rom_patch.py stage-set-zoom-camera-player-id` を追加。`Stage::setZoom` 内の `Stage::cameraX/Width` literal を player slot別に差し替え、StageCamera以外の描画camera入力を検証できるようにする。
  - `tools/nsmb_us_rom_patch.py stagefx-display-player-id` を追加。`StageFX::updateStart` / `updateLose` / `updateClear` / `updateVsTimesUp` 内の表示系 `Game::localPlayerID` 読み取りだけを固定し、ゲーム全体の `Game::localPlayerID` を変えずに開始/勝敗/タイムアップ表示だけをLuigi側に寄せられるか検証できる。
  - `tools/nsmb_us_rom_patch.py stage-layout-inventory-display-player-id` を追加。StageLayout の下画面HUDが `Game::getPlayerInventoryPowerup()` を読む箇所だけを player0/player1 に固定し、アイテム消費側の `setPlayerInventoryPowerup()` は触らずにストック表示を切り替える診断patch。
  - `tools/nsmb_us_rom_patch.py vs-results-display-player-id` を追加。VSResults scene の win/lose 判定用 local player read を固定する診断patch。現時点では player1勝利時に client へ `You Win!` を出せるが、player0勝利時に client 側 lose path の資源選択が壊れて data abort するため、最終採用不可。
  - `tools/nsmb_us_rom_patch.py overlay0-localplayer-literal-alias` を追加。`client localPlayerID=1` の緑画面切り分け用。`--mode layout` でStageLayout系localPlayerID literalのみを0読みへaliasでき、地形/BG表示が戻ることを確認済み。
  - `tools/nsmb_us_rom_patch.py player-render-wrap-x-offset` を追加。`Player::onRender` の表示用Xへwrap offsetを足す診断patch。StageCameraをplayer0側へ戻すとclient localID=1でもMario 3D modelが見えるため、player model描画自体は生きている。
  - `tools/nsmb_us_rom_patch.py stage-layout-final-view-player-id` を追加。`0x020BACC0` literal aliasの効果を狭める診断patchで、StageLayout更新関数末尾の view/player 引数だけを固定できる。`player0` 固定で緑画面は消えるが、player model欠落は残る。
  - `tools/nsmb_us_rom_patch.py player-render-r12-offset` を追加。`Player::renderModel` entry の `r12` 差分を疑った診断patch。ただし `r12 -= 0x400000/0x800000` ではplayer modelは戻らず、この仮説は優先度を下げる。
  - `ForceStageCameraSlotVerticalOnly` を追加。camera slot copyのうちY/heightだけをコピーする診断フック。ただし現時点ではこれだけではLuigi側player表示は戻らない。

## 最新の検証結果

### FPS / 検証速度

2026-05-27時点の切り分け:

- 旧release buildは `ENABLE_JIT=OFF` だったため、PacketBridge検証は実質インタプリタ実行になっていた。
- `cmake -S . -B build\release-windows-x86_64 -DENABLE_JIT=ON` で release を再構成し、`-AllowJitWithPacketBridge` で PacketBridge 使用時もJITを許可するようにした。
- 2026-05-27 の trace追加直後に `-PacketBridgeAllowJit` で frame 10 `pc=00000004` の prefetch abort が出た。原因は JIT BLX_reg trace がジャンプ先を保持していた `RSCRATCH` を C++ trace 呼び出しで破壊していたこと。trace後に target を復元して解消。
- JIT + PacketBridge の最小起動確認: `logs/smvl-packetbridge-jit-branchfix-host-200-20260527` は frame 200 まで abort なし。
- `-NoGameStateTrace -NoScreenshots` だけでは約11fpsのまま。trace/screenshotは主因ではない。
- `-NoHashLog` はCSVを止めるだけでなく、`MELONDS_NSML_DISABLE_HASH=1` でハッシュ計算自体も止めるようにした。
- JIT有効 + hash/trace/screenshotなし:
  - `logs/smvl-fps-jit-nohash-host-1800-20260527`: host内部 `52.76fps`
  - `logs/smvl-fps-jit-nohash-client-1800-20260527`: client内部 `53.61fps`
- `-NoFrameLimit` ではhost単体 `68.84fps` まで出るため、CPUが常に10fps相当しか出ない状態ではない。
- ただし stable PacketBridge 入力同期では、2026-05-27時点で `-AllowJitWithPacketBridge` を付けると frame 1980 以降の送信packetには非ゼロkeysが出る一方、game-state trace の `inputPlayer0Held` / `inputPlayer1Held` と actor movement が 0 のままになる。JIT実行時は interpreter 側の packet API hook がゲームロジックへ反映されないため、JITはまだ安定条件に含めない。検証ログ: `logs/smvl-stable-wrapper-jitinput-host-3000-20260527`, `logs/smvl-stable-wrapper-jittrace2-host-2100-20260527`。
- 通常ROM同士 + JIT + hash/trace/screenshotなし:
  - host単独 `logs/smvl-fps-clean-hostonly-host-1800-20260527`: 約 `67.25fps`
  - ローカルhost/client 2プロセス同時 `logs/smvl-fps-clean-normalrom-host-1800-20260527`, `logs/smvl-fps-clean-normalrom-client-1800-20260527`: host内部 `50.08fps`, client内部 `54.61fps`
  - 2プロセス同時はこのPC上の検証負荷が強く、実2PCの1インスタンス/PC条件とは分けて見る。
- `-FixedFrameTime` / `-TargetFps` は追加済みだが、PacketBridgeありの長め検証ではまだ実測60fpsへ張り付かない。次はPacketBridge per-frame処理と描画/SaveManager flushのどちらが残りの差分かを測る。

### 初期位置/初期死亡/死亡時停止

2026-05-27時点の切り分け:

- `logs/nsmvl-us-direct-entry-split-camera-full-both-different-host-3600-20260527` と client 側 trace では、`playerActor0X` / `playerActor1X` は host/client で一致している。ただしcamera-full-p1 ROMのclientスクショでは、地形/ブロックに対するキャラ位置が破綻している。これは「内部座標一致」とは別の表示バグ。
- 通常ROMをclientにも使うと、host/clientの上画面は地形相対で一致する。ログ: `logs/smvl-normalrom-both-display-host-1800-20260527`, `logs/smvl-normalrom-both-display-client-1800-20260527`
- 右下ストックHUDは、正準 `Game::localPlayerID=0` のままだと host/client とも player0 の在庫を表示する。診断フックで `player0InventoryPowerup=0x1`, `player1InventoryPowerup=0x4` を強制した検証では、client側だけ `stage-layout-inventory-display-player-id --player-id 1 --mode hud` を当てると右下HUDが player1 側の青いアイテム表示に変わり、frame 1300 まで開始残機減少なし、state一致、stage-visible verifier を通過した。ログ: `logs/smvl-invhudpatch-host-1350-20260527`, `logs/smvl-invhudpatch-client-1350-20260527`。これは「下画面ストック表示」は display-only patch で分離できる可能性が高い、という結果。
- 勝敗結果画面は `ForcePlayerStarCounters` 診断フックで短時間発火できるようにした。`player1BattleStars=5` の検証では `VSResults` scene (`SceneID=0xA`) へ遷移し、`vs-results-display-player-id --player-id 1` で client に `You Win!` を出せた。ログ: `logs/smvl-vsresults-text-p1win-host-1600-20260527`, `logs/smvl-vsresults-text-p1win-client-1600-20260527`。
- ただし `player0BattleStars=5` で client を負け表示にするケースは、同patchで `pc=02066EBC lr=02155F48 fault=00000020` の data abort になる。`VSResults` は `object+0x9B` を複数の資源index選択にも使っており、win/lose stateだけをlocal player1にすると lose path のtile sourceが破綻する。ログ: `logs/smvl-vsresults-text-p0win-client-1600-20260527`。次は `VSResults` object の `+0x70..+0x9B` と winner/local player resource table の関係を追う。
- 結果画面patchを除いた `StageFX + stage-layout-inventory-display-player-id` client UI ROM は、通常入力で frame 3600 まで verifier 通過。開始残機減少なし、stage visible、host/client state一致、ARM abortなし。ログ: `logs/smvl-stable-clientui-host-3600-20260527`, `logs/smvl-stable-clientui-client-3600-20260527`。当面の安定client UI候補はこの組み合わせ。
- client play ROM候補:
  - `scripts/generate-nsmb-mvl-client-play-rom.ps1` は `StageFX + StageLayout inventory all-read + StageCamera state/onUpdate + Stage::setZoom` を player1 表示/読み取りへ寄せる。
  - `logs/smvl-client-play-camera-romsetzoom-host-2400-20260528`, `logs/smvl-client-play-camera-romsetzoom-client-2400-20260528` は frame 2400 verifier 通過。clientスクショでLuigiが上画面に表示され、host/client state一致とstage-visibleを維持した。
  - `logs/smvl-client-play-camera-alias-host-2400-20260528`, `logs/smvl-client-play-camera-alias-client-2400-20260528` では runtime `RenderCameraAlias` だけでは上画面がMario側のままで、Luigi視点として不採用。
  - `PacketBridgePreserveLocalTouch` を追加し、local button はpacket-onlyのまま、touchだけはclient UIへ渡せるようにした。`logs/smvl-client-inventory-preservetouch-client-2400-20260528` では touch frame 付近で `NetPacketByte5=0x1` / `NetPacketByte6=0xD9` が出るが、`player1InventoryPowerup` はまだ消費されていない。次はこのpacket actionをhost側で同じtickに読ませるか、NSMBのItem/StageLayout側でどの条件が不足しているかを追う。
- `scripts/generate-nsmb-mvl-client-ui-rom.ps1` で生成した client UI ROM でも frame 1800 smoke と verifier 通過。ログ: `logs/smvl-generated-clientui-host-1800-20260527`, `logs/smvl-generated-clientui-client-1800-20260527`。
- `scripts/generate-nsmb-mvl-stable-roms.ps1` でUS原本から再生成した host/client ROM でも frame 1300 smoke と verifier 通過。ログ: `logs/smvl-stable-generator-host-1300-20260527`, `logs/smvl-stable-generator-client-1300-20260527`。
- `scripts/run-nsmb-mvl-stable-split.ps1` でも frame 1300 smoke と verifier 通過。ログ: `logs/smvl-stable-wrapper-host-1300-20260527`, `logs/smvl-stable-wrapper-client-1300-20260527`。
- `scripts/run-nsmb-mvl-stable-split.ps1 -RunRole host` と `-RunRole client -Peer 127.0.0.1` を別PowerShell jobから起動しても frame 1800 まで通過。ログ: `logs/smvl-stable-wrapper-role-host-1800-20260527`, `logs/smvl-stable-wrapper-role-client-1800-20260527`。
- stable wrapper の frame 3000 双方向入力検証も通過。ログ: `logs/smvl-stable-wrapper-both-3000-20260527`, `logs/smvl-stable-wrapper-both-client-3000-20260527`。`-RequirePlayer0Input -RequirePlayer1Input -RequireStageVisibleScreenshots` を通し、frame 2040以降に `inputPlayer0Held` / `inputPlayer1Held` が非ゼロになり、host/client の player actor 座標も一致した。`-RequireRemoteInputHits` は packet replay CSV 前提の旧判定なので、このROM patch経路の成功条件には `RequirePlayer*Input` を使う。
- stable wrapper で `-SendDelayFrames 4 -SendJitterFrames 4 -LookupTickDelay 10` のWAN遅延相当を入れても frame 2400 verifier 通過。ログ: `logs/smvl-stable-delay4-jitter4-host-2400-20260527`, `logs/smvl-stable-delay4-jitter4-client-2400-20260527`。
- camera patch切り分け:
  - `stage-camera-state-player-id` は3D actor側だけがズレる表示を作りやすく、現状不採用。
  - `stage-camera-player-id` / display-only も完全なLuigi視点ではない。
  - 実localPlayerID=1 client routeはstate一致するが、client画面がプレイヤー不在の右側表示になったため不採用。ログ: `logs/smvl-client-game-local1-retest-1800-20260527`
- 初期 `player1Dead=1` は frame 992 で先に立ち、frame 1112 に `player1Lives=5->4`, `player1Deaths=0->1` になる。`Game::playerDead[1]` の書き込み元は `Player::beginDeathTransition()`。その後 `PlayerBase::onDefeated()` が `Game::losePlayerLife(1)` / `Game::addPlayerDeath(1)` を呼ぶ。ログ: `logs/smvl-statefunc-trace-host-1150-20260527`, `logs/smvl-playerdead-watch-host-1030-20260527`
- actor内部watchでは frame 991 に player1 の transition state が `defaultTransitState` から `pitDeathTransitState` へ切り替わる。これは Goomba接触ではなく、初期 pipe/spawn 後の pit/death 判定。ログ: `logs/smvl-p1-state-watch-host-1000-20260527`
- `Player::defaultTransitState()` から呼ばれる画面外死亡チェックは `StageActor::isOutOfViewVertical(FxRect, playerID)`。RAM dumpでは frame 990-992 の `Stage::cameraY[0]=0x60000`, `Stage::cameraHeight[0]=0xC0000` に対して、`Stage::cameraY[1]=0`, `Stage::cameraHeight[1]=0` のまま。player1 は `playerID=1` なので、同じ `y=0xFFF20000` でも player0 だけ安全、player1 だけ縦画面外扱いになる。これは表示カメラだけでなくゲーム内死亡判定の入力値。
- baseline再確認ログ `logs/smvl-camera-slot-baseline-host-1150-20260527`: frame 992 で `cam={... y=00060000/00000000 ... h=000C0000/00000000}`、`player1Dead=1`, `p1 transitFunc=021196B0`。frame 1112 で `player1Lives=4`, `player1Deaths=1`。
- 診断フック `ForceStageCameraSlot` で slot0 の `Stage::cameraX/Y/Width/Height` を slot1 へ初期化すると、`logs/smvl-camera-slot-mirror-host-1300-20260527` では frame 992 でも `player1Dead=0`, `player1Lives=5`, `p1 transitFunc=0211E670` のまま。よって開始残機減少の直接原因は `Stage::camera*` remote slot 未初期化でほぼ確定。
- JIT + PacketBridge + host/client splitでも確認済み。`logs/smvl-camera-slot-split-branchfix-host-1800-20260527`, `logs/smvl-camera-slot-split-branchfix-client-1800-20260527` は frame 1800 まで abort/timeoutなし、host/clientとも frame 1006 で `lives=5/5`, `dead=0/0`, `p1 transitFunc=0211E670`。ローカル2プロセス同時で実効 `56.84fps`。
- `ForceStageCameraSlotEndFrame=1008` の短時間 bootstrap だけでも frame 1800 まで開始死亡なし。ログ: `logs/smvl-camera-slot-bootstrap-host-1800-20260527`。これは診断として有効だったが、恒常mirrorは遠距離時の画面外判定や勝敗/UIまで正しい保証がないため最終修正にはしない。
  - ROM patch版の確認:
  - 初回実装は overlay0 末尾 `0x020CA280` にstubを追記して BSS/global と衝突し、direct entry が進まなかった。失敗ログ: `logs/smvl-camera-fallback-compare-patched-host-1300-20260527`。
  - code cave `0x020C5298` へstubを置く方式に修正。`logs/smvl-camera-fallback-cave-host-1300-20260527` は frame 1006 で `lives=5/5`, `dead=0/0`, `p1 transitFunc=0211E670`。
  - `ForceStageCameraSlot` なしの host/client split でも開始死亡なし。`logs/smvl-camera-fallback-cave-split-host-1800-20260527`, `logs/smvl-camera-fallback-cave-split-client-1800-20260527` は frame 1800 まで通過。
  - 標準split helperのデフォルトもROM fallbackへ移行済み。`logs/smvl-standard-fallback-default-host-1800-20260527`, `logs/smvl-standard-fallback-default-client-1800-20260527` は `ForceStageCameraSlot` ログなしで frame 1800 まで通過し、スクリーンショットも通常の地形表示。
- `MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG` は開始保護の候補。終了フレーム後にfreeze flagを0へ戻す処理を追加した。ただし単純に frame 960-1800 で敵を止めるだけでは、解除後にGoombaがLuigiへ到達して死亡する。次は本来の開始保護/カウントダウン相当をROM/状態側で再現するか、player1 spawn位置/敵初期状態をROM patchで直す。

標準に近い検証条件は今後 `host localPlayerID=0 / client localPlayerID=1` を本筋にする。過去の `canonical local0` は packet/RNG 同期の制御検証には有効だったが、Luigi側UXを満たさないため最終方針ではない。

### 入力 packet 差し替え

これまでに分かったこと:

- host/player0 と client/player1 の入力を packet として相互に読ませる `PacketBridge` は動作する。
- `PacketBridgeStartFrame=1500`, `LookupTickDelay=10`, `PacketBridgeDirectCapture` などの条件では、canonical local0 の制御検証で座標、死亡状態、スター位置、スター再生成RNGが一致した。
- `-RunRole host` / `-RunRole client -Peer <host-ip>` による2PC相当の分割起動も通る。
- ただし canonical local0 は「同期検証」用であり、clientがLuigiとして自然に遊べる状態ではない。現在は localID=1 client の表示/カメラ/StageLayout 問題を優先している。
- 注意: `-PacketBridgeDirectCapture` を外すと split client で player1 入力が packet に乗らない。現在の安定条件には必須として扱う。

現在の表示系切り分け:

- `client localPlayerID=1` でも actor/lives/dead のstateは揃うが、上画面が緑一色になる。
- overlay0 StageLayout系localPlayerID literalを0読みへaliasすると地形/BGは戻る。
- `0x020BACC0` の単独alias、または `stage-layout-final-view-player-id --player-id 0` でも地形/BGは戻る。全StageLayout aliasは広すぎるため、今後はこの狭いpatchを基準にする。
- その状態でもplayer modelは出ない。`Player::onRender` と `NNS_G3dDraw` は呼ばれており、地形viewとPlayer model 3D view/projectionがまだ噛み合っていない。
- `player-render-wrap-x-offset + StageCamera player0` ではclient localID=1でもMario modelが出る。次はこの診断結果をLuigi側cameraへ寄せる。

### MvL 管理状態とスター生成

一部解決。US direct MvL ROM の `Game::loadLevel` 引数を切り分けた結果、`flag=1` がスター生成に必要な最小条件に見える。

- `flag=1` 単独: スター actor が出る。
- `unused1=1` 単独: スター actor は出ない。
- `controlOptions=0xFF` 単独: スター actor は出ない。
- `entrance=0xFF` 単独: `stageSceneSettings=0xB4FF00` にはなるが、スター actor は出ない。
- `entrance=0xFF + flag=1`: スター actor が出て、`stageSceneSettings=0xB4FF00` になる。旧 PacketBridge slot mirror 有効時は `Player::viewTransitState` 周辺で停止していたが、slot overlap 修正後は frame 3000 まで停止なし。

また、direct route では host/client の初期 `Net::random.value` が違うため、`flag=1` だけではスター位置が一致しない。`MELONDS_NSML_NET_RANDOM_VALUE=0x12345678` と `MELONDS_NSML_NET_RANDOM_AUTO=1` を direct MvL gameplay 判定でも効くように修正したところ、host/client の初期スター座標は一致した。

検証済みログ:

- `logs/nsmvl-us-direct-entry-flag-only-seed1234-auto-2400-20260526`
  - frame 2400 まで到達。
  - host/client とも `vsStarActorFound=1`。
  - host/client とも `vsStarActorX=0x2c0000`, `vsStarActorY=0xfff30000`。
- `logs/nsmvl-us-direct-entry-unused1-only-seed1234-1200-20260526`
  - `vsStarActorFound=0`。
- `logs/nsmvl-us-direct-entry-controlff-only-seed1234-1200-20260526`
  - `vsStarActorFound=0`。
- 修正前ログ: `logs/nsmvl-us-direct-entry-flag-only-seed1234-nomove-3600-20260526`
  - 入力なしでも host が frame 3120 付近で `arm9PC=0xFFFF0104`, `arm9LR=0x02118BE4`。
- 修正前ログ: `logs/nsmvl-us-direct-entry-entranceff-flag1-seed1234-nomove-3000-20260526`
  - `stageSceneSettings=0xB4FF00` かつスター actor あり。
  - host が frame 2400 付近で `arm9PC=0xFFFF0104`, `arm9LR=0x02118BE4`。

`0x02118BE4` は overlay10 の `Player::viewTransitState(void*)` 内。data abort の実原因は、PacketBridge の legacy LocalMP slot mirror が `Entrance::spawnEntrance` 系 global を上書きしていたことだった。direct route の entrance 初期化不足という仮説は今回の停止原因としては棄却。

追加した診断:

- `tools/nsmb_us_rom_patch.py` の `direct-mvl-entry` に `act`, `entrance`, `flag`, `unused1`, `controlOptions`, `unused2`, `challengeMode` を明示指定できるオプションを追加。
- `0xFFFFFFFF` など `mov` で表現できない即値を扱うため、ROM patch stub の即値ロードに `mvn` fallback を追加。
- `MELONDS_NSML_NET_RANDOM_AUTO` を GGID なしの direct MvL gameplay (`stageGroup=9 && vsMode=1`) でも適用するように修正。
- player actor の診断用 player ID trace を `Player + 0x11E` に修正。
- `MELONDS_NSML_FORCE_PLAYER_ACTOR_IDS` を追加。`Player + 0x11E` を actor0=0, actor1=1 に補正できるが、これだけでは `0x02118BE4` の停止は解消しなかった。
- `Entrance::spawnEntranceID`, `Entrance::transitionFlags`, `Entrance::spawnEntrance` を extended game-state trace に追加。
- `MELONDS_NSML_FORCE_PLAYER_TRANSITION_STATUS` と `MELONDS_NSML_FORCE_ENTRANCE_SPAWN_POINTERS` を追加。ただし今回の停止原因は direct ROM の初期化不足ではなく PacketBridge の legacy slot mirror overlap だった。

### PacketBridge slot overlap 修正

解決済み。

`PacketBridgeReplayOps keys,byte,tick,action` と `NSML_RefreshMarioVsLuigiPacketSlots()` が legacy LocalMP packet slot (`0x0208B040 + player * 0x3E`) へ packet を mirror していた。US direct MvL gameplay では player1 slot 範囲が `Entrance::spawnEntranceID/transitionFlags/spawnEntrance` と重なり、PacketBridge 開始後に `Entrance::spawnEntrance[1]` が `0x00030000` へ壊れていた。

対応:

- gameplay 中は legacy LocalMP slot mirror をデフォルト無効化。
- 必要な場合だけ `MELONDS_NSML_PACKET_BRIDGE_WRITE_GAMEPLAY_LOCAL_MP_SLOTS=1` で旧挙動を戻せるようにした。
- packet 取得は下位 packet API hook の scratch packet (`0x023C1000`) を使う。

検証済みログ:

- `logs/nsmvl-us-direct-entry-packetbridge-no-slot-overlap2-2300-20260526`
  - frame 2300 まで host/client とも data abort なし。
  - host/client とも `entranceSpawnID0=0`, `entranceSpawnID1=1`, `entranceSpawnPtr0=0x0229A9C4`, `entranceSpawnPtr1=0x0229A9D8` を維持。
- `logs/nsmvl-us-direct-entry-packetbridge-no-slot-overlap2-3000-20260526`
  - frame 3000 まで host/client とも data abort なし。
  - host/client とも `vsStarActorFound=1`, `vsStarActorX=0x2c0000`, `vsStarActorY=0xfff30000`。
  - `Entrance::spawnEntrance*` も維持。

過去の false lead:

- `mvlObject267` は `Stage::objectIDTable[0x010B] == 0x0145` から来る見かけ上の値で、`Stage::spawnStageObject` は profile `0x0145` を即 return する。試合管理 actor として追わない。
- `loadMvsLFilesThread` を `Game::loadLevel` 前後に呼ぶだけでは StageLayout / player transition 初期化は揃わない。

## 現在の課題

1. `client localPlayerID=1` の試合開始画面を自然なLuigi側表示にする。緑一色上画面はStageLayout系 localPlayerID 参照のaliasで地形/BGが戻るが、player modelはまだLuigi側cameraで自然に出ていない。
2. `player-render-wrap-x-offset + StageCamera player0` ではclient localID=1でもMario modelが出る。これは描画経路が生きている証拠だが、成功条件ではない。次はplayer1 cameraのまま、wrap/vertical/view matrixのどこでplayer modelが落ちるかを切り分ける。
3. 開始直後のactor構成、地形相対の表示、HUD、下画面、死亡/勝敗演出が、`host=player0`, `client=player1` の自然な役割解釈になっているかを確認する。これが通るまでストックアイテムや勝敗結果画面の個別検証へ進まない。
4. ローカル2プロセス同時検証は、安定条件のJITなしでは約10-11fps、JITありなら40fps台まで上がるが入力同期がゲームロジックへ反映されない。実用速度へ近づけるには、JIT側で PacketBridge packet API hook を正しく扱うか、ROM patch側でpacket API境界を置換してJITでも同じ値を返せるようにする必要がある。
5. Luigi がクリボーで死亡した後、復帰まで stage 全体の動きが止まるように見える。NSMB本体仕様か、direct entry/PacketBridge副作用かを比較検証する。

## 次にやること

1. `stage-layout-final-view-player-id --player-id 0` を基準に、StageLayoutの地形だけを戻した状態で、Player modelがなぜ上画面へ出ないかを3D view/projection側から追う。
2. `layoutalias + wrapx + cam0` でplayer modelが見えるログと、`finalview0 + wrapx + player1 camera` で見えないログを比較し、Player modelの投影行列/viewport/camera inputの差分を特定する。
3. 成功判定は「stage visible」だけでなく、player modelが出ていることをスクリーンショット/フレームバッファ検査でfailできるようにする。
4. 双方向入力の検証は、localID1表示gateが通ってから `-RequirePlayer0Input -RequirePlayer1Input` を戻す。`-RequireRemoteInputHits` の旧packet replay依存と混同しない。
5. 高速化はJITを無条件に許可しない。`-AllowJitWithPacketBridge` は現状「速度計測用/失敗再現用」で、成功判定には使わない。JIT対応を再開する場合は、PacketBridgeの `Net::getConsoleKeys` / `getPacketByte` / `getPacketTick` / `getPacketAction` hook がJIT実行でもguest R0へ反映されることを最初に検証する。
6. 実WAN相当の評価は、ENet reliable 前提で遅延/ジッタ中心に続ける。packet lossは「reliable retransmitによる遅延」としてまず扱う。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 成功条件は少なくとも次を確認する。
  - data abort / fatal / undefined がない。
  - 「通信が切断されました」画面がない。
  - host/client で想定した player input が game-state trace に出る。
  - 対応する actor 座標が動く。
  - screenshot が MvL stage として読める。
  - 画面確認は目視だけでなく、可能なら `-RequireStageVisibleScreenshots` で上画面地形ピクセルが存在することを確認する。
  - スター取得は `player*BattleStars` などの状態値で確認する。
- ROM 生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、達成済み、課題、次作業が上から読める形に保つ。

## 実2PC相当の実行メモ

同一PCで分離起動する場合:

```powershell
.\scripts\run-nsmb-mvl-stable-split.ps1 -Frames 3000 -Port 8181 -HostLogDir logs\host -ClientLogDir logs\client
.\scripts\verify-nsmb-mvl-stable-split.ps1 -HostLogDir logs\host -ClientLogDir logs\client -FromFrame 1500 -ToFrame 2990
```

実2PCでは host 側:

```powershell
.\scripts\run-nsmb-mvl-stable-split.ps1 -RunRole host -Port 8181 -HostLogDir logs\mvl-host
```

client 側:

```powershell
.\scripts\run-nsmb-mvl-stable-split.ps1 -RunRole client -Peer <host-ip> -Port 8181 -ClientLogDir logs\mvl-client
```

現時点では `-AllowJitWithPacketBridge` は成功条件に使わない。JITありでは送信packetにkeysが出てもゲームロジック側の `inputPlayer*Held` に反映されないため。
