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

### 実行結果

`roms/nsmb-us-direct-mvl-entry.nds`:

- 起動はする。
- メニュー操作で `Mario Vs. Luigi` を選択するところまでは到達する。
- `updateLoadGameSM` から `Game::loadLevel` を直呼びすると黒画面化し、最終的にprefetch abortになる。
- 1回だけ呼ぶguardとstack alignment修正を入れても改善しない。
- 結論: `VSConnectScene` から `Game::loadLevel` へ直接飛ぶのは、ファイルロード、Scene遷移、session初期化の前提を飛ばしすぎている可能性が高い。

`roms/nsmb-us-loadgame-sm-entry.nds`:

- `updateLoadGameSM` は元のままにして、初期サブメニューだけ `loadGameSME` にした。
- これも `Mario Vs. Luigi` 選択後に黒画面化し、prefetch abortになる。
- 結論: `VSConnectScene::onCreate` 直後に `loadGameSME` へ入れるだけでも早すぎる。通常のselect/search/confirm/char selectでセットされる内部フィールドか通信/session状態が不足している。

主なログ:

- `logs/nsmvl-us-direct-mvl-entry-smoke-20260525`
- `logs/nsmvl-us-direct-mvl-entry-align-menu-20260525`
- `logs/nsmvl-us-loadgame-sm-entry-menu-20260525`

## 現在のブロッカー

LocalMPを使わずにMvLを開始する入口をまだ作れていない。

原因候補:

- `VSConnectScene::loadGameSME` に入る前に、通常フローで初期化されるフィールドが不足している。
- `loadGameSM` 内のファイルロード、通信marker、packet buffer、player/character/stage選択情報の前提が満たせていない。
- `Game::loadLevel` 直呼びでは、MvL専用のfile cacheやstage start関連状態が揃わない。

## 次にやること

1. `VSConnectScene` の通常フローを壊さず、どのサブメニュー遷移で `loadGameSME` に入るのが安全かを追う。
   - `selectModeSME`
   - `charSelectSME`
   - `confirmSME`
   - `waitHostConfirmSME`
   - `loadGameSME`
2. `loadGameSME` に入る直前までにセットされる `VSConnectScene` フィールドを、通常LocalMPルートのRAM/traceから洗い出す。
3. ROMパッチで「不足フィールドを最小限セットしてからloadGameSMEへ入る」入口を試す。
4. それでも重い場合は、`Game::loadLevel` ではなく `CourseSelect -> Game::loadLevel` の既存経路を利用する入口へ切り替える。
5. MvL gameplayに入れた後で、試合中2P入力境界を特定し、WAN adapterと接続する。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、接続切断表示、片側だけ進行は失敗。
- `Mario Vs. Luigi` の試合画面、2P存在、HUD、stage object、スター/アイテム状態、入力反映をスクリーンショットとログで確認する。
- ROM生成物、savestate、古い巨大ログはgitに含めない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
