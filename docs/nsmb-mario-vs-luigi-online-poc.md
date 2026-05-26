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
  - host/client 別入力スクリプトを追加済み。
    - `tests/nsmb_us_direct_mvl_host_right.inputs`
    - `tests/nsmb_us_direct_mvl_client_right.inputs`
    - `tests/nsmb_us_direct_mvl_both_different.inputs`
- 追加した検証フック
  - `ForceMvlPlayerReady` を PowerShell script から指定可能にした。
  - `ForceMvlRuntimeState` を追加し、US direct entry と自然ルートの差分だった MvL runtime state byte `0x020CA6AC` を検証用に強制できるようにした。
  - StageLayout MvL branch のゲート/内部フィールドを trace に追加した。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_GATE` と `MELONDS_NSML_CALL_MVL_STAGE_LAYOUT_INIT` を追加し、StageLayout MvL 初期化を単発で検証できるようにした。
  - `MELONDS_NSML_FORCE_MVL_STAGE_LAYOUT_BUFFER` を追加し、`StageLayout + 0xA8CC` に診断用 0x2000 byte buffer を差し込めるようにした。

## 最新の検証結果

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

1. 3600 frame の双方向入力同期は成立したが、これはローカル2プロセス、固定遅延、packet lossなしの条件。
2. `Game::localPlayerID=0` 正準化でプレイ表示、カメラ、UI、勝敗処理が実用上問題ないかは未確認。
3. まだスター取得/再生成、死亡/復帰後の長時間同期、実WAN遅延/packet loss条件は未検証。
4. 8コインアイテム取得は自動化が難しいため後回し。

## 次にやること

1. canonical local0 + packet-only 構成で 3600 frame より長い同期検証を行い、死亡/復帰後も mismatch しないか確認する。
2. スター取得スクリプトを修正し、取得判定を `player*BattleStars` / `player*CollectedStars` / star actor 再生成で確認する。死亡演出や見た目だけで成功判定しない。
3. WAN想定の遅延/packet loss/ジッタをスクリプトまたはENet層で注入し、必要な入力遅延と待機条件を決める。
4. 表示・操作上、clientが `Game::localPlayerID=0` のままで問題ないかをスクリーンショットと操作ログで確認する。

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
