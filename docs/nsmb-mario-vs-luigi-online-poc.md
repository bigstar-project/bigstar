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
- direct-change と stage-start field forcing により、`NoLanMP` でも VSConnect の stage-start state から scene 5 `CourseSelect` までは到達できる。

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

`NoLanMP + PacketBridge from start` は、自然 LocalMP と同じ MvL stage load へまだ入れていない。

直近の診断では、VSConnect stage-start state から scene 5 `CourseSelect` までは進めたが、自然 LAN のように `sceneNext=0x0F` を立てる CourseSelect 更新処理が走らない。`DirectMvlBoot` で `Game::loadLevel` を強制すると `stageGroup=9` は立つが、`pc=020448C0` 付近で ARM9 data abort し、scene 5 から進まない。

`020448C0` は Code Reference 上では `MTX::rotateX` 付近なので、乱数や packet そのものではなく、ステージ/メニュー遷移を不自然に飛ばした結果、初期化されていない object/resource へ描画・行列処理が入っている可能性が高い。

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
- `Scene::tryChangeScene`: `0x020131DC`
- `MTX::rotateX` 付近: `0x020448C0`

## 次にやること

1. 自然 LAN の scene 5 `CourseSelect` 更新から `sceneNext=0x0F` / `Game::loadLevel` へ進む caller と条件を call trace / write trace で特定する。
2. `NoLanMP` 側で CourseSelect scene 5 へ入ったあと、自然 caller の前提フィールドだけを再現して stage load へ進めるか確認する。
3. `020448C0` abort 時の `r0=null` がどの object/resource 由来か、自然 LAN と forced route のレジスタ・周辺メモリ差分で切り分ける。
4. MvL stage 到達後、gameplay packet の `tick/keys/action` 一致と「通信が切断されました」画面の非発生を自動判定に入れる。
5. stage 内同期が確認できたら、WAN adapter の待ち制御を NSMB の input tick 要求に合わせて調整する。

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使う。
- ROM パッチへ進む場合も、差分パッチとして管理し、元 ROM はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
