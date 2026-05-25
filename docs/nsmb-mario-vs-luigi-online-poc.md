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
- player transition状態 (`player+0xB2D`, `+0x75C`, `+0x910`, `0x0208A96C/970`) を game-state CSV に出す診断列を追加済み。
- game-state CSV の既存ヘッダー漏れ (`courseSelectWord088`) を修正済み。以後のCSVではStageScene列を正しい位置で読める。
- `ARM.cpp` にwrite trace拡張と bad jump trace を追加済み。

## 最新の重要な発見

- 通常のWAN adapter routeでは StageScene が state 0 から state 1 へ進んだあと、`Stage::actorFreezeFlag=0x26` のまま停止する。
- state 1 の実体は `0x020A14D8`。ここは先頭付近で `0x020C9280` を見ており、非ゼロだと入力ラッチ処理へ進まずスキップする。
- 現在のWAN adapter routeでは `0x020C9280=0x18` になっているため、state 1 が閉じている。
- 診断用に `0x020C9280=0`, `StageScene+0x5645=1`, `0x020C928C=0` を入れると state 2 (`0x020A0C68`) へ進む。
- state 2 へ進んでも `Stage::actorFreezeFlag` は `0xA6` になり、まだplayer actorは動かない。
- state 2 では `StageScene+0x5643=0x3C` が立っており、次はここから自然にカウントダウン/開始へ進む条件を追う必要がある。
- `0x020C9280` write trace の結果:
  - `0x0214CA68` が StageScene初期化付近で `0x08` を書く。
  - `0x02126F04` が player系overlayから `0x18` を書く。
  - これはstate 1を閉じている直接要因だが、単純に毎フレーム0へ戻すとstate machineを壊すため、どの条件で自然に解除されるべきかを追う必要がある。
- `0x02126F04` は JP版 `PlayerBase::signalLocked()` 相当の `0x02126EDC` 内の書き込み点。US版 `NSMB-Code-Reference` の `PlayerBase::signalLocked()` / `signalUnlocked()` と対応する。
  - JP `PlayerBase::signalLocked()` 候補: `0x02126EDC`
  - JP `PlayerBase::signalUnlocked()` 候補: `0x02126E90`
  - trace上の呼び出し元は `0x02117CDC` と `0x0211A650`。どちらもPlayer系overlayで、MvsL開始時の土管/遷移ロックに近い。
- `PlayerBase::signalUnlocked()` 相当のビット解除だけを診断注入すると、`0x020C9280` は `0x18 -> 0x08` へ落ちるが、StageSceneはstate 1のまま止まる。
- state 1の後段には別の入力/開始ラッチがあり、診断的に `StageScene+0x5645=1` を一発入れるとstate 2へ進む。
- state 2は `StageScene+0x5649` を見ており、ここへ一発ラッチを入れるとstate 2からさらに進むが、現状は `state=1 / word561C=2` へ戻って安定しない。開始ラッチを外から足すだけでは不十分。
- `StageScene+0x5645` と `+0x5649` を長く強制すると state 1/2 を往復する。これは診断フックが自然な入力ラッチを毎フレーム再投入しているためで、最終方式としては使わない。
- `ForceStageActorFreezeFlag=0` は危険。host側で `ARM9: prefetch abort pc=FFFFF004` を起こすため不採用。
- `logs/nsmvl-signal-calltrace-20260525` の結果、通常WAN routeでは `signalLocked()` は各Playerに2回ずつ呼ばれるが、`signalUnlocked()` (`0x02126E90`) は一度も呼ばれていない。
- `Player` の土管出口遷移候補 `0x02117C80` は `player+0xB2D=0 -> 1` にし、`0x0208A96C[playerID]=2` を待つ構造。
- `logs/nsmvl-transition-table-trace-20260525` では `0x0208A96C/970` は後で `1 -> 2` へ進むが、`player+0xB2D` は1のまま残る。
- `logs/nsmvl-player-update-trace-20260525` では、Playerの遷移更新入口候補 `0x0211A56C` がWAN route中に呼ばれていない。つまり、遷移完了通知は立っているが、それを消費してPlayerを次段へ進める更新側が止まっている可能性が高い。
- `logs/nsmvl-player-main-update-trace-20260525` では、Player main update候補 `0x020F90D4` と遷移更新呼び出し点 `0x020F91C8` が呼ばれていない。Player遷移関数以前にactor update側が止まっている。
- `logs/nsmvl-freeze-flag-write-trace-20260525` では、`Stage::actorFreezeFlag` (`0x020C9250`) は `0x0214C9B0` で `0x26` に設定される。これはStageScene初期化付近の処理で、以後Player main updateを止める直接要因になっている。
- CSVヘッダーずれ修正後の短いsmokeで、ヘッダー列数と行列数が一致することを確認済み。
- `logs/nsmvl-post-transition-gate-pulse-20260525` では、`0x0208A96C/970=2` 後に `signalLocked` 相当を解除してからstate1/state2ラッチを入れると、state2へは入る。
  - ただし `Stage::actorFreezeFlag=0xA6` のまま、`player+0xB2D=1` / `transitFunc=0x02117C80` は変わらない。
  - state2継続ラッチ後は `Stage::actorFreezeFlag=0x26`, `StageScene state=1`, `StageScene+0x561C=2` に戻る。遷移完了後タイミングでも、外部ラッチだけでは自然な試合開始にはならない。

## 現在のブロッカー

- StageScene state 1を自然に閉じる条件がまだ特定できていない。`signalLocked()`解除、state1入力ラッチ、state2継続ラッチ、Player遷移更新がそれぞれ別条件になっている。
- `0x0208A96C/970` の遷移完了値は立つが、Player側が `player+0xB2D=1` から進まず、`signalUnlocked()` が呼ばれない。
- 診断的に state 2 へ進めても、state machineが自然な試合開始状態へ収束せず、まだplayer actorは操作可能になっていない。
- デバッグビルドの実行が遅く、3300F前後のWAN route検証はタイムアウトしやすい。CSVの部分結果は使えるが、成功判定は厳密に見る必要がある。

## 次にやること

1. Player遷移更新入口 `0x0211A56C` が呼ばれない理由を追う。特に `Stage::actorFreezeFlag=0x26`、`0x020C9280=0x18`、StageScene state 1の関係を見る。
2. StageScene state 2 (`0x020A0C68`) が `StageScene+0x5649` を自然に立てる条件、または `StageScene+0x561C=2` からstate1へ戻る理由を追う。
3. `player+0xB2D=1` かつ `0x0208A96C/970=2` の状態から、NSMBが本来どの経路で `signalUnlocked()` または通常操作状態へ戻すかを特定する。
4. 診断フックではなく、WAN adapterのpacket/input API、またはROM/メモリpatchの正しい開始短絡点から同じ状態へ自然に到達させる。
5. freeze解除後に、左右移動とジャンプがhost/client双方で反映されるか確認する。
6. その後、RNG seed/消費順、ビッグスター、8コインアイテム、ランダムステージを確認する。

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
- `logs/nsmvl-stage-scene-9280-write-trace-20260525`
  - `0x020C9280` の書き込み元が `0x0214CA68` と `0x02126F04` であることを確認。
- `logs/nsmvl-stage-scene-input-latch-long-20260525`
  - `+0x5645/+0x5649` の長時間強制は state 1/2 往復になり、自然開始にはならないことを確認。
- `logs/nsmvl-gameplay-probe-unfreeze-playercount-20260525`
  - 直接freeze解除が危険であることを確認。
- `logs/nsmvl-stage-scene-lock-ramdump-20260525`
  - JP版実RAMから `PlayerBase::signalLocked()` / `signalUnlocked()` 相当の実装とリテラルを確認。
- `logs/nsmvl-player-unlock-stage-start-20260525`
  - `signalUnlocked()` 相当の解除だけではstate 1から進まないことを確認。
- `logs/nsmvl-player-unlock-two-gate-pulse-20260525`
  - state1/state2のラッチを一発ずつ入れても自然な試合開始にはならず、state machineが戻ることを確認。
- `logs/nsmvl-signal-calltrace-20260525`
  - WAN routeでは `signalLocked()` だけが呼ばれ、`signalUnlocked()` は呼ばれないことを確認。
- `logs/nsmvl-transition-table-trace-20260525`
  - Player遷移完了テーブル `0x0208A96C/970` は `2` になるが、Playerの `+0xB2D` は1のまま進まないことを確認。
- `logs/nsmvl-player-update-trace-20260525`
  - Player遷移更新入口候補 `0x0211A56C` がWAN route中に呼ばれていないことを確認。
- `logs/nsmvl-transition-fields-csv-20260525`
  - game-state CSVへPlayer遷移フィールドを追加し、host/client双方で `transitionStatus=2`, `transitionStep=1`, `signalLock=1` が観測できることを確認。
- `logs/nsmvl-player-main-update-trace-20260525`
  - Player main update候補 `0x020F90D4` / `0x020F91C8` が呼ばれず、Player遷移更新以前で止まっていることを確認。
- `logs/nsmvl-freeze-flag-write-trace-20260525`
  - `Stage::actorFreezeFlag=0x26` の書き込み元が `0x0214C9B0` であることを確認。
- `logs/nsmvl-csv-header-smoke-20260525`
  - game-state CSVのヘッダー/行の列数一致を確認。
- `logs/nsmvl-post-transition-gate-pulse-20260525`
  - Player遷移完了後に強制ラッチしても、state2から自然な試合開始へ収束しないことを確認。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
