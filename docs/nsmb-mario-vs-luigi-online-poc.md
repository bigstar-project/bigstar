# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに遊べる形へ持っていく。

現方針は、`LocalMP` や `2インスタンス * 2プロセス` を最終形にしない。NSMB 側の MvL 接続 state-machine と gameplay packet を解析し、ローカル無線の代わりに WAN adapter から必要な packet/state を渡す。

## 採用しない方針

- `2 instances * 2PC` を最終形にする方針: LocalMP 間でも actor/object 差分が残り、重く、WAN 検証のフィードバックループが遅い。
- savestate 同期を最終形にする方針: 通信状態と actor lifecycle が噛み合わず、切断やズレが残る。
- 試合開始後に LocalMP から WAN へ切り替える方針: 切り替え時の停止で host/client state が崩れやすい。
- `MPInterface_LAN` を単純に WAN 化する方針: LAN 前提の遅延で接続開始段階が崩れる。

## 現在の到達点

- 通常 LocalMP LAN ルートでは `stageGroup=9`, `vsMode=1`, player actors, Big Star actor まで到達できる。
- NSMB Central と実測から、MvL gameplay packet は主に `tick`, `keys`, `action` を持つ入力同期パケットであることを確認済み。
- `NoLanMP + PacketBridge from start` で packet 送受信自体は可能。
- PacketBridge の `WireNSMLPacket` は ENet reliable packet に変更済み。localhost でも tick 欠落が出ていたため、ここは reliable が妥当。
- `PacketBridgeWait` は、まだ一度も remote packet を受け取っていない段階では待たないよう修正済み。
- `NoLanMP + PacketBridgeSubMenuSchedule` を追加し、自然 LAN で観測した VSConnect サブメニュー列をスクリプトから注入できるようにした。
- サブメニューの direct-change も追加した。任意PCから create 関数を呼ぶと data abort するため、デフォルトは current create/update/render ポインタの差し替えだけにしている。
- direct-change と safe scene call により、`NoLanMP` でも scene 5 `CourseSelect` object 作成、`VSConnect::startLoadLevel`、`Game::stageGroup=9` / `vsMode=1` までは再現できる。

## 最新の重要な発見

`A2DJ` の VSConnect サブメニューアドレスは、US/Code Reference の値をそのまま使えない。自然 LocalMP の trace から、少なくとも以下を確認した。

- `0x02156624`: create `0x021520A0`, update `0x02151E94`, render `0x02151E54`
- `0x02156640`: create `0x02151D74`, update `0x021519F0`, render `0x021519B0`
- `0x02156678`: create `0x021515B4`, update `0x021512B8`, render `0x0215125C`
- `0x02156694`: create `0x02151950`, update `0x021517A4`, render `0x02151764`

自然遷移のサブメニュー列:

- host: `0x02156624 -> 0x02156640 -> 0x02156678`
- client: `0x02156624 -> 0x02156640 -> 0x02156694 -> 0x02156678`

`0x02156624` へ直接 `scheduleSubMenuChange(..., 0x1E, 1)` しても、自然遷移と同じにはならない。任意PCからの create 呼び出しも data abort する。現在は create を呼ばずに current sub-menu pointer を差し替え、必要な前提フィールドを最小限だけ再現する方向で追っている。

## 現在のブロッカー

`NoLanMP + PacketBridge from start` は、自然 LocalMP と同じ MvL stage scene / gameplay actors へまだ入れていない。

直近の診断で分かったこと:

- `SafeCourseSelectFactoryCall` を `Scene::tryChangeScene` entry `0x0201314C` 上で実行すると、自然 LAN と同じ `CourseSelect` object base `0x021BE9D8` を作れる。
- `SafeStartLoadCall` を `0x02064F80` 上で実行し、`VSConnect+0x144/0x148/0x154` と `CourseSelect+0x078/0x07C` を自然値へ戻すと、`stageGroup=9`, `vsMode=1`, `currentScene=0x0F` までは安定する。
- `SafeTryChangeSceneCall` で `nextScene=3` を処理させると `currentScene=3` までは進む。
- ただし、stage scene object の create/update が自然遷移と同じ形でリンクされておらず、`playerActor0/1` と Big Star actor はまだ出ない。画面も blank / 「しばらくおまちください」判定になる。

つまり現在の主ブロッカーは「scene ID を 3 にすること」ではなく、自然の scene switch が行っている旧 CourseSelect scene の破棄、新 Stage scene object の登録、StageScene `onCreate` 実行を再現できていないこと。

## 主な実装済みテストフック

- 入力スクリプト再生
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / replay / bridge trace
- Big Star actor probe
- `PacketBridgeForceTick`
- `ForceNetReady`, `ForceLoadGameSM`, `ForceTransferResult`
- `SafeStartLoadCall`, `SafeCourseSelectCall`, `SafeCourseSelectFactoryCall`
- `SafeStageSceneFactoryCall`
- `SafeTryChangeSceneCall`
- `SafeCallProbe`, `SafeCallProbeOnly`
- `SafeUpdateLoadGameCall`, `SafeCreateLoadGameSM`
- `PacketBridgeForceStagePacketWords`
- `PacketBridgeSubMenuSchedule`, `PacketBridgeSubMenuDirect`
- `PacketBridgeForceStageStartSMFields`
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
- `VSConnect::scheduleSubMenuChange`: `0x021528A0`
- `VSConnect loadGameSM SubMenu`: `0x02156624`
- `VSConnect post-load SubMenu`: `0x02156640`
- `VSConnect stage-start SubMenu`: `0x02156678`
- `VSConnect client-confirm SubMenu`: `0x02156694`
- `loadMvsLFilesThread`: `0x02152E18`
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `Scene::tryChangeScene` entry: `0x0201314C`
- `Scene::tryChangeScene` transition block: `0x020131DC`
- `MTX::rotateX` 付近: `0x020448C0`

## 次にやること

1. 自然 LAN の `0x020131DC` / `0x020131FC` 周辺を RAM dump から追い、scene switch 時にどの object pointer / global が更新されるか特定する。
2. `Object::spawnScene` 相当の standalone 呼び出しではなく、自然 scene switch と同じ経路で Stage scene object を登録できるか確認する。
3. `NoLanMP` ルートで `playerActor0/1` と Big Star actor が出るまで、blank / connection dialog / disconnect 判定を必ず有効にして検証する。
4. MvL stage 到達後、gameplay packet の `tick/keys/action` 一致を確認する。
5. stage 内同期が確認できたら、WAN adapter の待ち制御を NSMB の input tick 要求に合わせて調整する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチへ進む場合も、差分パッチとして管理し、元 ROM はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
