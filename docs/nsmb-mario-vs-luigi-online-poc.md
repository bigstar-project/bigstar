# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に `1 melonDS instance * 2PC` で WAN 越しに遊べる形にする。

現在の本命方針は、DS LocalMP 全体を WAN に流すのではなく、NSMB の MvL が使う接続 state-machine と入力 packet を解析し、LocalMP の代わりに WAN adapter から必要な packet/state を渡す方式。

## 採用しない方針

- `2 instances * 2PC` を最終形にする方針: LocalMP 間の actor/object 差分が残り、重く、検証も遅い。
- savestate 同期を最終形にする方針: 通信状態と actor lifecycle が揃わず、切断やズレが残る。
- 試合途中で LocalMP から WAN に切り替える方針: 切り替え時の停止で host/client state が壊れやすい。
- `MPInterface_LAN` を単純に WAN 化する方針: LAN 前提の遅延で接続/開始段階が崩れる。

## 現在の到達点

- 通常 LocalMP LAN ルートでは `stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- NSMB Central の情報と実測から、MvL gameplay packet は主に `tick`, `keys`, `action` を中心に構成されることを確認済み。
- NoLanMP + PacketBridge from start で host/client 双方の packet 送受信自体は可能。
- `VSConnect::updateLoadGameSM` 周辺の hotpatch と trace により、host 側は WAN adapter 経路から `Game::loadLevel` まで自然到達できる。
- `ForceLoadGameSMBaselineFlags` により、通常 LAN 相当の `step`, `timer`, `vs_flags`, net sequence fields を再現できる。
- 2026-05-20 時点の最新テストでは、host 側は `Game::loadLevel` 後に player actors と Big Star actor が生成される。
- client 側は `stageGroup=9` へ入れる場合があるが、スクリーンショットでは「マリオをさがしています」の待機画面で止まり、`netPacketTick=1` のまま actor が生成されない。

## 現在のブロッカー

client 側が MvL 試合本体へ入れていない。

具体的には、host は gameplay actor / Big Star actor まで生成される一方、client は `Game::loadLevel` を直接呼んでも player actors / Big Star actor が生成されず、待機画面に残る。`PacketBridgeForceTick` を入れても client の観測上の packet tick は進んでいない。

## 直近の検証結果

- `logs/nsmvl-safecreate-pregame-step1-baselineflags-20260520`
  - host は `createLoadGameSM`, `loadMvsLFilesThread`, `Game::loadLevel` に到達。
  - client は `LoadGameSM step=7` 相当まで補正できるが、自然な level load には入らない。
- `logs/nsmvl-client-safeupdate-no-safecreate-20260520`
  - client だけ `updateLoadGameSM` を safe-call すると step は進むが、呼び続けると data abort になる。
- `logs/nsmvl-client-directload-after-host-loadsm-20260520`
  - host は actor / Big Star actor 生成まで到達。
  - client は `stageGroup=9`, `localPlayerID=1` になるが、actor は生成されず「マリオをさがしています」で停止。
- `logs/nsmvl-client-directload-forcetick-20260520`
  - `PacketBridgeForceTick` を足しても client の `netPacketTick` は `0x1` のまま。
- `logs/nsmvl-loadsm-step7-preload-20260520`
  - `loadMvsLFilesThread` の強制preloadは発火するが、target step 7 + preload だけでは host/client とも開始に進まない。

## 実装済みの主なテストフック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / packet replay / packet bridge trace
- Big Star actor ID `0x00D2` 周辺 probe
- `PacketBridgeForceTick`
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- role scoped `ForceNetReady`
- `ForceLoadGameSMBaselineFlags`
- `SafeStartLoadCall`, `SafeCourseSelectCall`, `SafeCourseSelectFactoryCall`
- `SafeUpdateLoadGameCall`, `SafeCreateLoadGameSM`
- role scoped `DirectMvlBoot`
- 黒画面検出、切断待機画面検出、gameplay actor 検証

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- lower MP status probe: `0x0204619C`
- lower MP getPacket: `0x02046480`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelectFactory`: `0x020130A8`

## 次にやること

1. client の「マリオをさがしています」待機画面を抜ける条件を特定する。
2. client 側で `netPacketTick=1` のまま止まる理由を、packet bridge の受信/適用経路から追う。
3. 通常 LAN client と WAN adapter client の `Net` 管理領域差分を、`stageGroup=9` 直後の frame で比較する。
4. client に必要な packet/action/tick/peer-ready 状態を最小補正し、actor / Big Star actor 生成まで到達させる。
5. host/client 両方で actor が出たら、切断画面ではないことをスクリーンショットで確認し、入力同期の検証へ戻す。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチへ進む場合も差分パッチとして管理し、元 ROM は含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
