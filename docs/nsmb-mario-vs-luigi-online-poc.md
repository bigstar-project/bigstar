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
- StageScene state:
  - state 1: `0x020A14D8`
  - state 2: `0x020A0C68`
  - state 3: `0x020A096C`
- state 1からstate 2へ進むには、`0x020C9280` のロック解除と `StageScene+0x5645` 相当の入力/開始ラッチが必要。
- state 2の `StageScene+0x5649` は確認/closeラッチ。late A/START入力でhost/client双方が `0x020A0E64` または近い分岐を踏めるが、結果はgameplay開始ではなく `StageScene+0x561C=2` / `StageScene+0x5618=1` へ戻るだけ。
- state 1からstate 3へ進む正規分岐は `0x020C92C0 & 3`。以前の `0x020C9280 & 3` という仮説は誤り。
- `logs/nsmvl-stage-state3-92c0-write-trace-20260525` では、`0x020C92C0` は `0x0214CA70` で初期化時に0を書かれるだけで、現在のWAN routeでは自然にbit0/bit1が立たない。
- 診断用に `MELONDS_NSML_FORCE_STAGE_SCENE_STATE3_GATE` を追加した。これは `0x020C92C0` へ値を書いて state1 -> state3 の分岐を踏ませるためのフック。
- `logs/nsmvl-stage-state3-gate-force-20260525` では、`0x020C92C0=1` を一発入れるとhost/clientとも最終的に `StageScene+0x5618=3` へ入る。
  - ただし `Stage::actorFreezeFlag=0x26` は残り、`player+0xB2D=1` のままPlayer更新は進まない。
  - つまり「state3へ入れた」だけでは試合開始成功ではない。
- `PlayerBase::signalLocked()` / `signalUnlocked()` について:
  - JP `PlayerBase::signalLocked()` 候補: `0x02126EDC`
  - JP `PlayerBase::signalUnlocked()` 候補: `0x02126E90`
  - 通常WAN routeでは `signalLocked()` は各Playerに2回ずつ呼ばれるが、`signalUnlocked()` は一度も呼ばれていない。
- `Player` の土管出口遷移候補 `0x02117C80` は `player+0xB2D=0 -> 1` にし、`0x0208A96C[playerID]=2` を待つ構造。
- `0x0208A96C/970` は後で `1 -> 2` へ進むが、Player側は `player+0xB2D=1` のまま残る。
- Player main update候補 `0x020F90D4` / 遷移更新呼び出し点 `0x020F91C8` / 遷移更新入口候補 `0x0211A56C` は現在のWAN route中に呼ばれていない。Player遷移関数以前にactor update側が止まっている。
- `Stage::actorFreezeFlag` (`0x020C9250`) は `0x0214C9B0` で `0x26` に設定される。直接0へ戻す診断は過去に `ARM9: prefetch abort pc=FFFFF004` を起こしており、最終方式としては使わない。
- game-state CSVに `0x020C92B4/92C0/92C8/92D0` を追加した。state3ゲートと周辺状態をCSVで追える。
- StageScene state 1 は `0x020C92D0` のイベントフラグを見て `Stage::actorFreezeFlag` を変更している。
  - `0x020C92D0 & 0x100`: `Stage::actorFreezeFlag |= 0x2e`
  - `0x020C92D0 & 0x200`: `Stage::actorFreezeFlag &= ~0x2e`
- 診断用に `MELONDS_NSML_FORCE_STAGE_SCENE_EVENT_FLAGS` を追加した。これは `0x020C92D0` に指定bitをORし、StageScene本来のイベント消費経路でfreeze解除を踏ませるためのフック。
- `logs/nsmvl-stage-event-92d0-clear-freeze-20260525` では、host側で `0x020C92D0 |= 0x200` が `0x020A18D4 -> 0x020A18F4` に消費され、直接freeze書き換えなしで `Stage::actorFreezeFlag=0` と `player+0xB2D=2` まで進むことを確認した。
- `logs/nsmvl-stage-event-92d0-no-state3-force-20260525` では、`0x020C92D0 |= 0x200` だけでもPlayer updateと `signalUnlocked()` は走るが、`0x020C92C0 & 3` が立たないため state3 へ自然には入らず、state1/state2 の循環へ戻ることを確認した。

## 現在のブロッカー

- `0x020C92C0 & 3` が本来どの関数、packet、session状態で立つのか未特定。
- `0x020C92D0 & 0x200` がfreeze解除の自然経路であることは見えたが、現在のWAN routeでそのイベントが自然発生する条件は未特定。
- freeze解除後はPlayer updateが走るが、state3開始ゲート `0x020C92C0 & 3` が立たない限り、自然な試合開始へは進まない。
- StageScene state遷移、Player遷移、actor freeze解除の3つがまだ自然な順番で接続できていない。特に `0x020C92C0` と `0x020C92D0` の発生源を、下位通信/session境界から追う必要がある。
- デバッグビルドの実行が遅く、3300F前後のWAN route検証はタイムアウトしやすい。CSVの部分結果は使えるが、成功判定は厳密に見る必要がある。

## 次にやること

1. `0x020C92C0` を自然に立てる書き込み元/呼び出し条件を特定する。
2. `0x020C92D0 & 0x200` を自然に立てる経路、またはROM/メモリpatchで安全に発生させる入口を特定する。
3. `0x020C92C0` / `0x020C92D0` を直接いじる診断フックではなく、WAN adapterのpacket/input API、またはROM/メモリpatchの正しい開始短絡点から同じ状態へ自然に到達させる。
4. state3遷移とfreeze解除が揃った状態で、Playerが通常操作状態へ戻るか確認する。
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
- `logs/nsmvl-stage-scene-byte-write-trace-20260525`
  - StageScene state2の主要書き込み元と、clientだけが `0x020A0E64` で `+0x5649` を自然に立てることを確認。
- `logs/nsmvl-state2-late-confirm-20260525`
  - late A/START入力でhost/client双方がstate2確認分岐を踏めるが、state3へは進まずstate1/2を往復することを確認。
- `logs/nsmvl-stage-state3-92c0-write-trace-20260525`
  - state1からstate3へ進む正規分岐が `0x020C92C0 & 3` であること、現在のWAN routeでは `0x020C92C0` が自然に立たないことを確認。
- `logs/nsmvl-stage-state3-gate-force-20260525`
  - 診断的に `0x020C92C0=1` を入れるとstate3へ入るが、`Stage::actorFreezeFlag=0x26` が残りPlayer更新はまだ走らないことを確認。
- `logs/nsmvl-stage-event-92d0-clear-freeze-20260525`
  - `0x020C92D0 |= 0x200` がStageScene state1本来のイベント処理で消費され、host側でfreeze解除とPlayer遷移更新再開が起きることを確認。
- `logs/nsmvl-stage-event-92d0-no-state3-force-20260525`
  - freeze解除イベントだけではPlayer updateと `signalUnlocked()` は走るが、`0x020C92C0 & 3` が立たず、自然なstate3開始には到達しないことを確認。

## 参考

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
