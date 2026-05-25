# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate、試合開始後のWAN切り替え、actor座標の外部同期は、desync、通信切断、低FPS、初期状態不一致が重く、最終形としては採用しない。

## 現在の方針

1. NSMBが使っているローカル通信境界を、できるだけ下位からWAN adapter化する。
   - 接続、peer情報、session state、packet availability、packet tickを含めて置き換える。
   - NSMB側のMvL同期ロジックは可能な限りそのまま使う。
   - 個別actor座標や描画状態を外から同期する方向には戻らない。
2. 必要に応じてROM/メモリpatchでロビーや開始処理を短絡する。
   - UI操作やLocalMP接続の完全再現にこだわらず、MvL開始へ自然に入る入口を探す。
   - 任意PCからの直接 `Game::loadLevel` はARM9 abortや黒画面化の原因になるため、自然な呼び出し文脈を優先する。
3. 試合中は入力packet中心の同期に寄せる。
   - RNGは `Net::random.value` / `Net::getRandom()` のseedと消費順一致で扱う。
   - ビッグスター、8コインアイテム、ランダムステージは後続の検証対象。

## 現在の到達点

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにする診断経路を実装済み。
- `PacketBridgeFakePeerInfo` でhost/clientとも相手peerを見つけた状態へ進められる。
- `PacketBridgeClientConfirmToStageStart` でclient確認待ち後に `VSConnect::updateStageStartSM` へ差し替えられる。
- `StageStartSM` は `state1C=3`, `state20=0`, `state24=1`, `state2C=0`, `state34=0` で自然に呼ばれる。
- `StageStartSM` step 3は `02087E20 == 2` 相当のチェックを待っていることをRAM dumpとdispatch traceから確認済み。
- `02087E20` をグローバルに `2` へ固定すると黒画面や低速化を起こすため不採用。
- `02151454` のstep 3比較命令だけを狭く上書きする `PacketBridgeStageStartNet20Check` を追加し、step 6まで進むことを確認済み。
- `0200E658` のstep 6 close待ちだけを狭く成功扱いにする `PacketBridgeStageStartStep6Close` を追加し、`vsConnect+0x144=7` まで進むことを確認済み。
- StageScene::onCreate内の `0200E658(0)` ready待ちだけを狭く成功扱いにする `PacketBridgeStageSceneReadyClose` を追加した。
- PacketBridgeが送る52byte packetに、NSMB送信処理と同じ `0x0208806C -> packet[0x29]` のready bitを明示的に載せるようにした。
- 診断用に `PacketBridgeReadPacketByte` と `PacketBridgeCheckPacketBits` を追加した。どちらも下位packet境界をadapter化するための実験フックで、現時点では単独成功していない。
- CourseSelect状態のCSV診断フィールドを拡張済み。
- `PacketBridgeForceCourseSelectReady` の `0214ED18` フックが開始フレーム指定前にも発火していたため、開始フレーム以降に制限した。
- `tests/nsmb_mario_vs_luigi_wan_course_select.inputs` を追加し、CourseSelect後にもA入力を入れる検証ができる。

## 最新の検証結果

### CourseSelect到達

ログ: `logs/nsmvl-ui-wan-course-select-fields-20260525`

- host/clientとも `sceneCurrentSceneID=0x5`, `courseSelectFound=1`。
- スクリーンショットは「たいせんほうほう / せんたくちゅう」画面。
- `stageID=0`, `stageGroup=0`, player actorなし。まだ試合開始ではない。

### CourseSelect ready遅延フック

ログ: `logs/nsmvl-ui-wan-course-select-force-ready-delayed-20260525`

- `PacketBridgeForceCourseSelectReadyStartFrame=3300` 以降に制限すると黒画面化せず、CourseSelect内部状態が `0x1 -> 0x3` に進む。
- スクリーンショットは「コースせんたくちゅう」画面。
- ただしA入力がないとそのまま選択画面に留まる。

### CourseSelect後A入力からStageScene生成まで

主なログ:

- `logs/nsmvl-ui-wan-load-calltrace-20260525`
- `logs/nsmvl-ui-wan-load-ramdump-20260525`
- `logs/nsmvl-ui-wan-force-stage-active-early-dump-20260525`

確認できたこと:

- A入力でコース選択画面から自然な `Game::loadLevel` 呼び出しまで進む。
- frame 4833付近で `Game::loadLevel(r0=0xF, r1=1, r2=9, playerID=localPlayer)` が呼ばれる。
- frame 4870以降に `sceneCurrentSceneID=0x3`, `stageGroup=9` へ移る。
- StageScene本体は `id=0x0003`, `settings=0x00B5FF00` として生成される。実測baseは `0x021B94CC`。
- player actor、Star、StageCamera、StageController、StageActorManager、MvlObject267も生成される。
- ただし自然経路ではStageSceneが `state=0`, `skipFlags=5` のままActive化せず、画面は黒のまま。
- `ForceStageSceneActive + ForceStageActorFreezeFlag=0 + ForcePlayerCount=2` はclient側で背景表示まで進むが、host側はHUDのみ/黒画面になり、object生成順やStageCamera状態もhost/clientで一致しない。
- そのためStageSceneのstateを外からActiveにするのは診断用途のみ。最終方式としては採用しない。

### StageScene ready close

主なログ:

- `logs/nsmvl-stage-scene-oncreate-trace-20260525`
- `logs/nsmvl-stage-scene-return-trace-20260525`
- `logs/nsmvl-stage-scene-ready-close-20260525`
- `logs/nsmvl-stage-scene-ready-close-dump-20260525`

確認できたこと:

- StageScene vtable `0x020C5864` の先頭slotから、A2DJのStageScene::onCreate候補は `0x020A2224`。
- StageScene::onCreateは `0200E658(0)` のready待ちで `BaseReturnState::Minus1` 相当を返し続け、Base::processCreateが毎フレームcreate処理を再試行していた。
- `PacketBridgeStageSceneReadyClose` で `instrAddr=0x0200E658`, `LR=0x020A2348` の呼び出しだけ成功扱いにすると、StageSceneは `state=1` へ進む。
- force activeなしでhostは地形つきMvsL画面を表示できた。
- clientもStageScene/StageLayout/player/star系objectはActive化するが、frame 5100時点では上画面が空中心で地形が表示されない。これは成功ではない。
- RAM dumpではStageLayoutはhost/client両方に存在する。差分はGoombaの有無、StageFX settings、StageController/StageFXのGUID割当などに残っている。

### packet bit / packet byte adapter検証

主なログ:

- `logs/nsmvl-stage-scene-ready-close-delay4920-20260525`
- `logs/nsmvl-stage-ready-packet-calltrace-20260525`
- `logs/nsmvl-read-packet-byte-no-readyclose-20260525`
- `logs/nsmvl-read-packet-byte-with-readyclose-20260525`
- `logs/nsmvl-packet-ready-byte-no-readhook-20260525`
- `logs/nsmvl-check-packet-bits-natural-20260525`

確認できたこと:

- `StageSceneReadyCloseStartFrame=4920` でhost/clientを同じ絶対frameまで待たせても、hostは地形あり、clientは空中心のままで改善しなかった。
- StageScene::onCreateの前半はhost frame 4883、client frame 4910付近で `0200E670(0)` を呼び、`0x0208806C` のbit 0を立てている。
- `0200E658(0)` は `020111D4 -> 020110E4` に入り、packet byte `0x29` のbit 0を全peer分チェックしている。
- `PacketBridgeReadPacketByte` で `0200E978` をbridge packetから直接読むようにしても、StageSceneは自然にはActive化せず黒画面のまま。
- `packet[0x29] = [0x0208806C]` をWAN packetへ載せても、既存の内部packet sequencerだけではStageScene ready待ちは自然に閉じなかった。
- `PacketBridgeCheckPacketBits` で `020111D4` をbridge packetの `0x29` から判定する試行はtimeoutした。現状のbridge packet選択・tick検索・ready bitのタイミングがまだ自然なNSMB側期待と噛み合っていない。
- `PacketBridgeReadPacketByte + StageSceneReadyClose` でもhost/clientのGoomba有無、StageFX settings、StageController GUID差分は残った。

## 現在のブロッカー

`CourseSelect -> Game::loadLevel -> sceneCurrent=0x3` までは進み、StageScene/player/star系objectもActive化できるところまで来たが、host/clientの画面・object setがまだ一致しない。

現時点で分かっている実体:

- real StageScene: `objectID=0x0003`, `settings=0x00B5FF00`, `base=0x021B94CC` 付近。
- `PacketBridgeStageSceneReadyClose` 後のstate: `state=1`, `flags=0x00010000`。
- forced activeは描画を部分的に進めるが、host/client間のobject setやStageCamera内部状態がずれるため、引き続き診断専用。

疑っている点:

- StageScene::onCreate完了前後のpacket bit到着順、packet tick検索、内部packet sequencerの状態がhost/clientでずれている。
- `0x0208806C -> packet[0x29]` のready bitをWAN packetへ載せるだけでは、NSMB内部の `processRecvPacket` / `checkAllPacketBits` の期待状態を満たせていない。
- `StageActorFreezeFlag=0x26` や `playerCount=0` が残っており、試合開始の最終ready条件がまだ満たせていない可能性。
- StageStart/CourseSelectで強制しているNet状態が、ロード後のstage packet / RNG / scene setup条件と矛盾している可能性。
- debug/releaseどちらも長時間検証は重い。診断は短いフレーム範囲とRAM dumpで回す。

## 次にやること

1. `020110E4` / `02011360` / `02011428` 周辺を基準に、NSMBのpacket bit送受信状態をもう一段正確にadapter化する。
   - 直接 `020111D4` を成功扱いにするのではなく、packet sequencerが期待する内部buffer/bitmapへbridge packetを自然に入れる方法を探す。
   - `PacketBridgeCheckPacketBits` はtimeoutしたため、次は値の強制ではなく内部buffer更新点を追う。
2. StageScene Active後のhost/client差分を、Goomba/StageFX/StageController生成前の分岐から追う。
   - 生成差がpacket順、RNG順、localPlayer差、StageFX character設定のどれかを切り分ける。
3. client上画面が空中心になる原因を確認する。
   - StageLayoutは存在するため、camera/view/register/VRAM側の差分も見る。
4. `StageActorFreezeFlag=0x26` と `playerCount=0` が解消される自然条件を追う。
5. host/clientで同じMvsL画面まで安定して到達できたら、入力packet同期、RNG seed/消費順、star/8コインアイテムの一致検証に戻る。

## 失敗済み・非採用の経路

- LocalMP状態を作ってからWANへ切り替える方式。
- savestateでLocalMP状態を共有してからWAN化する方式。
- actor座標やrender/model stateを外から同期する方式。
- 任意フレーム・任意PCからの直接 `Game::loadLevel`。
- `02087E20=2` のグローバル固定。
- `StageStartSM` / `VSConnect::onUpdate` の無理な直接呼び出し。
- `PacketBridgeReadPacketByte` 単独で `0200E978` をbridge packetへ差し替える方式。
- `PacketBridgeCheckPacketBits` 単独で `020111D4` をbridge packetから判定する方式。

## 検証ルール

- `通信が切断されました` は失敗として扱う。
- 黒画面、低速化、死亡演出、敵接触をスター取得や試合成功と誤判定しない。
- スクリーンショットとgame-state CSVの両方で確認する。
- player actor、stage scene、star/RNG、packet tickを確認するまで「対戦開始成功」とは呼ばない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
