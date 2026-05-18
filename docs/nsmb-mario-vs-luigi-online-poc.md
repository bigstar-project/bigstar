# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現時点では、`LocalMP 2インスタンス * 2プロセス`、savestate同期、途中DropMP、途中WAN切り替えは本筋から外している。理由は、スター位置やオブジェクト状態の不一致、切断判定、fps低下が重く、最終形に近くないため。

## 現在の方針

優先方針は「MvsLの接続/ロビー段階からWAN adapterを使う」こと。

NSMB側にはローカル通信時の入力/packet同期処理があるため、それをできるだけ使い、melonDS側は LocalMP の代わりに WAN 経由のMP packetを渡す adapter として振る舞う。必要に応じてNSMB側の状態を解析し、最小限のROM/メモリ側補助に寄せる。

## 採用しない方針

- savestate方式: 表示座標だけでなく stage/object 全体が一致しない。対戦基盤として弱い。
- DropMP/途中WAN切り替え: 切替瞬間にNSMB側の接続状態やobject状態が壊れやすい。
- `2インスタンス * 2PC` の状態同期: 重く、最終形から遠い。検証補助としてのみ使う。
- 死亡演出やスクリーンショットだけで成功判定すること: host/clientで表示が一致しないケースがあるため、game-state trace / packet trace / RAM dumpを優先する。

## 実装済みの検証基盤

- 自動入力スクリプト再生
- screenshot / framebuffer dump
- MainRAM dump
- game-state trace
- call trace
- packet trace / packet bridge trace
- `Net::getRandom()` 固定検証
- Big Star actor ID `0x00D2` 周辺probe
- `MPInterface_LAN` の WAN 検証オプション
- LAN MP 受信/送信 delay、stale、reliable、trace
- Wi-Fi MP channel mismatch をWAN検証用に受け入れる `MELONDS_NSML_WIFI_MP_ACCEPT_ANY_CHANNEL`
- `run-nsmb-mvl-lan-route-smoke.ps1` のLAN/NoLAN/PacketBridge検証オプション
- VSConnect load-game state machine 補助フック
- CourseSelect生成条件を追う実験フック

## 重要アドレス

- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::renderLoadGameSM`: `0x0215125C`
- VSConnect完了/開始系: `0x0214E0C0`
- CourseSelect生成関連: `0x0214F830`
- scene request: `0x020130A8`
- scene request apply: `0x02007ACC`
- scene transition: `0x02011CE8`
- object manager create: `0x0204BF8C`
- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- packet tick/key buffer: `0x02087F00`
- net state base: `0x02087E00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`

## 現在の検証結果

- 通常LANルートは `4200` フレームまで通る。
- `LanMPAcceptAnyChannel` だけでは通常LANは壊れない。
- raw LAN + 人工遅延 `LanMPSendDelayMs=20` では、hostは `vsMode=1` まで進むが、clientの `netPacketTick` が `0xb1` 前後で止まり、CourseSelect生成と `stageGroup=9` に進まない。
- `LanMPSendDelayMs=5` でも接続段階で崩れる。raw MP frameを単純にWAN遅延させる方向はかなり弱い。
- host側だけ20ms、client側だけ20ms、両方向20msのいずれでも接続段階で崩れる。片方向だけの返信遅延問題ではなく、DS MP通信のスキャン/返信窓全体が遅延に弱い。
- `LanMPStaleMs=1000` でも改善しない。単純なstale破棄だけが原因ではない。
- `-LanWanMode` の単純適用はさらに重く、clientが接続段階で停滞しやすい。
- LAN MP traceでは、hostはMP frameを送り続け、clientもhost frameを受信している。ただしclientからのMP送信/返信はほぼ出ず、NSMB接続状態は `netState1C=3/4` 付近で止まる。
- `VSConnect::updateLoadGameSM` をclient側で明示的に回すと `word144=7` までは進むが、clientの `netPacketTick` が止まったままなので CourseSelect / stage 遷移はまだ消費されない。
- `updateLoadGameSM` の `word144=7` 直書きは誤り。case6内の `0x020130A8 -> 0x02011CE8` を含む処理を通す必要がある。
- `object manager create` の直接呼び出しはARM9 abortしやすく、安定ルートではない。
- `scene request apply` / `scene transition` の直接呼び出しは実行できるが、通信tickが止まっている状態ではCourseSelect生成まで到達しない。
- 通常LANのpre-game packet captureを取れるようにし、成功ルートではhostが action `0x01 -> 0x02 -> 0x03`、clientが action `0x00 -> 0x02 -> 0x03` へ進むことを確認した。
- PacketBridgeOnlyではhostが action `0x01`、clientが action `0x00` のまま止まり、`netState1C=3/4` から進まない。
- 成功LANのpacket列をNoLanMP/PacketBridge環境へCSV replayしても、`Net::getPacket*` の戻り値だけではCourseSelectへ進まない。replay時にLocalMP packet slot (`0x0208AE50`, `0x0208B040 + player * 0x3E`) へも書くフックを追加したが、それでも `vsConnectWord078/07C` が通常LANのように `3` へ進まない。
- `transferPacket` の戻り値は通常LAN成功時も主に `0`。以前の `0x8` 強制は接続完了条件としては不適切。`0` 強制でも改善しない。
- `ForceNetReady` + `ForceLoadGameSM` は `vsMode=1` / `netState1C=6` までは作れるが、host側でARM9 abortが出やすく、CourseSelect生成までは未到達。
- ARM9 write traceを追加し、通常LANでは `0x02150DAC/0x02150DB0` が `VSConnect word078/07C` を `3` へ上げ、その後 `word144` が `1 -> 7` へ進むことを確認した。
- `DirectBoot + startLoadLevel` はhost/clientとも `stageGroup=9` へ到達するが、player actor / star actor が生成されず、clientの `localPlayerID` も期待通りではない。これは「試合開始成功」ではなく、ロード状態へ無理に入れただけ。
- `DirectBoot + Game::loadLevel` は現行の状態検査を通る場合があるが、ARM9 abortが出てplayer actorが生成されず、スクリーンショットも黒画面に近い。成功判定を `stageGroup/vsMode/localPlayerID` だけに依存してはいけない。
- smoke testのgame-state成功条件に `playerActor0Found`, `playerActor1Found`, `vsStarActorFound` を追加した。通常LANはこの厳しい判定でも通り、`DirectBoot + Game::loadLevel` の黒画面/未ロード状態は期待通り失敗する。

## 現在のブロッカー

WAN adapter開始時に、NSMBのpacket値だけでなく、接続/ロビー state machine が期待しているLocalMP由来の状態遷移も再現する必要がある。現状は packet replay / slot write を入れても `VSConnect` が通常LANと同じ完了状態に進まず、最終的なCourseSelect生成と `stageGroup=9` への遷移が消費されない。

次に見るべき本筋は、通常LANで `vsConnectWord078/07C` が `1 -> 3` へ変わる条件を特定し、それをWAN adapter開始時にも自然に満たすこと。無理なstate直書きはabortや不整合を生みやすいので、関数境界と必要な副作用を絞る。

## 次にやること

1. 通常LANで `VSConnect` が `word078/07C=3` へ進む直前の関数呼び出しとメモリ書き込みをtraceする。
2. PacketBridgeOnlyで同じ関数/書き込みが欠けている箇所を比較する。
3. 欠けている副作用がLocalMP slot由来ならslot emulationを拡張し、NSMB関数由来なら該当関数を安全なタイミングで呼ぶ。
4. `ForceLoadGameSMRunUpdate` のARM9 abort原因を潰す。直接trampolineを繰り返し呼ぶ方式は不安定なので、必要なら一回だけの関数呼び出し/return先管理に変える。
5. スクリーンショット側のblackout検出も、`SkipDisconnectScreenshotCheck` に頼りすぎない形へ分離する。
6. `netState1C=6`, `vsMode=1`, `VSConnect word144=7` の状態からCourseSelect生成、`stageGroup=9`、試合開始へ進むかを再検証する。
7. 試合開始後にスター/8コインアイテム等のランダム要素一致を確認する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM patch化へ進む場合は、日本版 `A2DJ` 向けのpatch生成/適用手順を別途作る。
