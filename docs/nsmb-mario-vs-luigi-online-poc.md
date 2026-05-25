# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標や描画状態の外部同期は、desync、通信切断、低FPS、内部状態不一致が大きく、最終方式としては採用しない。

## 現在の方針

主対象をUS版ROM `roms/nsmb-us.nds` (`A2DE`) に切り替える。理由は `external/NSMB-Code-Reference` がUS版を前提にしており、ROMパッチに必要な関数名、構造体、シンボルを直接使えるため。日本版 `A2DJ` はUS版PoC成立後に移植対象とする。

当面の本筋は次の2つ。

1. ROMパッチで、LocalMP接続UIに依存しない `Mario vs Luigi` 専用入口を作る。
2. 試合中の2P入力境界を特定し、WAN adapterからリモート入力を渡す。

重要な制約:

- actor座標、スター位置、freeze flagなどを外部から力技で同期しない。
- NSMB側の試合中ロジックと入力同期処理をできるだけそのまま使う。
- `通信が切断されました`、黒画面、死亡演出だけ、片側だけ進行、HUDだけ一致、actor不一致は成功扱いにしない。
- スクリーンショットとCSV/ログの両方で検証する。

## 完了したこと

- US版ROMのヘッダー確認済み。
  - gamecode: `A2DE`
  - ARM9 ROM offset: `0x4000`
  - ARM9 RAM base: `0x02000000`
  - ARM9 overlay table: `0x62FB0`
- `tools/nsmb_us_rom_tool.py` を追加済み。
  - US版 `symbols9.x` のシンボル解決
  - ARM9/overlayの展開
  - アドレスmapと逆アセンブル
  - ARM9圧縮コードは `ndspy.codeCompression.decompress` 経由で扱う
- `tools/nsmb_us_rom_patch.py` を追加済み。
  - `Net::getRandom()` / `Game::getRandom()` を固定返却へ差し替える `rng-constant`
  - ARM9/overlayの命令パッチ基盤
  - `direct-mvl-entry` の初期PoC
  - overlay再圧縮時に `arm9OverlayTable` の `compressedSize` も更新するよう修正済み
  - 診断用 `fake-opponent` パッチを追加済み
- `roms/nsmb-us-rng100.nds` を生成し、Debug smokeで起動成功を確認済み。
  - 生成ROMは `roms/` 配下なのでgitには含めない。
- `Game::loadLevel` のUS版アドレスを確認済み。
  - `_ZN4Game9loadLevelEtmhhhhhhhhhhhhhhm = 0x0200696C`
- `VSConnectScene` 周辺の主要シンボルを確認済み。
  - `updateLoadGameSM = 0x021577EC`
  - `createLoadGameSM = 0x02157AE8`
  - `onCreate = 0x02158FE8`
  - `loadGameSME = 0x0215CAF8`
- `Scene::prepareFirstScene` の初期TitleScreen指定箇所を確認済み。
  - `0x02013428: mov ip, #4`

## 最新の検証結果

### ROMパッチ基盤

`direct-mvl-entry` で以下の差し替えを生成できる。

- `Scene::prepareFirstScene` の初期sceneを `4(TitleScreen)` から `6(VSConnect)` に変更
- `VSConnectScene::onCreate` の初期サブメニュー literal を `selectModeSME` から `loadGameSME` に変更
- 任意で `VSConnectScene::updateLoadGameSM` を `Game::loadLevel(scene=0x0F, vs=1, group=9, stage=0, playerID=0, playerMask=3, rngSeed=0x100)` 直呼びスタブへ差し替え

逆アセンブル上のパッチ生成は成功している。

overlayを再圧縮するパッチでは、overlay file本体だけでなくoverlay tableの `compressedSize` も更新する必要がある。これを更新していなかったため、初期のoverlay52パッチROMは黒画面/prefetch abortを起こしていた可能性が高い。現在の `save_overlays()` は `ndspy.code.saveOverlayTable()` でtableも更新する。

### 実行結果

`roms/nsmb-us-direct-mvl-entry.nds`:

- 起動はする。
- メニュー操作で `Mario Vs. Luigi` を選択するところまでは到達する。
- `updateLoadGameSM` から `Game::loadLevel` を直呼びすると黒画面化し、最終的にprefetch abortになる。
- 1回だけ呼ぶguardとstack alignment修正を入れても改善しない。
- 結論: `VSConnectScene` から `Game::loadLevel` へ直接飛ぶのは、ファイルロード、Scene遷移、session初期化の前提を飛ばしすぎている可能性が高い。

`roms/nsmb-us-loadgame-sm-entry.nds`:

- `updateLoadGameSM` は元のままにして、初期サブメニューだけ `loadGameSME` にした診断ROM。
- overlay table修正前の結果は黒画面/prefetch abortだったため、古い結果は信頼しない。
- ただし `VSConnectScene::onCreate` 直後に `loadGameSME` へ入る方針は、通常のselect/search/confirm/char selectでセットされる内部フィールドを飛ばすため、現時点では本筋から外す。

US vanilla通常入力:

- `tests/nsmb_mario_vs_luigi.inputs` で `Mario Vs. Luigi` を選択し、`selectModeSME -> charSelectSME -> searchSME` まで自然に到達する。
- LocalMP peerがいないため `Searching for Luigi...` で待つ。
- call traceで確認した通常遷移:
  - `745`: `changeSubMenu(selectModeSME)`
  - `900`: `scheduleSubMenuChange(charSelectSME)`
  - `932`: `changeSubMenu(charSelectSME)`
  - `1260`: `scheduleSubMenuChange(searchSME)`
  - `1290`: `changeSubMenu(searchSME)`

`roms/nsmb-us-fake-opponent.nds`:

- `VSConnectScene::getOpponentNickname()` を、overlay内の長さ0 nickname構造を返すようにパッチ。
- `searchSME -> confirmSME` までは進む。
- 実通信/sessionがないため、confirm後すぐ `playerLeftSME` に落ちる。

`roms/nsmb-us-fake-opponent-load.nds`:

- `fake-opponent` に加え、confirm中の `playerLeftSME` literalを `loadGameSME` に差し替え。
- `loadGameSME` へ入り、画面は `Please wait.` になる。
- `updateLoadGameSM` は毎フレーム走るが、`Game::loadLevel` にはまだ到達しない。
- RAM dumpでは `VSConnectScene+0x16C=1`、`+0x170` のtimerだけが増える。
- つまり `updateLoadGameSM` state1内の通信/session条件が未成立。

`roms/nsmb-us-fake-opponent-load-progress.nds`:

- `updateLoadGameSM` state1の待ち分岐を診断用に2箇所NOP化。
  - `0x021578B0`
  - `0x021578D0`
- まだ `Please wait.` から進まず、`Game::loadLevel` 呼び出しは未確認。
- 次はstate1を抜けた後の追加条件、またはstate遷移自体が起きていない原因をRAM dumpで追う。

主なログ:

- `logs/nsmvl-us-vanilla-vsconnect-calltrace-20260525`
- `logs/nsmvl-us-fake-opponent-fixed-ovt-20260525`
- `logs/nsmvl-us-fake-opponent-load-20260525`
- `logs/nsmvl-us-fake-opponent-load-progress2-20260525`
- `logs/nsmvl-us-fake-opponent-load-progress-ram-20260525`

## 現在のブロッカー

LocalMPを使わずにMvLを開始する入口をまだ作れていない。

原因候補:

- `loadGameSM` 内の通信/session byteやmarker条件が未成立。
- `fake-opponent` はUI上の相手検出だけを偽装しており、Net packet buffer、marker、player ready、random syncまでは偽装していない。
- `Game::loadLevel` 直呼びでは、MvL専用のfile cacheやstage start関連状態が揃わない。

## 次にやること

1. `fake-opponent-load-progress` のRAM dumpを読み、`updateLoadGameSM` がstate1から進まない理由を確定する。
2. `updateLoadGameSM` state2以降の条件を、ROMパッチで順に診断NOP/固定値化して `Game::loadLevel` まで到達できるか確認する。
3. forceしている条件を、最終的にWAN adapterから自然に満たすべき条件と、ROM側で固定してよい条件に分類する。
4. MvL gameplayに入れた後で、試合中2P入力境界を特定し、WAN adapterと接続する。
5. US版PoC成立後に日本版 `A2DJ` へ移植する。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、接続切断表示、片側だけ進行は失敗。
- `Mario Vs. Luigi` の試合画面、2P存在、HUD、stage object、スター/アイテム状態、入力反映をスクリーンショットとログで確認する。
- ROM生成物、savestate、古い巨大ログはgitに含めない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
