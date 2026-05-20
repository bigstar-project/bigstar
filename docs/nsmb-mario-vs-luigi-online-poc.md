# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に `1 melonDS instance * 2PC` で WAN 越しに遊べる形にする。

現在の本命方針は、DS LocalMP 全体を WAN に流すのではなく、NSMB の MvL が使う接続 state-machine と gameplay packet を解析し、LocalMP の代わりに WAN adapter から必要な packet/state を渡す方式。

## 採用しない方針

- `2 instances * 2PC` を最終形にする方針: LocalMP 間の actor/object 差分が残り、重く、検証も遅い。
- savestate 同期を最終形にする方針: 通信状態と actor lifecycle が揃わず、切断やズレが残る。
- 試合途中で LocalMP から WAN に切り替える方針: 切り替え時の停止で host/client state が壊れやすい。
- `MPInterface_LAN` を単純に WAN 化する方針: LAN 前提の遅延で接続/開始段階が崩れる。

## 現在の到達点

- 通常 LocalMP LAN ルートでは `stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- NSMB Central の情報と実測から、MvL gameplay packet は主に `tick`, `keys`, `action` を中心に構成されることを確認済み。
- NoLanMP + PacketBridge from start で host/client 双方の packet 送受信自体は可能。
- `SafeStartLoadCall` を `0206B83C` から呼ぶと host/client とも `VSStageIntro` scene `0x0F` の `READY!` 画面まで到達する。
- scene request 方式で `scenePrevious=0x5`, `sceneNext=0x3` を立てると、host/client とも stage scene `0x03` までは進む。
- `PacketBridgeForceStagePacketWords` を追加し、自然 LAN と違っていた stage packet words を host/client とも `0xFFFF0003` 系に維持できるようにした。
- Data abort ログを拡張し、fault address / CPSR / 命令 / r0-r12 を出せるようにした。

## 現在のブロッカー

stage scene `0x03` 生成後に gameplay actor が生成されず、ARM9 data abort で止まる。

現在の abort は以下の形:

- `pc=0205545C`
- `lr=02044388`
- `instr=E510000C`
- `fault=FFFFFFF4`
- `r0=00000000`

Code Reference と call trace から、これは乱数 seed そのものではなく、`Random::Random` 周辺から heap backend に入り、stage 生成中に参照する resource / heap block の管理ヘッダが自然 LAN と一致していない問題と見ている。

## 直近の重要な検証

- `logs/nsmvl-force-stage-packet-words-postrefresh-20260520`
  - host/client とも `0x02087F04`, `0x0208B044`, `0x0208B048..54`, `0x02186A88` を自然 LAN 相当に揃えられた。
  - ただし actor は出ず、同じ data abort が残った。
- `logs/nsmvl-abort-calltrace-20260520`
  - forced route の abort 直前に `Random::Random` が `r1=0228E720` を参照していることを確認。
- `logs/nsmvl-natural-calltrace-random-20260520`
  - 自然 LAN では対応する初回参照が `r1=0228F080` になり、その後 actor 生成まで進む。
- `logs/nsmvl-forced-heapheader-dump-20260520` / `logs/nsmvl-natural-heapheader-dump2-20260520`
  - forced route の resource block は自然 LAN と heap allocation の位置・管理ヘッダが異なる。
  - そのため stage scene 生成時の `sizeOf` / heap backend 相当の参照で null/不正ヘッダへ落ちる可能性が高い。
- `logs/nsmvl-natural-loadfiles-trace-20260520`
  - 自然 LAN では `loadMvsLFilesThread(0x0208A478, 1, 'MSEG', 0)` が host `1887` / client `1896` 付近で一度走る。
  - その後 `FS::Cache::loadFile(0x02085470, 0x80, ...)` が stage 生成前に呼ばれる。
- `logs/nsmvl-natural-loadsm-sequence-20260520`
  - 自然 LAN の `VSConnect::scheduleSubMenuChange` では load-game 系候補として `r1=02156624`, `r2=0x1E`, `r3=1` が使われる。
  - 旧仮説の `0x02156678` は不採用に更新。

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
- `SafeStageSceneFactoryCall`
- `SafeCallProbe`, `SafeCallProbeOnly`, CPSR mode フィルタ
- `SafeTryChangeSceneTarget`, `SafeTryChangeSceneSetOnly`
- `SafeUpdateLoadGameCall`, `SafeCreateLoadGameSM`
- `PacketBridgeForceStagePacketWords`
- Data abort register/fault-address logging

## 重要アドレス

- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- `Net::Core::transferPacket`: `0x0200F98C`
- packet tick/key/action buffer: `0x02087F00`
- net state base: `0x02087E00`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::scheduleSubMenuChange`: `0x021528A0`
- MvL load-game submenu candidate: `0x02156624`
- `loadMvsLFilesThread`: `0x02152E18`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `Scene::tryChangeScene`: `0x020131DC`

## 次にやること

1. `VSConnect::scheduleSubMenuChange(0x02156624, 0x1E, 1)` を外部 trampoline から呼んでも state が進まない理由を特定する。自然呼び出し時の caller / CPSR / SP / object state との差分を見る。
2. `loadMvsLFilesThread` を thread entry として自然に起動する上位処理を特定する。直呼びは `prefetch abort(00000004)` になるため避ける。
3. forced route で `Random::Random` に渡る resource pointer / heap header を自然 LAN と同じ形にする。
4. actor 生成後に gameplay packet の `tick`, `keys`, `action` 同期検証へ戻る。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチへ進む場合も差分パッチとして管理し、元 ROM は含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
