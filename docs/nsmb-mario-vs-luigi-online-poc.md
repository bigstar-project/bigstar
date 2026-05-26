# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate 共有、試合開始後の WAN 切り替え、actor/state 強制同期は、切断、desync、不自然な内部状態、低 FPS の問題が大きいため最終方針から外す。

## 現在の方針

US 版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信 API 解析の精度を優先する。

方針は次の 2 本。

1. ROM patch で LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
2. 試合中に NSMB が読む packet/input API を WAN adapter に差し替え、NSMB 側の同期処理をできるだけそのまま使う。

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

成功済み。

- host/player0 の RIGHT/A/B 入力が client 側 `player=0` packet として読まれる。
- client/player1 の RIGHT/A/B 入力が host 側 `player=1` packet として読まれる。
- host/player0 RIGHT、client/player1 LEFT の同時入力で、frame 3600 まで主要 actor/input/star 関連 trace の mismatch は `0`。
- ログ: `logs/nsmvl-us-direct-entry-both-different-3600-20260526`

### MvL 管理状態とスター生成

未解決。

direct ROM は MvL stage / HUD / player actor までは表示できるが、frame 4800 でも `vsStarActorFound=0` のまま。

自然にスターが出た過去ログでは次の状態が見えている。

- `mvlObject267StateType=0x1`
- `mvlGlobal9670=0x3`
- `mvlManagerHalf494=0xff00`
- `mvlManagerHalf4A0=0xff00`
- `mvlManagerByteA8EC=0xff`
- `vsStarActorFound=0x1`

今回の direct ROM に対して検証用に次を強制したが、スター actor はまだ出ていない。

- `ForceStageSceneRuntimeWords`
- `ForceMvlPlayerReady`
- `ForceMvlRuntimeState`

ログ:

- `logs/nsmvl-us-direct-entry-runtimewords-4800-20260526`
- `logs/nsmvl-us-direct-entry-runtimewords-ready-3600-20260526`
- `logs/nsmvl-us-direct-entry-runtime-state-3600-20260526`

このため、現在の direct entry は「見た目のステージ開始」には到達しているが、MvsL の試合管理 actor / StageLayout 周辺の初期化が自然ルートとまだ一致していない。

## 現在の課題

1. direct ROM の MvL 初期化が不足しており、スター生成が始まらない。
2. `ForceMvlPlayerReady` と `ForceMvlRuntimeState` を足しても不足しているため、`mvlObject267` または StageLayout 周辺の追加初期化を特定する必要がある。
3. direct entry 側の強制 ready は client の actor 座標を壊すケースがある。試合開始状態を自然化するまでは、これを最終実装に入れない。
4. 8コインアイテム取得は自動化が難しいため後回し。まずスター生成/取得が自然に進む状態を優先する。

## 次にやること

1. US direct entry と、スターが出た自然ルートの `mvlObject267` / StageLayout 周辺フィールドをさらに比較する。
2. `mvlObject267` の更新関数または初期化関数を US シンボル/逆アセンブルから追い、ROM patch 側で自然に呼ぶべき関数を特定する。
3. 必要なら direct ROM の `Game::loadLevel` 直呼びを見直し、`VSStageIntro` / `VSConnectScene::updateLoadGameSM` に近い経路で開始する。
4. スター actor が自然に出るようになったら、入力スクリプトでスター取得を検証する。取得判定はスクショではなく `player*BattleStars` / `player*CollectedStars` / star actor の再生成で行う。
5. その後、8コインアイテム、死亡/復帰、長時間プレイ、WAN 遅延条件を順に検証する。

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
