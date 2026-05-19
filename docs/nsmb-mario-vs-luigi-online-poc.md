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
- `PacketBridgeForceTick` により、client 側 packet tick の停止は回避できる。
- pregame の `localPlayerID` を早く client=1 にすると通常 LAN とズレるため、`stageGroup=9` 前は host/client とも `localPlayerID=0` に寄せた。
- `ForceLoadGameSM step 3` から自然 update を回すと `VSConnect::updateLoadGameSM` は呼ばれるが、bridge では `vs_flags` が通常 LAN と同じタイミングで立たず、`CourseSelectFactory` / `StartLoadLevel` に自然到達しない。
- `SafeCourseSelectFactoryCall` は host 側では CourseSelect object を作れるが、client 側では同じ引数でも CourseSelect object が作られない。
- `SafeStartLoadCall` は `stageGroup=9` まで進められるが、player actors / Big Star actor は生成されないため成功扱いにしない。

## 現在のブロッカー

通常 LAN では `VSConnect::updateLoadGameSM` が `step 3 -> 5 -> 6 -> CourseSelectFactory -> StartLoadLevel -> Game::loadLevel` と進むが、NoLanMP + WAN adapter では `step 5` の timer だけが増え、`vs_flags` が `0x00010000` / `0x00030000` に進まない。

通常 LAN の call trace では、flags が立つ瞬間に net sequence 状態も `net_seq_ids=1`, `net_seq_cursors=2`, `net_seq_lengths=2` へ進んでいる。bridge 側は packet/action を合わせても flags が立たないため、次はこの sequence/state 条件を追う。

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
- `SafeStartLoadCall`, `SafeCourseSelectCall`, `SafeCourseSelectFactoryCall`
- 黒画面検出、通信切断画面検出、gameplay actor 検出

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
- `Game::vsMode`: `0x020850C4`
- local player ID: `0x020850BC`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelectFactory`: `0x020130A8`

## 直近の検証ログ

- 通常 LAN packet capture: `logs/nsmvl-baseline-packetcapture-gameplay-inputs-20260519-215929-attempt1`
- keys-only replay 成功: `logs/nsmvl-replay-keys-only-baseline-20260519-continue2`
- 通常 LAN level-load call trace: `logs/nsmvl-baseline-calltrace-level-load-20260519-220149-attempt1`
- pregame localPlayerID/action 補正後の step3 trace: `logs/nsmvl-bridge-step3-action3-pregame-idfix-20260520-continue`
- 通常 LAN flags 参考 trace: `logs/nsmvl-baseline-writetrace-vsflags-20260520-continue-attempt2`

## 次にやること

1. `VSConnect::updateLoadGameSM` の flags 更新条件を、通常 LAN と bridge の call trace / net sequence 状態差分から特定する。
2. bridge 側で不足している net sequence / receive 状態を、LocalMP ではなく packet adapter 側で補えるか試す。
3. 自然に `CourseSelectFactory` へ進むかを確認する。直接 `SafeCourseSelectFactoryCall` に頼る場合も、client 側で object が作られない原因を先に潰す。
4. `StartLoadLevel` / `Game::loadLevel` へ自然到達したら、player actors と Big Star actor の生成確認を必須条件に戻す。
5. actor 生成後に keys-only WAN packet bridge と結合し、入力が双方で反映されるか確認する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチ生成へ進む場合も、差分パッチとして管理し、元 ROM は含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
