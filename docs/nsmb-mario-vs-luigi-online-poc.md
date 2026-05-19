# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に WAN 越しの `1 melonDS instance * 2PC` で遊べる形にする。

現在の本筋は、DS LocalMP 全体を WAN に流すのではなく、NSMB の MvL が使う入力中心 packet と接続 state-machine を解析し、LocalMP の代わりに WAN adapter から必要な packet/state を渡す方式。

## 採用しない方針

- `2 instances * 2PC` を最終形にする方針: LocalMP 間の actor/object 差分が残り、重く、検証速度も悪い。
- savestate 同期を最終形にする方針: 表示や座標を合わせても通信状態と actor lifecycle が一致しない。
- DropMP / 試合途中 WAN 切替: 切替時の停止で host/client state が壊れやすい。
- `MPInterface_LAN` を単純に WAN 化する方針: LAN なら動くが、WAN 相当の遅延で接続承認段階が崩れる。
- UI 操作なしの単純 `loadLevel` / `StartLoadLevel` 呼び出し: `stageGroup=9` までは作れるが、player actor / Big Star actor が生成されない。

## 現在の到達点

- 通常 LocalMP LAN ルートは、`stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- 通常 LAN capture では MvL gameplay packet は `tick`, `keys`, `action` を中心に構成される。keys-only replay は 4200 frame 通過済みで、入力置換自体は有効。
- NoLanMP + PacketBridge from start では、host/client 双方で packet 送受信はできる。
- `PacketBridgeForceTick` に role 別 tick base を追加し、client 側 packet tick の停止を回避できるようにした。
- pregame の `localPlayerID` を早く client=1 にすると通常 LAN とズレるため、`stageGroup=9` 前は host/client とも `localPlayerID=0` に寄せた。
- `ForceLoadGameSMBaselineFlags` を追加し、通常 LAN 相当の `step 5/6`, `vs_flags=0x10000/0x30000`, net sequence fields を再現できるようにした。
- RAM 比較で、`vsConnect` 本体は通常 LAN と WAN adapter で一致する一方、Net 管理領域の `0x02087F30` と `0x0208806C` が不足していることを確認し、`ForceNetReady` で補うようにした。
- `VSConnect::updateLoadGameSM` の PC 範囲トレースにより、通常 LAN は `0x021514E4` 直前の Net readiness result が `r0=1`、WAN adapter は `r0=0` になって `CourseSelectFactory` をスキップすることを特定した。
- pregame WAN adapter では `0x021514E4` の readiness result を満たす hotpatch により、host 側は自然に `CourseSelectFactory(0x020130A8)` を呼び、CourseSelect object 生成まで到達できる。
- CourseSelect state byte (`courseSelect+0x64`) の write trace により、通常 LAN は `1 -> 0x0B -> 2 -> 3 ... -> 9` と進むが、bridge は `1` で止まることを確認した。
- CourseSelect state `1` の PC 範囲トレースにより、通常 LAN は `0x0214ED18` 直前の readiness result が `r0=1`、bridge は `r0=0` になって次 state に進まないことを特定した。
- `0x0214ED18` の readiness result を満たす hotpatch により、host 側は `StartLoadLevel` / `Game::loadLevel` まで自然到達し、`stageGroup=9` へ進むようになった。

## 現在のブロッカー

host は `stageGroup=9` まで進むが、player actors / Big Star actor はまだ生成されず、host 側で `ARM9 data abort (0205545C)` が出る。

client は `VSConnect` の `step 6/7` 状態を書けていても `updateLoadGameSM` が自然に呼ばれず、CourseSelect 系 state も作れない。`SafeCourseSelectFactoryCall` を 1900f / 1878f で入れても client 側は CourseSelect object として検出されず、`stageGroup=9` へ進まない。

client-only `RunUpdate` 注入は `ARM9 data abort` になったため不採用。`SafeStartLoadCall` は host/client とも `stageGroup=9` へ進めるが、host は data abort と黒画面に入り、player actors / Big Star actor が生成されないため成功扱いにしない。

次は、client 側で通常 LAN と同等の CourseSelect / load state が作られない理由と、host の `Game::loadLevel` 後に actor 生成へ進まない理由を追う。

## 実装済みの主なフック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / packet replay / packet bridge trace
- Big Star actor ID `0x00D2` 周辺 probe
- `PacketBridgeForceTick`
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- `ForceLoadGameSMBaselineFlags`
- `SafeStartLoadCall`, `SafeCourseSelectCall`, `SafeCourseSelectFactoryCall`
- 黒画面検査、通信切断画面検査、gameplay actor 検査

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
- missing Net readiness words observed in bridge: `0x02087F30`, `0x0208806C`
- Net readiness branch in `VSConnect::updateLoadGameSM`: `0x021514E4`
- `Game::stageGroup`: `0x02085058`
- `Game::localPlayerID`: `0x020850BC`
- `Game::vsMode`: `0x020850C4`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelectFactory`: `0x020130A8`

## 直近の検証ログ

- 通常 LAN level-load call trace: `logs/nsmvl-baseline-find-loadsm-20260520-continue-attempt1`
- 通常 LAN PC 範囲 trace: `logs/nsmvl-baseline-pcrange-loadsm-20260520-continue-attempt1`
- WAN adapter PC 範囲 trace: `logs/nsmvl-bridge-pcrange-loadsm-20260520-continue`
- Net readiness result hotpatch で host CourseSelect 到達: `logs/nsmvl-bridge-force-netready-result-20260520-continue`
- `SafeStartLoadCall` 検証、stageGroup=9 だが actor 未生成: `logs/nsmvl-bridge-force-startload-after-course-20260520-continue`
- CourseSelect state byte write trace: `logs/nsmvl-baseline-course064-writetrace-20260520-continue`, `logs/nsmvl-bridge-course064-writetrace-20260520-continue`
- CourseSelect state1 readiness trace: `logs/nsmvl-baseline-course-state1-pcrange-20260520-continue`, `logs/nsmvl-bridge-course-state1-pcrange-20260520-continue`
- CourseSelect state1 ready hotpatch で host が `Game::loadLevel` 到達: `logs/nsmvl-bridge-course-state1-ready-20260520-continue`

## 次にやること

1. client 側で通常 LAN と同等の CourseSelect / load state が作られない原因を、object action/state と callback pointer 周辺の通常 LAN 差分から特定する。
2. host の `Game::loadLevel` 後に player actors / Big Star actor が生成されず data abort する原因を、通常 LAN の load 後 RAM / call trace と比較する。
3. `SafeStartLoadCall` のような強制ロードではなく、NSMB の接続 state-machine が期待する readiness / packet / object state を adapter 側で満たす。
4. host/client 両方で player actors と Big Star actor が生成されたら、keys-only WAN packet bridge と結合し、入力が双方で反映されるか確認する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチ生成へ進む場合も、差分パッチとして管理し、元 ROM は含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
