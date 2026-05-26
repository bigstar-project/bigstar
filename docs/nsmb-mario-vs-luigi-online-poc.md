# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析の精度を優先する。

現在の有望ルートは「全ピアが同じ正準シミュレーションを持ち、入力だけを player packet として交換する」形。

- ROM patch で LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
- 試合中に NSMB が読む packet/input API を WAN adapter に差し替える。
- host/client のゲーム内 `Game::localPlayerID` は両方 `0` に固定する。
- ただし WAN adapter 上の送信者は host=player0、client=player1 として扱う。
- ローカル実入力は NSMB に直接渡さず、packet としてだけ送る。

NSMB Central の解析どおり、MvsL は接続時に RNG seed を同期し、試合中は主に入力情報 packet を通信している前提で進める。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 現在の最優先課題

ユーザー観察ベースで、次の4点を優先して潰す。

- host/client の上画面で Mario/Luigi の初期位置が違って見える。trace上の world座標差と、client表示用camera ROMによる screen座標差を分けて確認する。
- 開始直後に Luigi が死亡してから始まる。traceとスクショ上、初回の `player1Dead=1` は土管出現遷移中の内部フラグに見えるが、その後 frame 1950付近でGoombaがLuigiに接触して実死亡する。direct entryが本来の開始保護/カウントダウンを飛ばしている可能性が高い。
- FPSが低い。trace/screenshot ではなく、JIT OFF、ハッシュ計算、フレームリミッタ、PacketBridge処理が主因。現在はJITあり・hashなし・traceなしで内部55fps前後、無制限では68fps前後まで回復。固定60fps化は継続調整中。
- Luigi死亡中に敵やブロックアニメが止まるように見える。NSMB本来の死亡/リスポーン停止なのか、direct entry/PacketBridgeの副作用なのかを trace とスクリーンショットで分ける。

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
  - 標準split検証用の `scripts/run-nsmb-mvl-standard-split.ps1` を追加。host normal ROM / client camera-full-p1 ROM / `-PacketBridgeDirectCapture` / canonical local0 の長い起動条件をまとめた。
  - helper は `-RunRole both|host|client` に対応。ローカル2ジョブ検証では `both`、実2PCでは host側 `host`、client側 `client -Peer <host-ip>` を使う。
  - helper にFPS切り分け用の `-NoGameStateTrace`, `-NoScreenshots`, `-NoHashLog`, `-NoFrameLimit`, `-FixedFrameTime`, `-TargetFps`, `-AllowJitWithPacketBridge` を追加。
  - default の client camera ROM が無い場合、helper が `tools/nsmb_us_rom_patch.py` で自動生成する。生成ROMは git には含めない。
  - helper script は `logs/nsmvl-standard-helper-client-right-host-1800-20260527`, `logs/nsmvl-standard-helper-client-right-client-1800-20260527` で smoke と split verifier 通過。
  - host/client 別入力スクリプトを追加済み。
    - `tests/nsmb_us_direct_mvl_host_right.inputs`
    - `tests/nsmb_us_direct_mvl_client_right.inputs`
    - `tests/nsmb_us_direct_mvl_both_different.inputs`
    - `tests/nsmb_us_direct_mvl_star_collect_left.inputs`
- 追加した検証フック
  - `ForceMvlPlayerReady` を PowerShell script から指定可能にした。
  - `ForceMvlRuntimeState` を追加し、US direct entry と自然ルートの差分だった MvL runtime state byte `0x020CA6AC` を検証用に強制できるようにした。
  - StageLayout MvL branch のゲート/内部フィールドを trace に追加した。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE` と `MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT` を追加し、StageLayout MvL 初期化を単発で検証できるようにした。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER` を追加し、`StageLayout + 0xA8CC` に診断用 0x2000 byte buffer を差し込めるようにした。

## 最新の検証結果

### FPS / 検証速度

2026-05-27時点の切り分け:

- 旧release buildは `ENABLE_JIT=OFF` だったため、PacketBridge検証は実質インタプリタ実行になっていた。
- `cmake -S . -B build\release-windows-x86_64 -DENABLE_JIT=ON` で release を再構成し、`-AllowJitWithPacketBridge` で PacketBridge 使用時もJITを許可するようにした。
- `-NoGameStateTrace -NoScreenshots` だけでは約11fpsのまま。trace/screenshotは主因ではない。
- `-NoHashLog` はCSVを止めるだけでなく、`MELONDS_NSML_DISABLE_HASH=1` でハッシュ計算自体も止めるようにした。
- JIT有効 + hash/trace/screenshotなし:
  - `logs/smvl-fps-jit-nohash-host-1800-20260527`: host内部 `52.76fps`
  - `logs/smvl-fps-jit-nohash-client-1800-20260527`: client内部 `53.61fps`
- `-NoFrameLimit` ではhost単体 `68.84fps` まで出るため、CPUが常に10fps相当しか出ない状態ではない。
- `-FixedFrameTime` / `-TargetFps` は追加済みだが、PacketBridgeありの長め検証ではまだ実測60fpsへ張り付かない。次はPacketBridge per-frame処理と描画/SaveManager flushのどちらが残りの差分かを測る。

### 初期位置/初期死亡/死亡時停止

2026-05-27時点の切り分け:

- `logs/nsmvl-us-direct-entry-split-camera-full-directcapture-host-3600-20260527` と client 側 trace では、`playerActor0X` / `playerActor1X` は host/client で一致している。画面上の差は主に client camera-full-p1 ROM による `stageDisplayCameraX` 差。
- frame 900 のhostスクショはまだ上画面が遷移中で、clientは表示ROM patchの影響でフィールド表示が先に出る。これは「内部座標ズレ」ではなく表示/遷移差として扱う。
- 初期 `player1Dead=1` は frame 1020-1140 に出るが、スクショではLuigiが右土管から出てくる前後の遷移で、実死亡演出とは違う。
- その後、`tests/nsmb_us_direct_mvl_client_right.inputs` は frame 1980 まで player1 入力が無いため、Goombaが右から歩いてきて frame 1950付近でLuigiに接触する。ここからは実死亡で、Goomba Xも `0x62800` で止まる。
- `MELONDS_NSML_FORCE_STAGE_ACTOR_FREEZE_FLAG` は開始保護の候補。終了フレーム後にfreeze flagを0へ戻す処理を追加した。ただし単純に frame 960-1800 で敵を止めるだけでは、解除後にGoombaがLuigiへ到達して死亡する。次は本来の開始保護/カウントダウン相当をROM/状態側で再現するか、player1 spawn位置/敵初期状態をROM patchで直す。

標準に近い検証条件:

- direct ROM
- `-PacketBridgeStartFrame 1800`
- `-PacketBridgeMaxFrameLead 8`
- `-PacketBridgeLookupTickDelay 10`
- `-PacketBridgeLiveFallbackLatestBefore`
- `-PacketBridgeReplayReturnLookupTick`
- `-PacketBridgeReplayOps keys,byte,tick,action`
- `-PacketBridgeDirectCapture`
- host local player `0`
- client local player `1`

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
  - client camera-full-p1 ROM を含む split 構成でも、no-movement route + `PlayerStickToStar` + `delay=4` + `jitter=4` で `-RequireStarPickup -RequireStarRespawn` 通過。
  - ログ: `logs/nsmvl-us-direct-entry-split-camera-full-star-nomove-delay4-jitter4-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-star-nomove-delay4-jitter4-client-3600-20260527`
  - 最終状態は host/client とも `player0BattleStars=0x1`, 次スター座標 `0x3c0000,0xfff50000`。
  - 同じ表示ROM込み split 構成で `delay=12`, `jitter=8`, `LookupTickDelay=16` でも `-RequireStarPickup -RequireStarRespawn` 通過。
  - ログ: `logs/nsmvl-us-direct-entry-split-camera-full-star-nomove-delay12-jitter8-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-star-nomove-delay12-jitter8-client-3600-20260527`
- 2PC相当の分割起動:
  - `-RunRole host` と `-RunRole client -Peer 127.0.0.1` を別々の PowerShell invocation で起動できることを確認。
  - `logs/nsmvl-us-direct-entry-runrole-split-host-1800-20260526` と `logs/nsmvl-us-direct-entry-runrole-split-client-1800-20260526`: frame 1800 まで split mismatch `0`。
  - `logs/nsmvl-us-direct-entry-runrole-split-star-host-3600-20260526` と `logs/nsmvl-us-direct-entry-runrole-split-star-client-3600-20260526`: split 起動でも `-RequireStarPickup -RequireStarRespawn` 通過。
  - 実2PCでは client 側に `-Peer <host-ip>` を渡す想定。
  - client camera-full-p1 ROM + `-PacketBridgeDirectCapture` の split 双方向入力でも frame 3600 まで split verifier 通過。
  - ログ: `logs/nsmvl-us-direct-entry-split-camera-full-both-different-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-both-different-client-3600-20260527`
  - `-RequirePlayer0Input -RequirePlayer1Input` 付き verifier が通過。最終 player actor 座標も host/client で一致。
- 自然入力のスター取得 route:
  - `tests/nsmb_us_direct_mvl_star_collect_left.inputs` を direct MvL 起動手順込みに修正。
  - `logs/nsmvl-us-direct-entry-star-left-route-packet-only-canonical-local0-7200-20260526` は mismatch `0` で完走したが、`player*BattleStars` / `player*CollectedStars` は変化せず、スター取得は未達。
  - frame 6000 付近の勝利表示はスター取得ではなく player1 の死亡/勝敗状態によるものとして扱う。
- client 表示カメラ:
  - canonical local0 では host/client の内部状態同期は維持できるが、client 側も通常は Mario/player0 寄りのカメラになる。
  - `StageCamera::onUpdate` の display camera X だけを player1 にしてもスクリーンショットはほぼ変わらず、不十分だった。
  - `StageCamera` state function 側の `Game::localPlayerID` 参照を player1 にする ROM patch を追加し、client ROM だけに適用すると、ゲーム状態同期を壊さず client 表示を Luigi/player1 寄りにできることを確認。
  - `StageCamera` state + display camera X の両方を player1 にする結合ROMでも、frame 2700 まで verifier は mismatch `0`。
  - ログ: `logs/nsmvl-us-direct-entry-client-camera-full-p1-rom-canonical-local0-2700-20260527`
  - host normal ROM / client camera-full-p1 ROM の 2PC相当 split 起動でも、`-PacketBridgeDirectCapture` ありで frame 3600 まで smoke と split verifier が通過。
  - client 側 player1 入力は frame 2040-2760 の trace で確認。最終 `playerActor1X` は host/client とも `0x128fff`。
  - client 表示は `stageDisplayCameraX == stageCameraGlobalX1 == 0x3d8000` になり、player1側カメラを使う。
  - ログ: `logs/nsmvl-us-direct-entry-split-camera-full-directcapture-host-3600-20260527`, `logs/nsmvl-us-direct-entry-split-camera-full-directcapture-client-3600-20260527`
  - 注意: `-PacketBridgeDirectCapture` を外すと split client で player1 入力が packet に乗らない。現在の安定条件には必須として扱う。
  - これは client 表示専用ROM patch として扱う。ゲーム内 `Game::localPlayerID` は引き続き host/client とも `0` に正準化する。

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

1. client 表示カメラは player1 寄りにできたが、上画面で Mario/Luigi の初期表示位置が host/client で違って見える。trace上のworld座標ズレか、カメラ差によるscreen座標差かを切り分ける。
2. 現在の direct MvL 入口では開始直後に Luigi/player1 が一度死亡状態になることがある。最終対戦としては unacceptable なので、開始状態または spawn/transition 初期化を修正する。
3. 検証中の実効FPSが10程度まで落ちる。trace/screenshot/hashの負荷なのか、PacketBridge待機/スロットリング/JIT設定なのかを測定可能にし、通常プレイ条件で60fpsを目指す。
4. Luigi がクリボーで死亡した後、復帰まで stage 全体の動きが止まるように見える。NSMB本来の同期停止なのか、PacketBridge/Direct入口の不具合なのかを検証する。
5. client表示ROM込みのsplit構成でもスター取得/再生成同期は成立したが、これは制御hookによる取得であり、自然操作では未達。
6. 自然操作でスターを取りに行く入力 script はまだ未完成。死亡/勝利表示をスター取得と誤判定しないよう、状態値で検証する。
7. 8コインアイテム取得は自動化が難しいため後回し。

## 次にやること

1. 標準split helperにFPS/実行時間計測を追加し、trace/screenshotあり・なしの差を測る。
2. 開始直後の player1 death を traceで最小再現し、spawn/transition/global初期値のどれが原因か特定する。
3. host/clientの上画面差を、world座標・camera値・screenshot上の見え方に分けて確認する。
4. Luigi死亡後のstage停止が `Stage::actorFreezeFlag` や player transition status によるものか確認する。
5. 実WAN相当の評価は、ENet reliable 前提で遅延/ジッタ中心に続ける。packet lossは「reliable retransmitによる遅延」としてまず扱う。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 成功条件は少なくとも次を確認する。
  - data abort / fatal / undefined がない。
  - 「通信が切断されました」画面がない。
  - host/client で想定した player input が game-state trace に出る。
  - 対応する actor 座標が動く。
  - screenshot が MvL stage として読める。
  - スター取得は `player*BattleStars` などの状態値で確認する。
- ROM 生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針、達成済み、課題、次作業が上から読める形に保つ。
