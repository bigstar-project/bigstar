# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標や描画状態の外部同期は、desync、通信切断、低FPS、内部状態不一致が重く、最終方式としては採用しない。

## 現在の方針

1. NSMBが使うローカル通信境界を、できるだけ下位でWAN adapterへ差し替える。
   - 接続、peer/session、packet availability、packet tick、試合中入力packetまで含めて追う。
   - NSMB側の同期ロジック、RNG消費、MvL試合進行は可能な限りそのまま使う。
2. 必要ならROM/メモリpatchでロビーや開始処理を短絡する。
   - UI操作やLocalMP接続の完全再現にこだわらない。
   - ただし、試合中はNSMB Centralの記述どおり入力packet中心の同期へ寄せる。
3. 直接的なactor座標同期、描画状態同期、乱暴なfreeze解除は診断用に限定する。

## 到達済み

- `Wifi::startChildScan` / `Wifi::startParent` / `Wifi::connectToParent` 相当をWAN adapter側で成功扱いにする診断ルートを追加済み。
- host/clientとも fake peer を見つけた状態へ進められる。
- `VSConnect::updateStageStartSM` まで進めるための packet bridge 診断フックを追加済み。
- `CourseSelect -> Game::loadLevel -> sceneCurrent=0x3` まで到達できる。
- `Game::loadLevel` は host/client とも playerID=0 で呼び、StageScene ready後にclientの `localPlayerID` を1へ戻すと、両者とも地形ありのMvL画面まで到達する。
- StageScene本体は `objectID=0x0003`, `settings=0x00B5FF00`, `base=0x021B94CC` 付近に生成される。
- StageScene vtable は `0x020C5864`。主な関数:
  - onCreate: `0x020A2224`
  - update dispatcher: `0x020A1BAC`
  - render dispatcher: `0x020A1D60`
- StageScene update dispatcher は `StageScene + 0x5618` をstate indexとして使う。
  - state 0 update: `0x020A1B50`
  - state 1 update: `0x020A14D8`
  - state 2 update: `0x020A0C68`
- StageScene state/dispatch/関連グローバルを game-state CSV に出す診断列を追加済み。
- `ARM.cpp` にwrite trace拡張と bad jump trace を追加済み。

## 最新の重要な発見

- 通常のWAN adapter routeでは StageScene が state 0 から state 1 へ進んだあと、`Stage::actorFreezeFlag=0x26` のまま停止する。
- state 1 の実体は `0x020A14D8`。ここは先頭付近で `0x020C9280` を見ており、非ゼロだと入力ラッチ処理へ進まずスキップする。
- 現在のWAN adapter routeでは `0x020C9280=0x18` になっているため、state 1 が閉じている。
- 診断用に `0x020C9280=0`, `StageScene+0x5645=1`, `0x020C928C=0` を入れると state 2 (`0x020A0C68`) へ進む。
- state 2 へ進んでも `Stage::actorFreezeFlag` は `0xA6` になり、まだplayer actorは動かない。
- state 2 では `StageScene+0x5643=0x3C` が立っており、次はここから自然にカウントダウン/開始へ進む条件を追う必要がある。
- `ForceStageActorFreezeFlag=0` は危険。host側で `ARM9: prefetch abort pc=FFFFF004` を起こすため不採用。

## 現在のブロッカー

- StageScene state 1 を自然に閉じる packet/input/session 条件がまだ特定できていない。
- 診断的に state 2 へ進めても、まだ freeze が解除されず、試合開始状態には到達していない。
- デバッグビルドの実行が遅く、3300F前後のWAN route検証はタイムアウトしやすい。CSVの部分結果は使えるが、成功判定は厳密に見る必要がある。

## 次にやること

1. `0x020C9280` が本来どこで0になるか、write trace/call traceで追う。
2. state 2 (`0x020A0C68`) の `StageScene+0x5643` / `+0x561C` / `+0x563C` と、関連packet/input条件を追う。
3. 診断フックではなく、WAN adapterのpacket/input APIから同じ状態へ自然に到達させる。
4. freeze解除後に、左右移動とジャンプがhost/client双方で反映されるか確認する。
5. その後、RNG seed/消費順、ビッグスター、8コインアイテム、ランダムステージを確認する。

## 検証ルール

- `通信が切断されました` は失敗。
- 黒画面、極端な低FPS、死亡演出、敵接触、片側だけの進行、HUDだけ一致、actorだけ一致は成功扱いしない。
- スクリーンショットと game-state CSV の両方で確認する。
- player actor、StageScene、packet tick、RNG、star/itemまで確認するまで「対戦開始成功」とは呼ばない。

## 主なログ

- `logs/nsmvl-stage-scene-dispatch-fixed-20260525`
  - StageScene state 1 の dispatch 実体を確認。
- `logs/nsmvl-stage-scene-state1-globals-20260525`
  - state 1 が `0x020C9280=0x18` で閉じていることを確認。
- `logs/nsmvl-stage-scene-input-latch-clear9280-20260525`
  - 診断的に state 2 へ進めることを確認。ただしfreeze解除は未達。
- `logs/nsmvl-gameplay-probe-unfreeze-playercount-20260525`
  - 直接freeze解除が危険であることを確認。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
