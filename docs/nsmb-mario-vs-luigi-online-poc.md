# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor座標や外部状態の強制同期は、desync、通信切断、低FPS、内部状態不一致が大きく、最終方式としては採用しない。

## 現在の方針

主対象はUS版ROM `roms/nsmb-us.nds` (`A2DE`)。`external/NSMB-Code-Reference` がUS版前提であり、ROMパッチに必要な関数名・構造体・シンボルを直接使えるため。

本筋は次の2段階。

1. ROMパッチで、LocalMP接続UIに依存しない `Mario vs Luigi` 専用入口を作る。
2. 試合中のpacket/input境界をWAN adapterへ差し替え、NSMB側の同期処理はできるだけそのまま使う。

NSMB Centralの情報では、MvsLはRNG seedを接続時に一度同期し、その後は入力情報中心で通信する。したがって、最終的には「NSMBが読む対戦packetをWAN由来のpacketに置き換える」方向を優先する。

参考: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/mario-vs-luigi

## 実装済み

- US版ROM解析ツール `tools/nsmb_us_rom_tool.py`
  - `symbols9.x` のシンボル解決
  - ARM9/overlayの逆アセンブル
  - 圧縮ARM9を `ndspy.codeCompression.decompress` 経由で扱う
- US版ROMパッチツール `tools/nsmb_us_rom_patch.py`
  - `rng-constant`
  - `direct-mvl-entry`
  - `fake-opponent`
  - `fake-opponent --force-confirm-load --force-loadgame-progress`
  - `fake-opponent --mirror-packets`
  - overlay再保存時に `arm9OverlayTable` を更新
- 自動検証フック
  - 入力スクリプト
  - スクリーンショット出力
  - RAMダンプ
  - calltrace
  - game state trace
- US版診断ルート
  - `VSConnect -> VSMenu -> VSStageIntro -> Stage` 到達を確認
  - `Game::loadLevel(scene=0x0F, vs=1, group=9, stage=0)` 呼び出しを確認
  - `Scene::switchScene(3, settings)` でStage sceneに入ることを確認

## 現在分かっていること

- 1インスタンス診断ROMでMvsLステージ/HUD/ミニマップ表示までは到達する。
- `Input::update`、`Input::updatePlayerInput`、StageSceneの `onUpdate` はStage到達後も毎フレーム呼ばれている。
- 入力スクリプトの `RIGHT` は `Input::consoleKeys` と `Input::playerKeysHeld` まで届いている。
- `--mirror-packets` で `Net::getPacket(consoleID)` が2P側にもpacketを返すようにした場合、`inputPlayer1Held` にも同じ入力が入る。
- それでも画面上のプレイヤーは動かない。現在のfake-opponentルートは、ステージ表示には届くが、実試合として必要な内部状態がまだ自然に成立していない可能性が高い。
- `--fake-net-state` を `Net::getPacket` 内で常時適用すると、起動初期からNet状態を書き換えて白画面で固まる。
- `--fake-net-state-on-nickname` で検索後だけNet状態を2台接続済みに見せると、`Connection interrupted` へ落ちる。calltraceでは `Net::Core::setConnectionState(3)` と `Net::Core::transferPacket(1)` の直後に切断系の流れへ入る。
- RAM上のBig Star検出はruntime class ID `0x22` / settings `1` で拾えている。NSMB CentralのObject ID 210とは表記レイヤーが違う可能性がある。

## 現在の主な問題

fake-opponent + 強制進行パッチは、複数の通信/session/ready待ちをNOPで抜けている。そのため、ステージ表示まで行けても、実際の試合開始状態としては不完全な可能性がある。

特に次を確認する必要がある。

- NSMBが試合中に必要とするsession/ready/player状態のうち、どれを自然に満たすべきか
- `Net::getPacket` だけで足りるのか、それより下の接続/session境界もadapter化する必要があるのか
- Stage内でプレイヤーが描画・操作可能にならない直接原因
- Big Star、8コインアイテム、ステージランダム選択などのRNG seed同期方法

## 次にやること

1. `fake-opponent --mirror-packets` の結果を基準に、Stage内の「入力は届くがプレイヤーが動かない」原因を特定する。
2. `Connection interrupted` へ落ちる直接原因を、`Net::Core::transferPacket` / `setConnectionState` / error handler周辺から特定する。
3. `VSConnectScene::updateLoadGameSM` のNOPを減らし、可能な限りNet/Wifi/sessionグローバルを自然な値にして進める診断ROMを作る。
4. `Net::getPacket` より下のsession/packet境界を特定し、WAN adapterの差し替え点を決める。
5. 1インスタンスで「操作可能なMvsL試合」へ到達できたら、WAN由来の2P packetを流す最小PoCへ進む。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、通信切断表示、片側だけ進行、HUDだけ一致、actor不一致は失敗扱い。
- スクリーンショットとCSV/RAM/calltraceの両方で検証する。
- ROM生成物、savestate、巨大ログはgitに含めない。
- docsは古い追記を残し続けず、現在の方針・到達点・問題・次作業がすぐ分かる形に保つ。
