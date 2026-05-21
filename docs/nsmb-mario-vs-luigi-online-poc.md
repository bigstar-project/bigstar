# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

現在の主方針は、melonDS の LocalMP 2インスタンス同期を最終形にしない。NSMB側が元々持っているMvL接続/同期/state machineを活かし、ローカル無線の代わりにWAN adapter / PacketBridgeから必要なpacketを渡す。

ただし、現行の `NoLanMP + PacketBridge + state注入` は最終形としては危険。現行PacketBridgeは主に `ggid=0x42` のMvL文脈成立後のpacket helper/slotを差し替える作りで、接続・ロビー・ロード前提状態そのものを自然に成立させられていない。その不足を埋めるために `ForceNetReady`、`SubMenuDirect`、`CourseSelectFactory`、`LoadGameSM` などの内部state注入へ拡張してきたが、この方向を続けるとNSMBの状態機械を外から不完全に再現する作業になる。

## 今後の方針

最終目標に向けた本筋は、次の2アプローチに絞る。

1. **より下位の通信境界をadapter化する**
   - `Net::getConsoleKeys/getPacketByte/getPacketTick/getPacketAction` だけではなく、接続・ロビー・packet availability・session状態まで含む下位MP APIをWAN adapterへ置き換える。
   - まず `0204619C` / `0204622C` / `02046480` と `Net::Core::transferPacket` の副作用で足りるかを検証し、足りなければさらに下の送受信/状態APIを特定する。
   - これは「NSMB側の接続/同期/state machineをできるだけそのまま使う」方向。
2. **ロビー/開始処理をROMまたはメモリpatchで短絡し、試合中packetだけadapter化する**
   - UI操作、LocalMP接続、CourseSelect、StageStartSMを外から無理に再現するのをやめ、MvLステージ開始状態を直接作る専用入口を作る。
   - 試合開始後は、NSMBが本来使う入力/状態packetをWAN adapterに流す。
   - 現行のstate注入より、最終的な `melonDS 1インスタンス * 2PC` 形に近く、安定性の見込みがある。

当面は1を短く切り分け、接続/ロビーを自然成立させるための下位adapterが大きすぎると判断したら2へ寄せる。StageStartSMやCourseSelectを外から継ぎ接ぎする検証は、補助診断として扱い、最終方針の中心にしない。

## 現在の到達点

- `NoLanMP + PacketBridge from start` で、起動直後からローカル無線なしのPacketBridge経路を使う検証ルートを構築済み。
- `NoLanMP + PacketBridge + AllowPreGame` に `HostTickOffset=+52 / ClientTickOffset=-51 / ReturnLookupTick` を組み合わせると、`ForceNetReady` / `SubMenuDirect` / `CourseSelectFactory` / `LoadGameSM` なしで、host/clientとも `VSConnect::scheduleSubMenuChange(loadGameSM)` まで自然到達する。
- その状態では `VSConnect::updateLoadGameSM` が継続して呼ばれるが、`VSConnect` のload stepが `1` 付近で止まり、`VSConnect::startLoadLevel` と `Game::loadLevel` にはまだ到達しない。
- `updateLoadGameSM` をA2DJ実コードから逆アセンブルし、host側は `0x02087E24 == 2` とpeer entry、client側は `0x0208B6C0` 系のpeer recordを待つことを確認した。
- `PacketBridgeMaintainPacketFreeBytes` と `PacketBridgeMaintainSessionPeers` を追加した。これは上位VSConnect/CourseSelectを直接進めるForce系ではなく、LocalMPが本来維持する下位Netのpacket free-byte bitmapとpeer/session tableをPacketBridge側で補う診断フラグ。
- session peer補完ありでは、hostは `loadGameSM -> post-load -> StageStartSM` まで自然に進み、`vsMode=1` になる。ただし `StageStartSM` のstep 5で `VSConnect + 0x156` のready bitが両player分立つのを待ち続け、まだ `startLoadLevel` には到達しない。
- PacketBridgeのローカルpacket生成を修正し、gameplay中だけでなくpre-game/StageStart中も `0x02087F08..0x02087F33` を含む52 byte全体を送るようにした。これによりmarker byte `0x29` やpacket sequencer payloadをWAN adapterへ載せられる。
- 診断用 `PacketBridgeStageStartReadyProbe` を追加した。これは `StageStartSM` step 5で `VSConnect + 0x156` にready bitを直接立てる仮説検証フラグで、最終実装ではない。
- full packet化 + session peer補完 + ready probeでは、hostが `0200E664`、`VSConnect::startLoadLevel=0x0214E0C0`、`Game::loadLevel=0x020068A8` まで到達し、`stageGroup=0x9` / `vsMode=1` / player actor生成まで進むことを確認した。ただしclientはまだStageStartSMにそろわず、`sceneCurrentSceneID=0x6` / `vsMode=0` で待機側に残る。
- `PacketBridgeSubMenuSchedule` / `PacketBridgeSubMenuDirect` で、自然LANで観測したVSConnectサブメニュー遷移を注入できる。
- `SafeCourseSelectFactoryCall` により、`CourseSelect` object生成までは再現できる。
- `VSConnect::startLoadLevel` と `Game::loadLevel(scene=0x0F, stageGroup=9, vsMode=1, ggid=0x42)` まではNoLanMP経路で再現できる。
- `CourseSelect` update内の自然なstage requestを特定し、`0x0214C3D4 -> 0x020130A8(scene=3, settings=00B5FF00)` でStage sceneへ遷移しようとするところまで再現できる。

## 直近で分かったこと

- 純粋PacketBridge寄りの最新検証では、host/clientのpacket tickが約51-52フレームずれていた。role別replay tick offsetと、返却packetのtickをlookup tickへ正規化する処理を入れたことで、少なくともLoadGameSMへの自然遷移までは進むようになった。
- LoadGameSM停滞中は `Net::Core::advancePacketSequencer` / `processRecvPacket` / `processSendPacket` が大量に呼ばれる一方、`readPacketByte/readPacketInt/freePacketBytes/allocPacketBytes` 系の呼び出しは観測できていない。
- `packetFreeBytesRecvBitmap` だけを維持してもLoadGameSM停滞は解消しなかった。peer/session tableまで維持するとhostはStageStartSMへ進むため、現時点の境界不足は「packet payload + free-byte bitmap」ではなく「session/peer state + packet sequencer完了通知」まで含む。
- StageStartSM step 5は `VSConnect + 0x156` のbit 0/1を待つ。write traceではこのready bitへの自然書き込みはまだ発生していない。hostだけが先にStageStartSMへ進み、clientが別サブメニューに残るため、StageStart以降のpacket tick差が固定offsetでは吸収できないほど広がる。
- `0200E670(0)` はmarker 0を立て、`0200E658(0)` はpacket byte `0x29` のmarker 0がactive peer分そろったかを見る同期バリアだった。full packet化前はbyte `0x29` が常に0になっており、ready probe後もhostがここで詰まっていた。
- full packet化後は、ready probeありのhostがmarker barrierを抜けて `startLoadLevel` まで進む。したがってhost側StageStartの残課題は、ready bitを本来立てるpacket sequencer complete callbackをPacketBridge境界で再現すること。
- ただしclientはまだStageStartSMへ自然到達していないため、hostだけを進めても最終対戦にはならない。client側のLoadGameSM/PostLoad/StageStart遷移条件を下位Net/session側から満たす必要がある。
- `PacketBridgeWait` のcore側実装を追加したが、全remote packet取得で20ms待つ構成は重すぎ、host/clientともpost-load付近で停滞してプロセスタイムアウトした。WAN adapterとして待ちは必要だが、無条件waitではなく対象phase/packet種別を絞る必要がある。
- `0x02087E10` / `Game::localPlayerID=0x020850BC` をroleに合わせて常時維持する試験は、host側StageStart後にscene状態を壊したため不採用。local aid/player ID補完は接続前段階へ常時書くのではなく、開始状態を作るROM/メモリpatch側で扱う方がよい。
- tick差を吸収するための診断として `PacketBridgeLiveFallbackNearest` を追加したが、広い前後fallbackを接続前段階から使うと古い/未来のpacketを拾いすぎ、hostがStageStartSM中に `netState1C=9` へ落ちる。無条件のnearest fallbackは本筋にしない。
- `0x0214C3D4` はVSConnectのstage-start updateではなく、overlay 52内のCourseSelect updateが `Scene::tryChangeScene` 相当を呼ぶ場所だった。
- そのため、外から無理にscene 3へ飛ばすより、CourseSelect updateのready判定を自然タイミングで通す方が筋が良い。
- `loadMvsLFilesThread` の日本版A2DJアドレスは `0x02152E04`。以前使っていた `0x02152E18` は関数入口ではなく、完了フラグ `0x0208A478=1` を書く途中命令だった。
- MvLファイルロード本体は直接呼ぶものではなく、StageStartSMが `02004BFC(entry=0x02152E04, stackSize=0x1000)` でロード用スレッドを作る。
- ただし、PacketBridge側から単純にロードスレッドを早期起動しても `02009B94` 付近のcache loadで実質停止する。ロード前提状態または呼び出しタイミングが自然StageStartSMとまだ一致していない。

## 現在の主ブロッカー

NoLanMP + PacketBridge経路で、接続/ロード開始に必要な下位Net状態が自然LANと一致していない。

純粋adapter寄りの構成では、接続ロビーからhostのStageStartSMまでは自然に起きるが、host/clientが同じサブメニュー段階にそろわず、StageStartSMのready bitが自然には立たない。

full packet化でpre-game/StageStartのmarker/payload欠落は解消した。ready bitを診断的に立てるとhostは `startLoadLevel` まで進むため、次の主ブロッカーは「packet sequencer complete callbackを自然に発火させること」と「client側もhostと同じLoadGameSM/PostLoad/StageStart段階へ進めること」。

補助診断として、過去のstate注入ルートではStage sceneへ入る直前のリソース準備も自然LANと一致していないことが分かっている。ファイルロードを省略するとStage初期化中に `0205545C` 付近でdata abortする。逆にロード関数/ロードスレッドを単純に呼ぶと、`02009B94` 付近で非常に長く止まり、VSConnect scene 6から先へ進まない。

より大きい設計上のブロッカーは、現行PacketBridgeの差し替え境界が高すぎる可能性があること。`Net::getConsoleKeys/getPacketByte/getPacketTick/getPacketAction` とpacket slot差し替えだけでは、MvL接続前の下位通信状態、接続確立、player ID、ロビー、ロード開始条件まで自然に作れない可能性が高い。

## 次にやること

1. 現行PacketBridgeで「純粋adapter」と呼べる範囲を切り分ける。
   - `ForceNetReady` / `SubMenuDirect` / `CourseSelectFactory` / `LoadGameSM` などの内部state注入なしで、host/clientとも `loadGameSM` までは自然到達することを確認済み。
   - `packetFreeBytesRecvBitmap` だけでは不足。peer/session table補完でhostはStageStartSMまで進むことを確認済み。
   - pre-game/StageStart中も52 byte packet全体を送るように修正済み。ready probe併用でhostが `startLoadLevel` / `Game::loadLevel` まで進むことを確認済み。
   - 次はStageStartSM ready bitを本来立てるpacket sequencer callbackを特定し、診断probeではなくadapter境界で再現する。
   - 並行して、client側も同じStageStartSMへ自然到達させる条件を特定する。
   - tick fallbackやpacket waitを使う場合は、接続/ロビー全体ではなく、packet sequencerの対象action/phaseに限定する。
2. 差し替え境界を下げる候補を調べる。
   - `Net::Core::transferPacket` の返り値だけでなく、副作用・下位MP API・session状態を含めて置き換えられるかを調べる。
   - `0204619C` / `0204622C` / `02046480` だけで足りるか、さらに下の送受信/状態APIが必要かを確認する。
3. ROM/メモリpatchでMvL開始状態を直接作るルートを設計する。
   - A2DJ用に、ROM本体はコミットせず、ユーザー所有ROMへ差分適用するpatch生成方式を前提にする。
   - MvLステージ開始状態の最低条件を、既存のアドレス表・自然LANログ・`external/NSMB-Code-Reference` から整理する。
4. 試合中packet同期だけをadapter化できるか検証する。
   - NSMB Centralの「入力情報中心のpacket」前提を、実ログとhelper hookで再確認する。
   - stage開始後の `packet tick/keys/action` がWAN越しで一致するかを最小検証する。
5. 補助診断として、自然StageStartSMのstep遷移を再現する。
   - `createStageStartSM=0x021515B4`
   - `updateStageStartSM=0x021512B8`
   - step 2の `02004BFC(entry=0x02152E04)` を、自然の前提状態で踏ませる。
6. Stage scene遷移後に `playerActor0/1` と Big Star actor が出ることを、game-state traceとスクリーンショットで確認する。

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
