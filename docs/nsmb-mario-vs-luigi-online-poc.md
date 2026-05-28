# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析の精度を優先する。

2026-05-28 の検証で、`client Game::localPlayerID=1` の direct MvL route は、表示だけでなく object spawn set も host とズレることが分かった。具体的には host local0 では Goomba/movingHazard が存在する一方、client local1 direct route では同じframeで `movingHazardFound=0` になり、入力同期以前にsimulationが一致しない。overlay0 localPlayerID alias を広く当ててもこの差は解消しなかった。

このため、現時点の本筋は次に変更する。

- simulation は host/client とも canonical `Game::localPlayerID=0` に揃える。
- client は表示、HUD、StageFX、PacketBridge local player を player1/Luigi 役に寄せる。
- NSMBが読む試合中packet/input/touch APIをWAN adapterへ差し替え、host/clientで同じcanonical packet列を読ませる。
- `client localPlayerID=1` の自然再現は研究対象として残すが、現時点では最終WAN対戦へ向けた主経路から外す。
- actor座標、敵、スター、残機、死亡状態がhost/clientで一致することを成功条件にする。見た目だけの補正や死亡/残機カウンタ補正は成功扱いしない。

NSMB Central の解析どおり、MvsL は接続時に RNG seed を同期し、試合中は主に入力情報 packet を通信している前提で進める。ただし「入力だけで同期できる」ためには、敵やステージ処理が両PCで同じ入力列・同じsimulation条件を読む必要がある。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 現在の最優先課題

現在の最優先は、canonical local0 simulation + client player1表示/UI/input のrouteを、実際に遊べるWAN netplayへ近づけること。

- 最新の成功ログ:
  - `logs/smvl-hybrid-display1-split-framelead20-2240-20260528`
    - host/client とも canonical `localPlayerID=0`
    - client ROM は `StageCamera player1 + StageFX player1 + inventory HUD player1`
    - `PacketBridgeLookupTickDelay=10`
    - frame 1500-2240 で host/client state verifier 通過
  - `logs/smvl-hybrid-avoidgoomba-delay60-split-3000-20260528`
    - `PacketBridgeLookupTickDelay=60`
    - frame 1500-3000 で actor座標、敵、残機/死亡状態を含むstate一致
    - `-RequirePlayer0Input -RequirePlayer1Input -RequireStageVisibleScreenshots` 通過
  - `logs/smvl-hybrid-helper-3000-20260528`
    - `scripts/run-nsmb-mvl-hybrid-split.ps1` から再現
    - frame 1500-3000 で `-RequirePlayer0Input -RequirePlayer1Input -RequireStageVisibleScreenshots` 通過
  - `logs/smvl-hybrid-helper-renderdefault-3000-20260528`
    - hybrid helperの標準ROM生成で `Player::renderModel visible arg` patchをhost/client双方に適用
    - client上画面でMario/Luigi両方のplayer model表示を確認
    - frame 1500-3000 でstate verifier通過
  - `logs/smvl-hybrid-safe-bothjump-4200-20260528`
    - `tests/nsmb_us_direct_mvl_safe_short.inputs`
    - Mario/Luigiをその場ジャンプさせ、frame 1500-4200 で `RequirePlayer0Input -RequirePlayer1Input -RequireNoLifeLossUntilFrame 4200` 通過
  - `logs/smvl-hybrid-separated-host-3000-20260528` / `logs/smvl-hybrid-separated-client-3000-20260528`
    - `RunRole host` と `RunRole client` を別PowerShell jobとして起動
    - frame 1500-3000 で別ログディレクトリ比較 verifier 通過
- 最新の未解決:
  - client表示はまだ広いQAが必要。Goombaについては `Goomba::onRender` と `OAM/drawSprite` がclientでも呼ばれ、単独スクリーンショットで描画を確認したため、直近の差分はcamera差分の可能性が高い。player modelはhost/client双方へ同じrender-visible patchを当てると表示できるが、cullingを雑に外しているため最終品質としては要改善。
  - `tests/nsmb_us_direct_mvl_safe_short.inputs` はMario/Luigi両者入力あり・死亡なしの4200frame安全ルート。次はさらに長時間化し、実操作に近い左右移動やスター/8コインアイテム検証へ広げる必要がある。
  - `PacketBridgeLookupTickDelay=10` ではclientのlocal player1 packetがhostより先に反映されることがある。delay 60 では同期できたため、最終的にはlockstep待ち/入力遅延の自動調整が必要。
  - client側のHUD/カメラ/StageFXはplayer1へ寄せているが、trace上の `Game::localPlayerID` はcanonical 0 のまま。勝敗演出、ストックアイテム使用、死亡演出がLuigi視点として成立するかは未検証。
  - JIT + PacketBridgeはまだ成功条件に使わない。`logs/smvl-hybrid-jit-trace-2300-20260528` ではpacket API hook自体は値を返すが、game-stateの `inputPlayer*Held` へ反映されない。`logs/smvl-hybrid-jit-branchdone-safe-3000-20260528` のBL skip実験は試合開始前で止まったため破棄した。

直近の次アクション:

- hybrid helperを使って2PC相当のhost/client分離実行へ移し、同じROM/PacketBridge条件をWAN向けに検証する。
- `PacketBridgeLookupTickDelay=60` は固定条件として入った。次は固定値ではなく、lockstep待ち/入力遅延の自動調整へ進める。
- 死亡しない両者入力スクリプトを、4200 frameからさらに長時間へ伸ばす。
- client Luigi視点で、敵/アイテム/死亡/勝敗演出/ストックHUDが自然に成立するかを、スクリーンショットと状態値の両方で検証する。特にrender-visible patchは表示改善には有効だが、clientだけに当てるとstate差分が出るため必ずhost/client双方へ同じpatchを当てる。
- 高速化はJIT core hookを直接いじる前に、ROM patch側でpacket API境界を置換できるか再検討する。JIT実験は `-AllowJitWithPacketBridge -PacketBridgeTrace` で再現可能。

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
  - `tools/nsmb_us_rom_patch.py stage-camera-state-vertical-slot-zero` を追加。StageCamera state関数のY/height参照だけslot0へ向け、runtime ForceStageCameraでは間に合わないview matrix生成タイミングをROM側で切り分ける。
  - `tools/nsmb_us_rom_patch.py player-render-range-view-player-id` を追加。`Player::renderModel()` から `Stage::isOutsidePlayerRange` へ渡るviewIDだけを固定し、player model欠落がrange/camera slot由来かを診断できる。
  - `tools/nsmb_us_rom_patch.py player-view-transit-local-player-id` / `player-vs-pipe-local-player-id` を追加。`Player::viewTransitState` / `Player::vsPipeTransitState` 内だけlocalPlayerID比較を固定する診断patch。localID1 object set不一致の直接解決にはならなかった。
  - `tools/nsmb_us_rom_patch.py stage-entity-skip-render-player-id` を追加。hybrid client表示で `StageEntity::skipRender` のcamera slotだけplayer1へ寄せる診断patch。Goombaは後続のcalltraceでclient側でもrender pathが呼ばれることを確認済み。
  - `tools/nsmb_us_rom_patch.py player-render-wrap-x-offset` は負方向offsetも受け取れるようにした。単純なdisplay X wrap補正ではlocalID1のplayer model欠落は解消しなかった。
  - `scripts/generate-nsmb-mvl-hybrid-roms.ps1` / `scripts/run-nsmb-mvl-hybrid-split.ps1` を追加。canonical local0 simulation + client player1表示/UI/input の成功条件を再現するための標準helper。標準生成ではhost/client双方へ `player-render-model-visible` も当て、片側だけの描画patch副作用を避ける。
  - `tests/nsmb_us_direct_mvl_avoid_goomba.inputs` を追加。Luigiを早めに動かし、PacketBridge入力遅延とstate同期を検証しやすくするための暫定入力スクリプト。
  - `tests/nsmb_us_direct_mvl_safe_short.inputs` を追加。Mario/Luigiをその場ジャンプさせ、最初のGoomba接触を避ける4200frame安全ルート。
  - `TraceNSMLPlayerRender` を追加。`Player::onRender`, `Player::renderModel()`, `Player::renderModel(bool)` のframe, actor, playerID, characterID, visibleFlag, display vector, model pointerをstdoutに出せる。
  - `ForcePlayerActorPosition` を追加。player actor位置/character/playerIDを一時的に書き換え、描画欠落が座標・キャラ・playerIDのどれに依存するかを切り分ける診断フック。

## 最新の検証結果

### 2026-05-28 hybrid route

- `client localPlayerID=1` direct routeは、表示だけでなくobject spawn setがhostと一致しない。`logs/smvl-local1-raw-client-2040-20260528` と `logs/smvl-local1-rangeview0-client-selfinput-2240-20260528` では `movingHazardFound=0` のまま。host local0では同じ区間でGoomba/movingHazardが存在し、これがplayer1へ干渉する。
- canonical local0 simulationに戻し、client側だけ `StageCamera player1 + StageFX player1 + inventory HUD player1` を当てるhybrid ROMでは、host/clientのobject setが一致する。`logs/smvl-hybrid-display1-split-framelead20-2240-20260528` は frame 1500-2240 で verifier 通過。
- `tests/nsmb_us_direct_mvl_avoid_goomba.inputs` と `PacketBridgeLookupTickDelay=60` の組み合わせでは、`logs/smvl-hybrid-avoidgoomba-delay60-split-3000-20260528` が frame 1500-3000 で verifier 通過。player0/player1入力が入り、actor座標、敵、死亡/残機状態がhost/clientで一致した。後半に死亡はあるが両者一致しており、desyncではなく入力ルートの問題。
- `PacketBridgeLookupTickDelay=10` では、clientが自分のplayer1 packetをhostより早く読んで frame 1560 から差分が出た。入力遅延/lockstep制御はWAN対戦の必須要素。
- hybrid client表示のGoombaについては、`Goomba::onRender` と `OAM/drawSprite` がclientでも呼ばれ、単独スクリーンショットで描画を確認した。split screenshot上の見え方差分はcamera差分の可能性が高く、現在は「Goomba render gateが壊れている」とは扱わない。
- `player-render-model-visible` をclientだけに当てると frame 1620 でstate mismatchした。host/client双方に同じpatchを当てると `logs/smvl-hybrid-render-visible-both-3000-20260528` と `logs/smvl-hybrid-helper-renderdefault-3000-20260528` で verifier 通過。描画関数にも状態副作用があり得るため、表示patchは左右で一致させる。

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
- 2026-05-28 のhybrid JIT再検証:
  - `scripts/run-nsmb-mvl-hybrid-split.ps1` に `-AllowJitWithPacketBridge` / `-PacketBridgeTrace` を追加し、hybrid routeでもJIT失敗を再現できるようにした。
  - `logs/smvl-hybrid-jit-safe-3000-20260528` は約68秒で3000frame完走するが、`inputPlayer0Held` / `inputPlayer1Held` が0のまま。
  - `logs/smvl-hybrid-jit-trace-2300-20260528` ではpacket replay log上は非ゼロkeysを返せているが、ゲーム側入力状態には反映されない。JIT core側でBLをスキップする実験は試合開始を壊したため採用しない。
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

### 入力 packet 差し替え

これまでに分かったこと:

- host/player0 と client/player1 の入力を packet として相互に読ませる `PacketBridge` は動作する。
- canonical local0 simulation では、clientをplayer1入力/表示へ寄せてもhost/clientのstateを一致させられる。
- `-RunRole host` / `-RunRole client -Peer <host-ip>` による2PC相当の分割起動も通る。
- 注意: `-PacketBridgeDirectCapture` を外すと split client で player1 入力が packet に乗らない。現在の安定条件には必須として扱う。
- `PacketBridgeForceTick` 使用時にDirectCapture送信packet自身のtickを書き換えていなかった問題は修正済み。`CaptureAndSendNSMLPacketLocked()` で送信packet[0:1]もcanonical tickへ正規化する。
- `PacketBridgeLookupTickDelay=10` ではclient側のlocal player1 packetがhostより先に反映されることがある。`PacketBridgeLookupTickDelay=60` では `logs/smvl-hybrid-avoidgoomba-delay60-split-3000-20260528` がstate verifierを通った。
- `PacketBridgeMaxFrameLead=20` と `PacketBridgeThrottleStartFrame=1500` は有効。`PacketBridgeWait` は双方待ちでタイムアウトしやすい。

localID1 route の切り分け結果:

- `client localPlayerID=1` の緑一色上画面は、overlay0 StageLayout系localPlayerID literalを0読みへaliasすると地形/BGが戻る。
- `stage-camera-state-vertical-slot-zero + player-render-range-view-player-id --player-id 0` で、client localID1の上画面にMario/Luigi両方が表示される。player model欠落はactor不在ではなくrange/camera判定由来。
- ただしlocalID1 direct routeはobject setがhost local0と一致しない。Goomba/movingHazardがclient側に出ないため、最終WAN同期の主経路としては現時点で不採用。

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

1. `client localPlayerID=1` direct routeは、表示だけでなくobject spawn setがhostと一致しないため、主経路から外す。以後はcanonical local0 simulation + client player1表示/UI/input のhybrid routeを本筋にする。
2. DirectCaptureでclient自身はplayer1入力を読め、`PacketBridgeLookupTickDelay=60` と frame lead throttle を使えばhost/client state一致まで到達する。この条件はhybrid helperへ固定済みなので、次はhost/client分離実行とWAN/2PC検証へ移す。
3. 開始直後のactor構成、地形相対の表示、HUD、下画面、死亡/勝敗演出が、`host=player0`, `client=player1` の自然な役割解釈になっているかを確認する。これが通るまでストックアイテムや勝敗結果画面の個別検証へ進まない。
4. ローカル2プロセス同時検証は、安定条件のJITなしでは約10-12fps、JITありなら40fps台まで上がるが入力同期がゲームロジックへ反映されない。実用速度へ近づけるには、JIT側で PacketBridge packet API hook を正しく扱うか、ROM patch側でpacket API境界を置換してJITでも同じ値を返せるようにする必要がある。

## 次にやること

1. hybrid helperを実2PCで動かし、`PacketBridgeLookupTickDelay=60` 条件のまま切断や片側先行入力が出ないかを見る。同一PC上のhost/client別wrapper jobは3000frame通過済み。
2. `tests/nsmb_us_direct_mvl_safe_short.inputs` を4200frameより長時間化し、`RequirePlayer0Input -RequirePlayer1Input -RequireNoLifeLossUntilFrame` をより長いframe範囲で通す。スター/8コインアイテムはその後。
3. client Luigi視点のQAを追加する。成功判定は「stage visible」だけでなく、player model、敵、HUD、死亡演出、勝敗演出が自然に見えることをスクリーンショット/フレームバッファ検査でfailできるようにする。
4. 高速化はJITを無条件に許可しない。`-AllowJitWithPacketBridge` は現状「速度計測用/失敗再現用」で、成功判定には使わない。JIT対応を再開する場合は、PacketBridgeの `Net::getConsoleKeys` / `getPacketByte` / `getPacketTick` / `getPacketAction` hook がJIT実行でもguest R0へ反映されることを最初に検証する。

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

同一PCでまとめて検証する場合:

```powershell
.\scripts\run-nsmb-mvl-hybrid-split.ps1 `
  -Frames 4200 `
  -InputScript tests\nsmb_us_direct_mvl_safe_short.inputs `
  -LogDir logs\hybrid-both `
  -RegenerateRoms
```

同一PCでhost/client wrapperを分ける場合:

```powershell
.\scripts\run-nsmb-mvl-hybrid-split.ps1 `
  -RunRole host `
  -Frames 3000 `
  -InputScript tests\nsmb_us_direct_mvl_safe_short.inputs `
  -LogDir logs\hybrid-host `
  -Port 8241 `
  -SkipVerify
```

別PowerShellでclient側:

```powershell
.\scripts\run-nsmb-mvl-hybrid-split.ps1 `
  -RunRole client `
  -Peer 127.0.0.1 `
  -Frames 3000 `
  -InputScript tests\nsmb_us_direct_mvl_safe_short.inputs `
  -LogDir logs\hybrid-client `
  -Port 8241 `
  -SkipVerify
```

分離ログを比較する場合:

```powershell
.\scripts\verify-nsmb-mvl-stable-split.ps1 `
  -HostLogDir logs\hybrid-host `
  -ClientLogDir logs\hybrid-client `
  -FromFrame 1500 `
  -ToFrame 3000 `
  -RequireNoLifeLossUntilFrame 3000 `
  -RequireStageVisibleScreenshots
```

実2PCではhost側:

```powershell
.\scripts\run-nsmb-mvl-hybrid-split.ps1 `
  -RunRole host `
  -Frames 3000 `
  -InputScript tests\nsmb_us_direct_mvl_safe_short.inputs `
  -LogDir logs\mvl-host `
  -Port 8241 `
  -SkipVerify
```

client側:

```powershell
.\scripts\run-nsmb-mvl-hybrid-split.ps1 `
  -RunRole client `
  -Peer <host-ip> `
  -Frames 3000 `
  -InputScript tests\nsmb_us_direct_mvl_safe_short.inputs `
  -LogDir logs\mvl-client `
  -Port 8241 `
  -SkipVerify
```

現時点では `-AllowJitWithPacketBridge` は成功条件に使わない。JITありでは送信packetにkeysが出てもゲームロジック側の `inputPlayer*Held` に反映されないため。
