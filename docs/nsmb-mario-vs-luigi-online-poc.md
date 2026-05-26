# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切替、actor/state強制同期は、通信切断・desync・不自然な内部状態・低FPSの問題が大きいため、最終方針から外す。

## 現在の方針

US版 ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` が US 版のシンボルを持つため、ROM patch と通信API解析の精度を優先する。

主方針は次の2つ。

1. ROM patch または低レベル adapter で、LocalMP UI/接続処理に依存しない MvL 専用入口を作る。
2. 試合中に NSMB が読む packet/input API を WAN adapter に差し替え、NSMB 側の同期処理をできるだけそのまま使う。

NSMB Central の解析どおり、MvL は接続時に RNG seed を同期し、試合中は主に入力情報 packet を通信している前提で進める。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 実装済み

- US版 ROM 解析/patch tooling
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
  - `direct-mvl-entry`
  - `--force-ready-progress`
  - `--force-transfer-result`
  - `--clear-actor-category-mask`
  - `--force-scene-settings`
  - `--call-load-mvsl-files`
- direct MvL ROM
  - 生成物: `roms/nsmb-us-direct-mvl-entry-ready-transfer-clear-mask-settings-files.nds`
  - `Game::loadLevel` 後の `Scene::nextSceneSettings=0x00B4FF00` が必要。
  - `VSConnectScene::loadMvsLFilesThread` 呼び出しが必要。
  - single process で MvL stage / HUD / player actors 到達済み。
- PacketBridge
  - US版下位 Wifi API hook を移植済み。
  - `Net::getConsoleKeys`
  - `Net::getPacketByte`
  - `Net::getPacketTick`
  - `Net::getPacketAction`
  - direct MvL では Net GGID が `0` になるため、試合中 context 判定は `stageGroup=9 && vsMode=1` を許容するよう修正。
  - `-PacketBridgeStartFrame` を ARM 側 hook にも反映し、StageStart/Scene切替中に adapter が早すぎて介入しないようにした。
- 自動検証
  - host/client 別入力スクリプト対応済み。
  - screenshot / game-state trace / packet replay log / packet bridge trace 対応済み。
  - player powerup / inventory powerup / dead / character を extended game-state trace に追加済み。

## 最新の検証結果

標準に近い検証設定:

- direct ROM
- `-PacketBridgeStartFrame 1800`
- `-PacketBridgeMaxFrameLead 8`
- `-PacketBridgeLookupTickDelay 10`
- `-PacketBridgeLiveFallbackLatestBefore`
- `-PacketBridgeReplayReturnLookupTick`

### host入力 -> client側player0

ログ:

- `logs/nsmvl-us-direct-entry-host-right-delay10-20260526`

結果:

- host が player0 に `RIGHT+A` / `RIGHT+B` / `RIGHT` を入力。
- client 側 replay hook で `player=0`, `keys=0x11/0x12`, `hit=1` を確認。
- client 側 `inputPlayer0Held` が `0x11/0x12` に変化。
- client 側 player0 actor 座標が移動。
- data abort / fatal / remote input timeout なし。

### client入力 -> host側player1

ログ:

- `logs/nsmvl-us-direct-entry-client-right-delay10-20260526`

結果:

- client が player1 に `RIGHT+A` / `RIGHT+B` / `RIGHT` を入力。
- host 側 replay hook で `player=1`, `keys=0x11/0x12`, `hit=1` を確認。
- host 側 `inputPlayer1Held` が `0x11/0x12` に変化。
- host 側 player1 actor 座標が移動。
- data abort / fatal / remote input timeout なし。

この時点で、`melonDS 1インスタンス * 2プロセス` の localhost WAN adapter で、試合中の双方向入力packet差し替えが成立し始めている。

## 現在の課題

1. まだ短時間の非対称入力検証のみ。実戦に近い長時間走行で desync / disconnect / black screen が出ないか未確認。
2. `PacketBridgeLookupTickDelay=10` は暫定値。WAN遅延に対して固定値で足りるか、動的調整が必要かを検証する。
3. host/client の actor 座標は近いが完全一致ではないフレームがある。入力遅延・tick正規化・フレーム先行制御のどれで詰めるべきか追加検証が必要。
4. HUDアイテム差分は `playerInventoryPowerup` trace で分類できるようになったが、長めの試合でまだ確認していない。
5. direct ROM 起動はまだメニュー入力スクリプトに依存している。最終的には UI 操作なしで MvL 開始状態へ入る ROM patch に寄せたい。

## 次にやること

1. `host_right` / `client_right` の検証を 3000〜5000 frame に伸ばし、actor座標・input・inventory・star状態の一致/差分を確認する。
2. `PacketBridgeLookupTickDelay` と `PacketBridgeMaxFrameLead` の組み合わせを整理し、最小限の入力遅延で安定する設定を探す。
3. 片方だけでなく、両者が同時に異なる入力を入れるスクリプトを追加する。
4. UI操作を減らす direct MvL ROM patch を進める。
5. 必要なら `Net::getPacket` そのものを返す hook も追加し、byte/tick/action/keys の個別hookだけで不足する場面を潰す。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 成功条件は、少なくとも次を確認する。
  - data abort / fatal / undefined がない。
  - 「通信が切断されました」画面が出ない。
  - host/client で想定した player input が game-state trace に出る。
  - 対応する actor 座標が動く。
  - screenshot が MvL stage として読める。
- ROM生成物、savestate、巨大ログは git に含めない。
- docs は古い追記を残し続けず、現在の方針・達成済み・課題・次作業がすぐ読める形に保つ。
