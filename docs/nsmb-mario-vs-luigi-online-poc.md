# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現在の主方針は、melonDS の LocalMP を WAN 越しにそのまま延長するのではなく、NSMB がローカル通信時に扱う MvL packet / 接続 state-machine を解析し、LocalMP の代わりに WAN adapter から同等の packet と最低限の状態遷移を供給すること。


## 採用しない方針

- savestate 同期方式: 表示座標だけでは stage/object/通信状態が一致せず、対戦基盤として弱い。
- DropMP / 途中WAN切り替え: 切り替え時に NSMB 側の接続状態が壊れやすい。
- `2インスタンス * 2PC` の状態同期: 重く、最終形から遠い。検証用としてのみ使う。
- スクリーンショットだけの成功判定: 死亡演出や黒画面を誤判定しやすいため、game-state trace / actor presence / packet trace を優先する。


## 実装済みの検証基盤

- 入力スクリプト再生
- screenshot / framebuffer dump
- MainRAM dump
- game-state trace
- call/write/random trace
- packet capture / packet bridge trace
- Big Star actor ID `0x00D2` 周辺 probe
- smoke test の厳格判定: `playerActor0Found`, `playerActor1Found`, `vsStarActorFound`
- `run-nsmb-mvl-lan-route-smoke.ps1` の LAN / NoLanMP / PacketBridge 検証オプション
- WAN packet adapter の下層MPフック実験
- `ForceNetReady` / `ForceLoadGameSM` 補助フック


## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- lower MP status probe: `0x0204619C`
- lower MP hasPacket: `0x0204622C`
- lower MP getPacket: `0x02046480`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`
- VSConnect load-game:
  - create: `0x021515B4`
  - update: `0x021512B8`
  - render: `0x0215125C`
- CourseSelect 関連:
  - create: `0x0214F830`
  - factory/request: `0x020130A8`
  - scene request apply: `0x02007ACC`
  - scene transition: `0x02011CE8`


## 現在の到達点

通常LANは最新検証でも `3300` frame まで成功し、`stageGroup=9`, `vsMode=1`, player actors, star actor を確認済み。

NoLanMP + PacketBridge では、`Net::getPacket*` だけを返しても接続は進まない。`transferPacket()` が lower MP の packet pointer を通して内部 buffer valid byte を立てる必要があるため、`0204619C`, `0204622C`, `02046480` を hook する下層MP bridgeを追加した。

下層MP bridgeにより、remote packet は NSMB の `transferPacket()` に届くようになった。pregame中に action `0x01` を強制する実験では、host は `netState24=2`, `VSConnect word078/07C=3/3` まで進む。さらに `ForceNetReady` を併用すると、両側を通常LANに近い `vsMode=1`, `netState1C=6`, `netState20=2`, `netState24=2` に揃えられる。

ただし、まだ PacketBridge だけでは CourseSelect 生成と `stageGroup=9` への自然遷移には届いていない。`ForceLoadGameSM` は通常LANで観測した `VSConnect +0x144/+0x148/+0x154` に近い値を設定できるようになったが、それだけでは CourseSelect は生成されない。


## 新しい重要な知見

通常LANの pre-game packet capture では、接続段階の packet action は以下のように遷移する。

- host: `0x00 -> 0x01 -> 0x02 -> 0x03`
- client: `0x00 -> 0x01 -> 0x02 -> 0x03`

代表的な遷移:

- host action `0x01`: frame `1303`, tick `0x0001`
- host action `0x02`: frame `1803`, tick `0x01F0`
- host action `0x03`: frame `1812`, tick `0x01F5`
- client action `0x01`: frame `1542`, tick `0x00B3`
- client action `0x02`: frame `1761`, tick `0x0189`
- client action `0x03`: frame `1767`, tick `0x01F5`

つまり接続段階では host/client の packet tick は常に同一ではない。action `0x03` 付近で揃う。したがって、接続開始から強制的に共通 tick にする実験は NSMB の handshake 条件を壊す可能性がある。最終的には「接続段階はNSMBの自然なtick/action遷移を尊重し、gameplay開始後に入力同期用のtick管理へ寄せる」設計が必要そう。

通常LANの write trace では、CourseSelect生成前に以下の状態へ進むことを確認した。

- host: `VSConnect +0x144=7`, `+0x148=0x30`, `+0x154=0x00030000`
- client: `VSConnect +0x144=7`, `+0x148=0x27`, `+0x154=0x00030001`

このため `ForceLoadGameSM` の補助値を通常LAN相当に修正し、client 側では `localPlayerID=1` も補正するようにした。

一方、以下は失敗として確認済み。

- `UpdateLoadGameSM` を両側で無理に呼ぶと `ARM9 data abort (023C0008)` が出る。
- `CourseSelectFactory` 直接呼び出しは host 側で `ARM9 data abort (0204C004)` が出る場合があり、CourseSelect object は生成されない。
- `Game::loadLevel()` を SafeCall で直接呼ぶと `stageGroup=9` と `READY!` 画面までは到達するが、5200 frame まで待っても player actors / star actor が生成されない。つまり「面IDだけを切り替える」だけでは試合開始状態として不十分。


## 現在のブロッカー

WAN adapterが packet を渡すだけでは、CourseSelect 生成と実ステージ actor 生成に自然到達しない。

不足しているものは以下のどちらか、または両方の可能性が高い。

- LocalMP由来の副作用がまだ不足している。
- pre-game action `0x02/0x03` の生成条件、tickジャンプ、VSConnect内部状態のどれかを再現できていない。
- UI bypass の場合も、`Game::loadLevel()` の前後に必要な scene transition / object manager / ready countdown 状態が不足している。


## 次にやること

1. 通常LANの `stageGroup=9` 到達前後と、SafeLoadLevelの `READY!` 固着状態の RAM dump / process list を比較し、actor生成に必要な scene/object 状態を特定する。
2. call trace は通常LANのWi-Fiタイミングを壊しやすいので、まず write trace と RAM dump 差分を優先する。
3. UI bypass ルートでは、`Game::loadLevel()` 直呼びではなく、通常LANが作る scene transition / CourseSelect / ready countdown 状態を再現する。
4. それでも UI bypass が詰まる場合は、pre-game中だけ通常LANの action/tick 遷移を模した synthetic packet mode に戻る。
5. gameplay到達後、入力同期netplayと乱数固定/検証へ戻る。


## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM patch化へ進む場合は、日本版 `A2DJ` 向けの patch 生成/適用手順を別途作る。
