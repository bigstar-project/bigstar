# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標や描画状態の外部同期は、desync、通信切断、低FPS、内部状態不一致が大きく、最終方式としては採用しない。

## 現在の方針

主対象はUS版ROM `roms/nsmb-us.nds` (`A2DE`)。`external/NSMB-Code-Reference` がUS版前提で、ROMパッチに必要な関数名、構造体、シンボルを直接使えるため。日本版 `A2DJ` はUS版PoC成立後に移植対象とする。

当面の本筋:

1. ROMパッチで、LocalMP接続UIに依存しない `Mario vs Luigi` 専用入口を作る。
2. 試合中の2P入力境界を特定し、WAN adapterからリモート入力を渡す。

重要な制約:

- actor座標、スター位置、freeze flagなどを外部から力技で同期しない。
- NSMB側の試合中ロジックと入力同期処理をできるだけそのまま使う。
- `通信が切断されました`、黒画面、死亡演出だけ、片側だけ進行、HUDだけ一致、actor不一致は成功扱いにしない。
- スクリーンショットとCSV/ログの両方で検証する。

## 現在の到達点

US版ROMパッチの診断ルートで、1インスタンスのまま `VSConnect -> VSMenu -> VSStageIntro -> Stage` まで到達した。

確認済みログ:

- `logs/nsmvl-us-fake-opponent-stage-entry-20260525`

確認できた遷移:

- `VSConnectScene::updateLoadGameSM` の通信/session待ちを診断NOPで抜ける。
- `Scene::switchScene(5, 1)` で `VSMenu` へ入る。
- VSMenu側の転送待ちを診断NOPで抜け、OK入力後にコース選択へ入る。
- `Game::loadLevel(scene=0x0F, vs=1, group=9, stage=0)` が呼ばれる。
- `Scene::switchScene(0x0F, settings)` で `VSStageIntro` へ入る。
- VSStageIntroのReady待ちを診断NOPで抜ける。
- `Scene::switchScene(3, settings)` で `Stage` へ入り、スクリーンショット上でMvsLステージとHUDを確認。

ただし、これはまだ「診断用に複数の待ち条件を強制的に抜けた」状態であり、最終形ではない。次は、これらの待ち条件をWAN adapter/ROM側の自然なsession状態で満たす条件と、試合中2P入力境界を分けて特定する。

## 実装済みツール

- `tools/nsmb_us_rom_tool.py`
  - US版 `symbols9.x` のシンボル解決
  - ARM9/overlayの展開
  - アドレスmapと逆アセンブル
  - ARM9圧縮コードは `ndspy.codeCompression.decompress` 経由で扱う
- `tools/nsmb_us_rom_patch.py`
  - `rng-constant`
  - ARM9/overlay命令パッチ基盤
  - overlay再圧縮時の `arm9OverlayTable` 更新
  - `direct-mvl-entry`
  - `fake-opponent`
  - `fake-opponent --force-confirm-load --force-loadgame-progress`
- `tests/nsmb_us_fake_opponent_load_progress.inputs`
  - US版fake-opponent診断ルート用の自動入力

## 重要シンボル

- `Game::loadLevel = 0x0200696C`
- `Scene::switchScene = 0x020131FC`
- `Scene::prepareFirstScene = 0x020133A4`
- `VSConnectScene::updateLoadGameSM = 0x021577EC`
- `VSConnectScene::getOpponentNickname = 0x021574A4`
- `VSConnectScene::loadGameSME = 0x0215CAF8`
- `VSConnectScene::selectModeSME = 0x0215CB30`

Scene ID:

- `Stage = 3`
- `VSMenu = 5`
- `VSConnect = 6`
- `VSStageIntro = 15`

## 診断パッチで現在外している待ち条件

`fake-opponent --force-confirm-load --force-loadgame-progress` は、現在以下を診断用に強制している。

- `VSConnectScene::getOpponentNickname()` が空の偽NicknameInfoを返す。
- confirm中の `playerLeftSME` 遷移を `loadGameSME` に差し替える。
- `updateLoadGameSM` state1のsession byte待ちを外す。
- `updateLoadGameSM` state3のsession byte待ちを外す。
- `updateLoadGameSM` state4の内部完了待ちを強制的にstate5へ進める。
- `updateLoadGameSM` state5の2プレイヤーready bit待ちを外す。
- `updateLoadGameSM` state6の完了待ちを外す。
- VSMenu post-load transfer待ちを外す。
- VSStageIntro ready待ちを外す。

これらは「どこで止まるか」を見るための診断パッチであり、最終的には必要最小限に戻す。

## 次にやること

1. Stage到達後、1P入力がゲーム内で反映されるかをスクリーンショット/座標ログで確認する。
2. Stage中の2P入力データがどこから読まれているかを特定する。
3. 診断NOPのうち、WAN adapter側で自然に満たすべきsession/ready条件と、ROM側で短絡してよい条件を分類する。
4. 2P入力境界をWAN adapterに接続する最小PoCを作る。
5. 2PC相当のローカルhost/clientで、同一ステージ・同一スター位置・入力反映・通信切断なしを確認する。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、通信切断表示、片側だけ進行は失敗。
- `Ready!` 到達だけでは成功扱いにしない。`Stage` sceneで実際のMvsLステージ/HUD/入力反映を確認する。
- ROM生成物、savestate、古い巨大ログはgitに含めない。

## 参照

- NSMB Central: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi
- `external/NSMB-Code-Reference`
