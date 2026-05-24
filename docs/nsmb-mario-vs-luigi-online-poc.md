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

### CourseSelect後A入力

ログ: `logs/nsmvl-ui-wan-load-black-ram-20260525`

- A入力でコース選択画面から `Game::loadLevel` の自然呼び出し付近まで進む。
- frame 4860付近で `sceneCurrentSceneID=0x5`, `sceneNextSceneID=0xF`, `stageGroup=9`。
- その後 `sceneCurrentSceneID=0x3`, `stageGroup=9` へ移るが、画面は黒、`playerActor0/1` と `stageScene` は未生成。
- frame 5100 RAM dumpではStageController、StageActorManager、Goombaなど一部のstage系objectは見えるが、player actorとStageSceneはまだ見えない。
- これは成功ではない。現ブロッカーは「ロード開始後にステージ/プレイヤー生成まで進まない」こと。

## 現在のブロッカー

`CourseSelect -> Game::loadLevel -> sceneCurrent=0x3` までは進むが、黒画面のまま `stageSceneFound=0`, `playerActor0Found=0`, `playerActor1Found=0` に留まる。

疑っている点:

- `Scene::current/next/isSceneActive` の遷移が自然経路とずれている。
- `StageActorFreezeFlag=0x26` のまま stage actor / player生成が止まっている可能性。
- StageStart/CourseSelectで強制しているNet状態が、ロード後のstage packet / RNG / scene setup条件と矛盾している可能性。
- debug/releaseどちらも長時間検証は重く、Release presetはPATH上のclang前提で失敗したため、フルパス指定でRelease構成を作成した。

## 次にやること

1. 黒画面ロード中の自然待ち条件を特定する。
   - `sceneCurrent=0x3`, `sceneNext=0x181`, `sceneIsSceneActive=1` の意味を追う。
   - `StageActorFreezeFlag=0x26` が自然値か異常値かを比較する。
   - `stageID/stageGroup/vsMode/localPlayerID` と stage packet words の関係を確認する。
2. `Game::loadLevel` 呼び出し時の引数を、US symbol / A2DJ port / RAM dumpで再確認する。
   - 現在の `force Game::loadLevel MvL args` が自然呼び出しを壊していないか確認する。
3. 必要ならCourseSelectからのloadLevel後だけ、最小限の下位Net/scene条件を補う。
   - ただしactor座標同期や描画状態の外部同期には戻らない。
4. stage scene / player actorが生成されたら、入力packet同期とRNG一致検証に戻る。

## 失敗済み・非採用の経路

- LocalMP状態を作ってからWANへ切り替える方式。
- savestateでLocalMP状態を共有してからWAN化する方式。
- actor座標やrender/model stateを外から同期する方式。
- 任意フレーム・任意PCからの直接 `Game::loadLevel`。
- `02087E20=2` のグローバル固定。
- `StageStartSM` / `VSConnect::onUpdate` の無理な直接呼び出し。

## 検証ルール

- `通信が切断されました` は失敗として扱う。
- 黒画面、低速化、死亡演出、敵接触をスター取得や試合成功と誤判定しない。
- スクリーンショットとgame-state CSVの両方で確認する。
- player actor、stage scene、star/RNG、packet tickを確認するまで「対戦開始成功」とは呼ばない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
