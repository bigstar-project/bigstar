# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` でWAN越しに遊べる形へ持っていく。

過去に試した `LocalMP 2インスタンス * 2プロセス`、savestate共有、試合開始後のWAN切り替え、actor/state強制同期は、通信切断、desync、内部状態不一致、低FPSが大きく、最終方針としては採用しない。

## 現在の方針

US版ROM `roms/nsmb-us.nds` (`A2DE`) を主対象にする。`external/NSMB-Code-Reference` がUS版前提で、関数名・構造体・シンボルを直接活用しやすいため。

本筋は次の2段階。

1. ROMパッチまたは低レベルadapterで、LocalMP接続UIに依存しない `Mario vs Luigi` 専用入口を作る。
2. 試合中にNSMBが読むpacket/input境界をWAN adapterへ差し替え、NSMB側の同期処理をできるだけそのまま使う。

NSMB CentralのMvsL資料では、接続時にRNG seedを同期し、その後は主に入力だけを送ると説明されている。したがって、外から座標やスター状態を強制同期するのではなく、NSMBが読む対戦packetをWAN由来のpacketへ置き換える方向を優先する。

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
  - スクリーンショット
  - RAM dump
  - game state trace
  - calltrace
  - object lifecycle summary
- US版の主要アドレス確認
  - `Game::loadLevel = 0x0200696C`
  - `Net::getPacket = 0x0200EB50`
  - `Net::getConsoleKeys = 0x0200E854`
  - `Net::updatePacket = 0x0201031C`
  - `Net::Core::transferPacket = 0x0200FAE0`
  - `Stage::stageLayout = 0x020CAD40`
- 可視forcedルート
  - `fake-opponent --force-confirm-load --force-loadgame-progress` 系で、1インスタンスのMvsLステージ/HUD/ミニマップ表示まで到達。
  - `--mirror-packets` で `Net::getPacket(consoleID)` がローカル `sendPacket` を返す診断経路を作成。
  - `inputPlayer1Held` にも入力が入ることを確認済み。ただし画面上の2Pが自然に動くところまでは未達。
- 黒画面sessionルート診断
  - `--fake-net-state-on-nickname` とsession flag補完で `Connection interrupted` は回避。
  - `Scene 3` / `stageGroup=9` / player actor / Big Star actor 生成までは到達。
  - ただし黒画面のまま。
  - `StageScene` の主要フィールドは可視ルートと近いが、`stageSceneStateType=0` / `skipFlags=0x05` のままcreate processに残り続ける。
  - `StageScene` と子objectの `state/skipFlags` を強制しても黒画面は解消しない。
  - 可視ルートではStageSceneのprocess linkが成立し、`0204D204` のpostCreate callbackが呼ばれてactive化する。
  - 黒画面sessionルートではStageSceneのprocess link前後関係が欠けており、同callbackを自然に呼べない。直接呼びはdata abortになるため、単純な後付けpostCreateでは解決しない。

## 現在分かっていること

- 可視forcedルートは、試合画面までの自動到達ルートとして使える。
- 黒画面sessionルートは、通信/session状態を自然に再現しようとしているが、StageSceneがprocess listに正しく入らない。現時点では本筋ではなく補助診断扱いにする。
- NSMB側のMvsL同期を使うには、actor座標やスター状態を外から合わせるより、`Net::getPacket` / `Net::getConsoleKeys` / packet tick/action/byte の境界を安定して差し替える方が筋が良い。
- 2Pが自然に動かない現在の問題は、単に同じkeysを返すだけでは不足しており、packet tick/action、local player ID、player mapping、または試合中ready/session状態のどこかがまだ足りない可能性が高い。

## 現在の主な課題

1. 可視forcedルート上で、NSMBが2P入力として実際に参照しているpacket/input境界を確定する。
2. `Net::getPacket` だけで足りるか、`getConsoleKeys` / `getPacketByte` / `getPacketTick` / `getPacketAction` も一貫して差し替える必要があるかを確認する。
3. WAN adapterに渡す最小packet形式を決める。
4. 接続開始時のRNG seed、stage、player ID、character、player maskを固定またはWAN handshakeで同期する。
5. 可視forcedルートから、UI操作なしでMvsL開始できるROMパッチ入口へ整理する。

## 次にやること

1. 可視forcedルートを主軸に戻し、`Net::getPacket` / `getConsoleKeys` / `getPacketByte` / `getPacketTick` / `getPacketAction` の呼び出しをcalltraceで分類する。
2. 1P入力を2P packetへミラーするだけでなく、tick/action/packet byteを旧LocalMP相当の値にそろえる診断パッチを作る。
3. 2P actorが自然に動くかをスクリーンショットとCSVで確認する。
4. 動いたら、ミラー入力をローカル入力ではなくWAN受信入力へ置き換える。

## 検証ルール

- `frame limit reached` だけでは成功扱いにしない。
- 黒画面、prefetch/data abort、通信切断表示、片側だけ進行、HUD不一致、actor不一致は失敗扱い。
- スクリーンショットとCSV/RAM/calltraceの両方で確認する。
- ROM生成物、savestate、巨大ログはgitに含めない。
- docsは古い追記を残し続けず、現在の方針・到達点・課題・次作業がすぐ分かる形に保つ。
