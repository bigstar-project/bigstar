# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

現在の主方針は、melonDS の LocalMP 2インスタンス同期を最終形にしない。NSMB側が元々持っているMvL接続/同期/state machineを活かし、ローカル無線の代わりにWAN adapter / PacketBridgeから必要なpacketを渡す。

## 現在の到達点

- `NoLanMP + PacketBridge from start` で、起動直後からローカル無線なしのPacketBridge経路を使う検証ルートを構築済み。
- `PacketBridgeSubMenuSchedule` / `PacketBridgeSubMenuDirect` で、自然LANで観測したVSConnectサブメニュー遷移を注入できる。
- `SafeCourseSelectFactoryCall` により、`CourseSelect` object生成までは再現できる。
- `VSConnect::startLoadLevel` と `Game::loadLevel(scene=0x0F, stageGroup=9, vsMode=1, ggid=0x42)` まではNoLanMP経路で再現できる。
- `CourseSelect` update内の自然なstage requestを特定し、`0x0214C3D4 -> 0x020130A8(scene=3, settings=00B5FF00)` でStage sceneへ遷移しようとするところまで再現できる。

## 直近で分かったこと

- `0x0214C3D4` はVSConnectのstage-start updateではなく、overlay 52内のCourseSelect updateが `Scene::tryChangeScene` 相当を呼ぶ場所だった。
- そのため、外から無理にscene 3へ飛ばすより、CourseSelect updateのready判定を自然タイミングで通す方が筋が良い。
- `loadMvsLFilesThread` の日本版A2DJアドレスは `0x02152E04`。以前使っていた `0x02152E18` は関数入口ではなく、完了フラグ `0x0208A478=1` を書く途中命令だった。
- MvLファイルロード本体は直接呼ぶものではなく、StageStartSMが `02004BFC(entry=0x02152E04, stackSize=0x1000)` でロード用スレッドを作る。
- ただし、PacketBridge側から単純にロードスレッドを早期起動しても `02009B94` 付近のcache loadで実質停止する。ロード前提状態または呼び出しタイミングが自然StageStartSMとまだ一致していない。

## 現在の主ブロッカー

NoLanMP + PacketBridge経路でStage sceneへ入る直前のリソース準備が自然LANと一致していない。

具体的には、ファイルロードを省略するとStage初期化中に `0205545C` 付近でdata abortする。逆にロード関数/ロードスレッドを単純に呼ぶと、`02009B94` 付近で非常に長く止まり、VSConnect scene 6から先へ進まない。

## 次にやること

1. 自然StageStartSMのstep遷移を再現する。
   - `createStageStartSM=0x021515B4`
   - `updateStageStartSM=0x021512B8`
   - step 2の `02004BFC(entry=0x02152E04)` を、自然の前提状態で踏ませる。
2. 早期ロードスレッドを単独で呼ぶのではなく、StageStartSMの`0x140/0x144/0x148/0x154`などを自然値に寄せて、NSMB自身のupdateにロード開始させる。
3. Stage scene遷移後に `playerActor0/1` と Big Star actor が出ることを、game-state traceとスクリーンショットで確認する。
4. actor出現後、host/client双方でpacket tick/keys/actionが一致するかを確認する。

## 実装済みの主なテストフック

- input script replay
- screenshot / framebuffer dump
- game-state trace
- call trace / write trace
- MainRAM dump
- packet capture / replay / bridge trace
- `PacketBridgeSubMenuSchedule`, `PacketBridgeSubMenuDirect`
- `SafeCourseSelectFactoryCall`, `SafeStartLoadCall`, `SafeTryChangeSceneCall`
- `PacketBridgeForceLoadGameSM`
- `PacketBridgeForceCourseSelectReady`
- `PacketBridgeForceMvlLoadThread`
- data abort register/fault-address logging

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
- `VSConnect::startLoadLevel`: `0x0214E0C0`
- `Game::loadLevel`: `0x020068A8`
- `CourseSelect update`: `0x0214C380`
- `CourseSelect natural stage request`: `0x0214C3D4`
- `loadMvsLFilesThread`: `0x02152E04`
- `Thread create used by StageStartSM`: `0x02004BFC`
- `Scene::tryChangeScene` entry: `0x0201314C`
- Stage init abort point currently observed: `0x0205545C`

## 必要なもの

- 検証にはユーザー提供の `roms/nsmb.nds` を使用する。
- ROM本体や商用資産はリポジトリに含めない。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
