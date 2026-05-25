# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor/state強制同期は、desync・通信切断・低FPS・内部状態不一致が大きく、最終方式としては採用しない。

## 現在の方針

US版ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` がUS版前提で、ROMパッチに必要な関数名・構造体・シンボルを直接使いやすいため。

本筋は次の2段階。

1. ROMパッチまたは低レベルadapterで、LocalMP接続UIに依存しない `Mario vs Luigi` 専用入口を作る。
2. 試合中のpacket/input境界をWAN adapterへ差し替え、NSMB側の同期処理はできるだけそのまま使う。

NSMB Centralの情報では、MvsLは接続時にRNG seedを一度同期し、その後は主に入力packetを通信する。したがって、最終的には「ゲーム重要状態を外から強制同期する」のではなく、「NSMBが読む対戦packetをWAN由来のpacketに置き換える」方向を優先する。

参照: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 実装済み

- US版ROM解析ツール `tools/nsmb_us_rom_tool.py`
  - `symbols9.x` のシンボル解決
  - ARM9/overlayの逆アセンブル
  - 圧縮ARM9/overlayの展開対応
- US版ROMパッチツール `tools/nsmb_us_rom_patch.py`
  - `rng-constant`
  - `direct-mvl-entry`
  - `fake-opponent`
  - `--force-confirm-load`
  - `--force-loadgame-progress`
  - `--mirror-packets`
  - `--fake-net-state-on-nickname`
  - `--force-transfer-result`
  - overlay保存時の `arm9OverlayTable` 更新
- 自動検証フック
  - 入力スクリプト
  - スクリーンショット出力
  - RAM dump
  - game state trace
  - calltrace
  - object lifecycle summary
- US版診断ルート
  - `VSConnect -> VSMenu -> VSStageIntro -> Stage` の到達確認
  - `Game::loadLevel(scene=0x0F, vs=1, group=9, stage=0)` 呼び出し確認
  - `Scene::switchScene(3, settings)` でStage sceneへ入ることを確認
  - `Stage::stageLayout = 0x020CAD40` に修正済み

## 現在分かっていること

- 旧forcedルート `fake-opponent --force-confirm-load --force-loadgame-progress` は、1インスタンスでMvsLステージ/HUD/ミニマップ表示まで到達する。
- `--mirror-packets` で `Net::getPacket(consoleID)` が1P側にもpacketを返すと、`inputPlayer1Held` にも同じ入力が入る。
- ただし、画面上の2Pプレイヤーはまだ自然には動かない。ステージ表示に到達しても、実試合として必要な内部ready/session状態が自然成立しているとは限らない。
- `--fake-net-state-on-nickname` とsession flag補完で `Connection interrupted` は避けられるが、このsessionルートは `Scene 3` / `stageGroup=9` / player actor / Big Star actor 生成後も黒画面になる。
- 黒画面sessionルートでは `StageScene` の主要フィールドは可視ルートとかなり近いが、`stageSceneStateType=0` / `skipFlags=0x05` のまま create process に残り続ける。
- `MELONDS_NSML_FORCE_STAGE_SCENE_ACTIVE` で `StageScene` と子objectの `state/skipFlags` を強制すると、CSV上はactive化するが画面は黒いまま。単純なstate/skipFlags強制では不十分。
- `appSleepControl` を旧ルート値に合わせても、Net状態ブロックを旧ルートからコピーしても、黒画面は解消しなかった。
- calltraceを入口単位に見直した結果、`020A3310` は関数入口ではなく `020A32C0` 内部命令の可能性が高い。現在の本質は `StageScene` がcreate processから抜けないこと。
- object lifecycle summaryにより、可視ルートは `objectSkipUpdate/Render=2` 程度、黒画面sessionルートは `0xE` 程度のobjectがskip状態で止まることを確認した。

## 現在の主な問題

fake-opponent + 強制進行パッチは、複数の通信/session/ready待ちをNOPまたは偽状態で抜けている。そのため、ステージ表示やactor生成に到達しても、実試合として必要な内部状態が自然に成立していない可能性が高い。

最終目標に対しては、actor座標やスター状態を外から同期する方向ではなく、次を特定する必要がある。

- MvsL開始時に本当に必要なsession/ready/seed/packet条件
- `StageScene` がcreate processから抜けるために必要な条件
- 試合中にNSMBが読むpacket/input境界

## 次にやること

1. 可視旧forcedルートを「実験用の最低到達ルート」として維持し、黒画面sessionルートの追跡は原因特定に限定する。
2. `StageScene` がcreate processに残る直接条件を、`Base::processCreate` / create process executor / StageScene onCreate後の戻り値から確認する。
3. ROMパッチ側では、UI/LocalMPを自然再現するより、MvsL開始状態を作る最小入口と、試合中packet境界の差し替えに寄せる。
4. `Net::getPacket` より下の接続/session APIを置換すべきか、試合中packetだけで足りるかを、US版シンボルとcalltraceで切り分ける。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、通信切断表示、片側だけ進行、HUDだけ一致、actor不一致は失敗扱い。
- スクリーンショットとCSV/RAM/calltraceの両方で確認する。
- ROM生成物、savestate、巨大ログはgitに含めない。
- docsは古い追記を残し続けず、現在の方針・到達点・問題・次作業がすぐ分かる形に保つ。
