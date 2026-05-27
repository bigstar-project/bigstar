# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析の精度を優先する。

現在の有望ルートは「全ピアが同じ正準シミュレーションを持ち、入力だけを player packet として交換する」形。

- ROM patch で LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
- 試合中に NSMB が読む packet/input API を WAN adapter に差し替える。
- host/client のゲーム内 `Game::localPlayerID` は、安定同期検証では両方 `0` に固定する。現時点ではこれを正準シミュレーション条件として扱う。
- ただし WAN adapter 上の送信者は host=player0、client=player1 として扱う。
- client を実 `Game::localPlayerID=1` にするルートは、単純採用しない。下画面UI/ストックアイテムはLuigi側らしく変わるが、上画面描画、StageFX、object生成まで変わってしまい、同期済みstateの表示だけを切り替える用途には副作用が大きい。
- ローカル実入力は NSMB に直接渡さず、packet としてだけ送る。

NSMB Central の解析どおり、MvsL は接続時に RNG seed を同期し、試合中は主に入力情報 packet を通信している前提で進める。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 現在の最優先課題

ユーザー観察ベースで、次の4点を優先して潰す。

- client で Luigi 側を実際にプレイできる表示/操作系を優先する。現時点の判断では、ゲーム全体の `Game::localPlayerID=1` 化は避け、正準シミュレーションは `localPlayerID=0` のまま維持する。その上で client ROM だけ `StageCamera` / `Stage::setZoom` / `StageFX` / `StageLayout` の表示・下画面読み取りを player1 に寄せる。
- 2026-05-28 の検証で、`StageCamera state + onUpdate + Stage::setZoom` を player1 に揃えた client ROM は、host/client state一致を保ったまま client 上画面にLuigiを表示できた。`RenderCameraAlias` runtime hookだけでは上画面がMario側のままで不十分だった。確認ログ: `logs/smvl-client-play-camera-romsetzoom-host-2400-20260528`, `logs/smvl-client-play-camera-romsetzoom-client-2400-20260528`。
- 右下ストックアイテムは、表示だけなら `stage-layout-inventory-display-player-id --mode hud` で player1 表示にできる。ただし使用操作まで含めるには `--mode all-read` と local touch の扱いが必要。`PacketBridgeNeutralizeLocalInput` が touch も消していたため、`PacketBridgePreserveLocalTouch` を追加した。これで client 側 packet byte は touch時に変化するが、2026-05-28時点では `player1InventoryPowerup` 消費までは未達。次は packet byte5/action のhost側反映と、StageLayout/Item使用処理の境界を追う。
- host/client の上画面で Mario/Luigi の表示位置が違って見える問題は、単なるカメラ差として扱わない。旧 `camera-full-p1` ROM patch は3D actor側とBG/地形側のカメラを揃えられず、地形相対の表示が壊れたため不採用。新しい client play ROM は `scripts/generate-nsmb-mvl-client-play-rom.ps1` で生成し、`Stage::setZoom` も含めて揃える。
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

標準に近い検証条件:

- direct ROM
- `-PacketBridgeStartFrame 1500`
- `-PacketBridgeMaxFrameLead 8`
- `-PacketBridgeLookupTickDelay 10`
- `-PacketBridgeLiveFallbackLatestBefore`
- `-PacketBridgeReplayReturnLookupTick`
- `-PacketBridgeReplayOps keys,byte,tick,action`
- `-PacketBridgeDirectCapture`
- host local player `0`
- client のゲーム内 local player は現状 `0` に正準化する。送信packet上だけ client を player1 として扱う。
- direct entry の remote camera bounds は、診断hookではなく `camera-fallback-slot-zero` ROM patch で扱う方向へ移行する。

### 入力 packet 差し替え

大きく前進。

- host/player0 の RIGHT/A/B 入力が client 側 `player=0` packet として読まれる。
- client/player1 の RIGHT/A/B 入力が host 側 `player=1` packet として読まれる。
- `PacketBridgeStartFrame=1500` でpacketを事前に溜めると、gameplay開始後の `Net::getPacketByte` / `Net::getConsoleKeys` は host/client で同じ値を返す。
- ただし `Game::localPlayerID` を host=0/client=1 にすると、packet APIが一致していても local/remote 処理差で player actor がズレる。
- `Game::localPlayerID` を両方 `0` に固定し、clientは送信上だけ `player=1` packet を出すと、frame 3600 まで player0/player1座標、死亡状態、スター位置の mismatch は `0`。
- ログ: `logs/nsmvl-us-direct-entry-both-different-packet-only-canonical-local0-3600-20260526`
- 同じ canonical local0 + packet-only 構成で単独入力も確認済み。
  - `logs/nsmvl-us-direct-entry-host-right-packet-only-canonical-local0-3600-20260526`: mismatch `0`
  - `logs/nsmvl-us-direct-entry-client-right-packet-only-canonical-local0-3600-20260526`: mismatch `0`
- 長めの検証:
  - `logs/nsmvl-us-direct-entry-both-different-packet-only-canonical-local0-7200-20260526`: mismatch `0`
  - player0/player1 座標、死亡状態、スター座標は trace 間隔内で一致。
- WAN遅延の初期検証:
  - `-PacketBridgeSendDelayFrames` を追加し、NSML packet 送信を人工的に遅らせられるようにした。
  - `-PacketBridgeSendJitterFrames` を追加し、packetごとに最大N frameの決定論的ジッタを足せるようにした。
  - 初期実装は release をframe基準だけにしていたため、frame lead待機中にrelease frameへ進めず `delay=12` で詰まった。壁時計時間でもreleaseするように修正。
  - `logs/nsmvl-us-direct-entry-send-delay4-lookup10-2400-20260526`: `delay=4`, `LookupTickDelay=10`, mismatch `0`
  - `logs/nsmvl-us-direct-entry-send-delay12-lookup10-wallrelease-2400-20260526`: `delay=12`, `LookupTickDelay=10`, mismatch `0`
  - `logs/nsmvl-us-direct-entry-delay4-jitter4-lookup10-2400-20260526`: `delay=4`, `jitter=4`, `LookupTickDelay=10`, mismatch `0`
- スター取得/再生成の制御検証:
  - `PlayerStickToStar` hook で player0 を同じ frame にスター位置へ吸着し、`player0BattleStars=0x1` になることを確認。
  - 次スターが `0x3c0000,0xfff50000` に再生成され、host/client で一致することを確認。
  - verifier の `-RequireStarPickup -RequireStarRespawn` も通過。
  - ログ: `logs/nsmvl-us-direct-entry-star-stick-p0-script-option-canonical-local0-3600-20260526`
  - これは自然入力の成功ではなく、RNG と再生成処理が正準packet同期で一致するかの制御テスト。
- 遅延/ジッタ下のスター取得/再生成:
  - `logs/nsmvl-us-direct-entry-star-stick-delay4-jitter4-canonical-local0-3600-20260526`: `delay=4`, `jitter=4`, `LookupTickDelay=10`, `-RequireStarPickup -RequireStarRespawn` 通過。
  - `logs/nsmvl-us-direct-entry-star-stick-delay12-jitter8-lookup16-canonical-local0-3600-20260526`: `delay=12`, `jitter=8`, `LookupTickDelay=16`, `-RequireStarPickup -RequireStarRespawn` 通過。
  - 少なくとも reliable packet 前提の遅延/ジッタ注入では、スター取得と再生成RNGは正準packet同期で維持できている。
  - 過去に client `camera-full-p1` ROM 込みでも state verifier は通ったが、後でスクリーンショット上の表示破綻が分かったため、現在は表示成功として扱わない。
- 2PC相当の分割起動:
  - `-RunRole host` と `-RunRole client -Peer 127.0.0.1` を別々の PowerShell invocation で起動できることを確認。
  - `logs/nsmvl-us-direct-entry-runrole-split-host-1800-20260526` と `logs/nsmvl-us-direct-entry-runrole-split-client-1800-20260526`: frame 1800 まで split mismatch `0`。
  - `logs/nsmvl-us-direct-entry-runrole-split-star-host-3600-20260526` と `logs/nsmvl-us-direct-entry-runrole-split-star-client-3600-20260526`: split 起動でも `-RequireStarPickup -RequireStarRespawn` 通過。
  - 実2PCでは client 側に `-Peer <host-ip>` を渡す想定。
  - client `camera-full-p1` ROM + `-PacketBridgeDirectCapture` の split 双方向入力でも state verifier は frame 3600 まで通ったが、スクリーンショット表示が破綻していたため成功扱いにしない。
  - ログ: `logs/nsmvl-us-direct-entry-split-camera-full-both-different-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-both-different-client-3600-20260527`
- 自然入力のスター取得 route:
  - `tests/nsmb_us_direct_mvl_star_collect_left.inputs` を direct MvL 起動手順込みに修正。
  - `logs/nsmvl-us-direct-entry-star-left-route-packet-only-canonical-local0-7200-20260526` は mismatch `0` で完走したが、`player*BattleStars` / `player*CollectedStars` は変化せず、スター取得は未達。
  - frame 6000 付近の勝利表示はスター取得ではなく player1 の死亡/勝敗状態によるものとして扱う。
  - client 表示カメラ:
  - canonical local0 では host/client の内部状態同期は維持できるが、client 側も通常は Mario/player0 寄りのカメラになる。
  - `StageCamera::onUpdate` の display camera X だけを player1 にしてもスクリーンショットはほぼ変わらず、不十分だった。
  - `StageCamera` state function 側の `Game::localPlayerID` 参照を player1 にする ROM patch は、trace上のgame-state verifierは通るが、スクリーンショットでは地形/ブロックに対して actor がズレる。ユーザー指摘どおり、これはカメラ差ではなく表示として破綻しているため現状不採用。
  - host normal ROM / client `camera-full-p1` ROM の 2PC相当 split 起動は frame 3600 まで state verifier が通るが、表示が不正なので成功扱いにしない。ログ: `logs/nsmvl-us-direct-entry-split-camera-full-both-different-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-both-different-client-3600-20260527`
  - client も通常ROMに戻すと、frame 1800 のスクリーンショットで地形相対の Mario/Luigi 位置は一致する。ログ: `logs/smvl-normalrom-both-display-host-1800-20260527`, `logs/smvl-normalrom-both-display-client-1800-20260527`
  - client の実 `Game::localPlayerID=1` と p1 direct ROM は、現時点では player actor が生成されず smoke に失敗する。ログ: `logs/smvl-default-p1rom-local1-client-1800-20260527`, `logs/smvl-default-p1files-local1-client-1800-20260527`
  - camera fallback ROM + 同一ROMで `ClientGameLocalPlayerID=1` にすると、player actor は生成され、下画面の右下ストックアイテムもLuigi側らしい表示になる。ログ: `logs/smvl-camera-fallback-local1-samerom-client-1800-20260527`。
  - ただし同条件の上画面は地形/プレイヤーが表示されず空だけになる。`Game::localPlayerID=1` を early に書いても直らない。
  - StageCamera trace では、local1 は `Stage::cameraX/Y/W/H[1]` と `StageCamera` target/pos が 0 のままになる。`ForceStageCameraSlot` で slot1 global camera bounds を埋めると view matrix と `stageDisplayCameraX` は local0 と一致するが、上画面はまだ空のまま。ログ: `logs/smvl-stagecam-trace-local1-client-1250-20260527`, `logs/smvl-stagecam-trace-local1-mirror-client-1250-20260527`。
  - RAM dump 差分では local0 client は object数13、local1+mirror client は object数12。local1+mirror では Goomba actor が消え、`StageFX` actor settings が `0x00008010` になっていた。`ForceStageFXSettings=0x8000` で runtime settings を戻しても上画面空表示は直らない。ログ: `logs/smvl-ramdump-local0-client-1250-20260527`, `logs/smvl-ramdump-local1-mirror-client-1250-20260527`, `logs/smvl-local1-mirror-stagefx8000-client-1250-20260527`。
  - overlay10 の `Game::localPlayerID` 静的参照は StageCamera, Item, StageFX, Player transition, render/effect 系に分布する。短時間の runtime trace `logs/smvl-localplayer-reftrace-host-1300-20260527`, `logs/smvl-localplayer-reftrace-client-1300-20260527` では frame 886-1300 の間に踏まれたのは StageCamera 系のみ。StageFX/Item はこの区間ではまだ踏まれていない。
  - camera focus loop の実行境界も確認した。overlay0 `0x020BAB24` から呼ばれる `Game::updatePlayerCameraFocus` は direct route では通常 `player0` のみで、`0x02046C34` が返す loop count が `1` のため `player1` が処理されない。`ForceCameraFocusLoopCount=2` で `updatePlayerCameraFocus(1)` / `PlayerBase::followCamera(...,1)` は呼ばれ、`Stage::camera* [1]` への書き込みも発生する。ログ: `logs/smvl-camera-loop2-fix-client-1050-20260527`, `logs/smvl-camera-loop2-write-client-1250-20260527`。
  - ただし camera focus loop を `player1` まで回すと、`Stage::cameraY[1]` が一時的に負方向値になり、`StageActor::isOutOfViewVertical(..., playerID=1)` が再び `Player::pitDeathTransitState()` へ入る。`Game::localPlayerID=1 + ForceCameraFocusLoopCountClientOnly` は上画面の sky-only を消し、下画面HUD/ストックもLuigi側らしくなるが、clientだけ `player1Dead=1`, `player1Lives=4`, actor Y divergence になり host と同期不能。ログ: `logs/smvl-local1-camera-loop2-clientonly-host-1300-20260527`, `logs/smvl-local1-camera-loop2-clientonly-client-1300-20260527`。
  - `camera-player1-out-of-view-slot0 + camera-focus-loop-count=2` のROM patch版では、JIT有効でも host/client state と開始残機を維持したまま `Stage::camera* [1]` を生成できた。clientだけ `StageCamera` p1 patch を当てても verifier は通る。ログ: `logs/smvl-romloop2-camerafullp1-host-1300-20260527`, `logs/smvl-romloop2-camerafullp1-client-1300-20260527`。
  - ただしその状態でも上画面スクリーンショットはまだほぼMario側表示に見える。`stageDisplayCameraX` と view matrix は client で p1側に変わるが、画像差分は主に小さいsprite領域のみだった。`Stage::setZoom` の cameraX/Width を p1 slotへ向けても verifier は通るが、上画面は大きく変わらない。ログ: `logs/smvl-romloop2-camerafullp1-setzoomp1-host-1300-20260527`, `logs/smvl-romloop2-camerafullp1-setzoomp1-client-1300-20260527`, `logs/smvl-romloop2-camerafullp1-setzoomp1-host-2800-20260527`, `logs/smvl-romloop2-camerafullp1-setzoomp1-client-2800-20260527`。
  - `stagefx-display-player-id=1` を client ROMだけに当てた検証では、frame 1300 まで開始残機は減らず、frame 2800 まで host/client のplayer座標、死亡状態、actor状態は一致した。ログ: `logs/smvl-stagefx-display-p1-host-1300c-20260527`, `logs/smvl-stagefx-display-p1-client-1300c-20260527`, `logs/smvl-stagefx-display-p1-host-2800-20260527`, `logs/smvl-stagefx-display-p1-client-2800-20260527`。ただし1300/2800スクショでは下画面ストックや上画面カメラの違いはまだ見えないため、これは「StageFX表示だけは限定patchしてもstateを壊さない」という検証結果に留める。
  - 現時点では、上画面は共有カメラ表示のままでもMario/Luigiの地形相対位置は一致する。localPlayerIDをゲームロジックに見せるより、下画面HUD/ストック/勝敗表示を正準stateから読み替える方向を優先する。
  - よって global `Game::localPlayerID=1` はカメラだけでなく `StageFX` / object生成 / result/UI 側まで切り替える。最終ルートとしては副作用が大きく、当面は採用しない。
  - `Game::loadLevel(... playerID=1 ...)` まで揃えた p1 direct ROM も検証したが、client側で stage actor / player actor / star actor が生成されず、`Ready!` または黒画面のまま進まない。ready/transfer/files 系補助を全部入れても同じ。ログ: `logs/smvl-p1-loadlevel-local1-client-1300-20260527`, `logs/smvl-p1-full-local1-client-1300-20260527`, `logs/smvl-p1-full-local0-client-1300-20260527`。
  - p0 direct ROM で stage/player actor 生成後に `Game::localPlayerID=1` へ遅延切り替えする検証も追加。frame 1300 で切り替えると client が `ARM9 pc=00000004` prefetch abort になった。スクリーンショットは直前の通常画面が残るが、実行状態は壊れる。ログ: `logs/smvl-delayed-local1-client-1800-20260527`。
  - 次の表示方針は、ゲーム内 `Game::localPlayerID=0` の正準シミュレーションを維持し、Luigi側UXは display player id を読む箇所を限定patchするか、emulator側overlayでHUD/結果表示を差し替える方向。勝敗判定やストックアイテムを壊さないため、カメラだけを単独で変える実装は成功扱いにしない。`StageFX` は限定patch候補としてstate一致検証を通過したが、Item/HUD系は gameplay消費処理と混ざっているため未採用。
  - 注意: `-PacketBridgeDirectCapture` を外すと split client で player1 入力が packet に乗らない。現在の安定条件には必須として扱う。
  - 注意: 正準化した `Game::localPlayerID=0` は同期検証には有効だが、clientがLuigiとして遊べる最終UXではない。右下ストックアイテム、local player UI、勝敗判定がlocal player依存なら、カメラだけを変えても最終要件を満たせない。
  - 当面は「ゲーム内 `Game::localPlayerID` は host/client とも `0` に正準化し、表示は通常ROM同士で破綻しない状態を基準にする」。Luigi視点/UILayout/勝敗表示は、状態同期とは別に解く。

この結果から、当面は「各ピアのゲーム内 local player は正準化する。操作プレイヤーの違いはWAN adapter側だけで表現する」方針で進める。

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

1. 勝敗結果画面の client 表示が未解決。`vs-results-display-player-id` は player1 勝利表示だけ確認できたが、player0勝利/client敗北で data abort するため採用不可。
2. Luigi視点の上画面カメラは未解決。`camera-full-p1` は地形相対のactor表示が壊れるため不採用。現時点の安定client UIは共有上画面 + Luigi側HUD表示。
3. ローカル2プロセス同時検証は、安定条件のJITなしでは約10-11fps、JITありなら40fps台まで上がるが入力同期がゲームロジックへ反映されない。実用速度へ近づけるには、JIT側で PacketBridge packet API hook を正しく扱うか、ROM patch側でpacket API境界を置換してJITでも同じ値を返せるようにする必要がある。
4. Luigi がクリボーで死亡した後、復帰まで stage 全体の動きが止まるように見える。NSMB本体仕様か、direct entry/PacketBridge副作用かを比較検証する。
5. 自然操作でスターを取りに行く入力 script はまだ未完成。死亡/勝利表示をスター取得と誤判定しないよう、状態値で検証する。8コインアイテム取得は自動化が難しいため後回し。

## 次にやること

1. `scripts/run-nsmb-mvl-stable-split.ps1` を実2PC用の標準入口として整える。host/client別起動コマンド、必要ROM生成、推奨高速フラグ、verifier条件をこのdocsとscriptに反映する。
2. 双方向入力の検証は `-RequirePlayer0Input -RequirePlayer1Input` を必須にし、`-RequireRemoteInputHits` の旧packet replay依存と混同しない。
3. 勝敗結果画面は `VSResults` object の `+0x70..+0x9B` と winner/local player resource table を追い、表示だけをclient player1視点にする。state同期を壊すpatchは採用しない。
4. Luigi死亡後のstage停止がNSMB本体仕様かどうか、通常LocalMP/実機相当ルートとの比較方法を決める。
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
