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

最終形は `1 melonDS instance * 2PC`。現時点で一番筋が良い候補は、NSMBの接続処理を途中から奪うのではなく、melonDS既存の `MPInterface_LAN` を「最初からWAN transport」として使う方向。

`MPInterface_LAN` は LocalMP と同じ `SendCmd` / `SendReply` / `RecvHostPacket` API を ENet で運ぶ実装なので、NSMB側のローカル通信state-machineを最も壊しにくい。自動テストでは、遅延なしのLAN経路は `LanStartAttempts` 付きで `3600` frame まで成功し、`stageGroup=9`, `vsMode=1`, player actors, star actor を確認できる。

一方、WAN想定の単純な設定変更はまだ失敗している。

- `MELONDS_NSML_LAN_WAN_MODE` / reliable / 長いtimeout: 遅延なしでも接続探索段階で失敗する。
- `MELONDS_NSML_LAN_MP_STALE_MS=1000`: hostが「ルイージをさがしています」で止まる。
- `MELONDS_NSML_LAN_MP_SEND_DELAY_MS=5`: hostが探索中、clientが「melonDSマリオがあらわれました / たいせんしますか？」で止まる。

このため、単にtimeoutやstale windowを伸ばすだけでは不十分。NSMBの探索/承諾UIとMP frameの鮮度管理が強く結びついているため、WAN化するなら `LAN.cpp` のMPキュー処理を「古いframeをどう捨てるか」「CMD/reply/ackをどこまで待つか」「探索中の固定入力をどう待たせるか」まで含めて調整する必要がある。

NoLanMP + PacketBridge / ForceLoadGameSM / SafeCall 系は、接続途中から状態を作る実験としては有用だったが、自然なCourseSelect生成や実ステージactor生成には届いていない。今後の本筋からは外し、必要な解析補助としてだけ使う。


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

追加で確認したこと:

- `SafeUpdateLoadGameCall` は、host/client別の安定PC (`0200E700` / `02010810`) で呼ぶとabortは避けられるが、NoLan状態ではCourseSelect生成へ進まない。
- 正常LAN packet captureをreplayしても、NoLan + ForceLoadGameSM ではLoadGameSMが自然遷移しない。接続初期化の副作用が足りない。
- `CourseSelectFactory` 直接呼び出しはhostでCourseSelect生成まで進む場合があるが、clientでは同じ引数でも生成されない。呼び出し文脈依存が強い。
- `Game::loadLevel()` をSafeCallで直接呼ぶと `stageGroup=9` と `READY!` 画面までは到達するが、player actors / star actor が生成されない。つまり「面IDだけを切り替える」だけでは試合開始状態として不十分。
- `run-nsmb-mvl-lan-route-smoke.ps1` に接続ダイアログ検出を追加した。「通信が切断されました」だけでなく、「相手がいなくなりました」「相手を探しています」「たいせんしますか？」で止まる画面も失敗扱いにする。


## 現在のブロッカー

`MPInterface_LAN` を最初から使うルートは正常LANでは動くが、WAN想定の遅延・長い待ち・長いstale windowを入れると接続探索/承諾段階で止まる。

特に、host/clientの固定入力スクリプトは相手発見タイミングに追従できない。実装面でも、LAN MPの `RXQueue` stale処理、CMD/reply/ack待ち、ENet reliable/unsequencedの使い分けがNSMBの期待するローカル通信テンポから外れると切断扱いになる。


## 次にやること

1. `MPInterface_LAN` ルートを本筋にする。NoLan後付けForceではなく、最初からLAN/WAN transportでNSMBの接続処理を走らせる。
2. 固定入力スクリプトを改善し、clientの「たいせんしますか？」表示を待ってからAを押す、hostの探索中を待つ、という画面/状態待ち型に近づける。
3. `LAN.cpp` のMP挙動を調整する。まずは `RecvHostPacket` / `RecvReplies` のtimeout、stale破棄、CMD/reply/ack別の待ちをログで分類し、5ms遅延で止まる理由を特定する。
4. 5ms送信遅延で `stageGroup=9` 到達を目標にする。そこを越えたら10ms、20msと上げる。
5. gameplay到達後、入力同期netplayと乱数固定/検証へ戻る。


## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM patch化へ進む場合は、日本版 `A2DJ` 向けの patch 生成/適用手順を別途作る。
