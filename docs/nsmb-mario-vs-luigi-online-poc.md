# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標の外部同期は、desync、通信切断、低FPS、初期状態不一致が重く、最終方式としては採用しない。

## 現在の方針

1. NSMBが使っているローカル通信境界を、できるだけ下位でWAN adapter化する。
   - 接続、peer情報、session state、packet availability、packet tickを含めて置き換える。
   - NSMB側のMvL同期ロジックは可能な限りそのまま使う。
   - actor座標や描画状態を外から同期する方向には戻らない。

2. 必要に応じてROM/メモリpatchでロビーや開始処理を短絡する。
   - UI操作やLocalMP接続を完全再現することにこだわらない。
   - 試合中は、NSMB Centralの記述どおり入力packet中心の同期へ寄せる。

3. RNGは `Net::random.value` / `Net::getRandom()` のseedと消費順一致で扱う。
   - ビッグスター、8コインアイテム、ランダムステージは継続検証対象。

## 現在できていること

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにする診断ルートを実装済み。
- `PacketBridgeFakePeerInfo` でhost/clientとも相手peerを見つけた状態へ進められる。
- `PacketBridgeClientConfirmToStageStart` でclient確認待ち後に `VSConnect::updateStageStartSM` へ進められる。
- `StageStartSM` は `state1C=3`, `state20=0`, `state24=1`, `state2C=0`, `state34=0` で呼ばれる。
- `StageStartSM` step 3の `02087E20 == 2` 相当の比較命令だけを狭く成功扱いにする `PacketBridgeStageStartNet20Check` を追加済み。
- `StageStartSM` step 6の `0200E658` ready待ちだけを狭く成功扱いにする `PacketBridgeStageStartStep6Close` を追加済み。
- `CourseSelect -> Game::loadLevel -> sceneCurrent=0x3` まで到達できる。
- StageScene本体は `objectID=0x0003`, `settings=0x00B5FF00`, `base=0x021B94CC` 付近に生成される。
- StageScene::onCreate候補は `0x020A2224`、vtableは `0x020C5864`。
- StageScene::onCreate内の `0200E658(0)` ready待ちだけを狭く成功扱いにする `PacketBridgeStageSceneReadyClose` を追加済み。
- `PacketBridgeReadPacketByte` と `PacketBridgeCheckPacketBits` を診断用に追加済み。ただし単独では成功していない。
- `Game::loadLevel` のplayerID引数と、試合中にNSMBが読む `localPlayerID` を分離する診断フックを追加済み。

## 最新の重要な発見

`Game::loadLevel` のplayerID引数をhost=0/client=1にすると、client側でStageScene/StageLayout/player/star系objectは生成されるが、上画面の地形が表示されず空中心になる。

一方で、`Game::loadLevel` のplayerID引数をhost/client両方0に固定し、StageScene ready後にclientの `localPlayerID` を1へ戻すと、frame 5100時点でhost/clientとも地形ありのMvL画面まで到達した。

確認ログ:

- `logs/nsmvl-loadlevel0-client-local1-smoke5100-20260525`

確認内容:

- host/clientとも通信切断画面ではない。
- host/clientとも黒画面ではない。
- host/clientとも上画面に地形、土管、ブロック、HUD、ミニマップが表示される。
- clientのCSV上の `localPlayerID` はframe 5100で `0x1`。

これは成功ではなく、次の検証入口。まだ実際の入力同期対戦、長時間安定、RNG/アイテム一致は未確認。

## 入力検証の最新結果

試合画面到達後の入力確認用に `tests/nsmb_mario_vs_luigi_wan_gameplay_probe.inputs` を追加した。

ログ:

- `logs/nsmvl-gameplay-probe-loadlevel0-local1-20260525`
- `logs/nsmvl-gameplay-probe-unfreeze-playercount-20260525`

結果:

- freeze解除なしでは、frame 5160以降に入力packetは変化するが、`StageActorFreezeFlag=0x26` が残るためplayer actorは動かない。
- `ForceStageActorFreezeFlag=0` と `ForcePlayerCount=2` をframe 5000以降に入れると、host側ではMarioが実際に動き、スクショ上も移動を確認できた。
- ただしこの強制解除ルートはhost側で `ARM9: prefetch abort (frame=5191 pc=FFFFF004)` を起こしたため失敗。
- client側は同じ検証でframe 5100以降のスクショ/CSVまで安定到達しておらず、host/client同期検証にはまだ使えない。

結論:

- 入力がplayer actorへ届く経路自体は一部確認できた。
- ただし、`StageActorFreezeFlag` や `playerCount` を外から雑に解除するのは不安定。
- 次はfreezeを直接0にするのではなく、StageStart/StageSceneの自然な開始条件、countdown、packet-ready、player/session stateのどれがfreeze解除を担当しているかを追う。

## 現在のブロッカー

- StageScene readyを自然なpacket sequencerだけで閉じられていない。
- `02087E20=2` を広く、またはStageScene検出後に書く方式は通信切断やタイトル復帰を起こすため不採用。
- `PacketBridgeReadPacketByte` 単独で `0200E978` をbridge packetへ差し替えてもStageSceneは自然にActive化しない。
- `PacketBridgeCheckPacketBits` 単独で `020111D4` をbridge packetから判定する方式はtimeoutする。
- `PacketBridgeStageSceneReadyClose` は診断用バイパスであり、最終的にはpacket sequencer側で自然に満たす必要がある。
- frame 5100到達後の実際の試合入力、相手入力packet、RNG消費順、スター/8コインアイテム一致は未検証。

## 次にやること

1. `Game::loadLevel playerID=0` と `localPlayerID=role` の分離を前提に、frame 5100以降で入力packet同期を再開する。
2. host/clientでplayer actorの操作主体、HUD、packet送受信player番号が破綻していないか確認する。
3. `StageActorFreezeFlag=0x26` が自然に解除される条件を追う。
4. `StageSceneReadyClose` なしで同じ状態へ行くため、`020110E4` / `02011360` / `02011428` 周辺のpacket buffer/bitmap更新点を追う。
5. MvL画面到達後、短い入力スクリプトで左右移動やジャンプが双方に反映されるかを確認する。
6. その後、RNG seed/消費順、スター位置、8コインアイテム、ランダムステージを検証する。

## 失敗済み・非採用のルート

- LocalMP状態を作ってからWANへ切り替える方式。
- savestateでLocalMP状態を共有してからWAN化する方式。
- actor座標やrender/model stateを外から同期する方式。
- 任意フレーム・任意PCからの直接 `Game::loadLevel` 呼び出し。
- `02087E20=2` のグローバル固定。
- `02087E20=2` をStageScene検出後に書く方式。
- `StageStartSM` / `VSConnect::onUpdate` の無理な直接呼び出し。
- `PacketBridgeReadPacketByte` 単独で `0200E978` をbridge packetへ差し替える方式。
- `PacketBridgeCheckPacketBits` 単独で `020111D4` をbridge packetから判定する方式。

## 検証ルール

- `通信が切断されました` は失敗。
- 黒画面、低速化、死亡演出、接触をスター取得や試合成功と誤判定しない。
- スクリーンショットとgame-state CSVの両方で確認する。
- player actor、StageScene、packet tick、RNG、star/itemまで確認するまで「対戦開始成功」とは呼ばない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
