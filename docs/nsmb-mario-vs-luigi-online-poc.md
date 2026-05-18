# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード `Mario vs Luigi` を、最終的に WAN 越しの `1 EmuInstance * 2PC` で遊べる形にする。

現在の方針は、melonDS の `LocalMP 2インスタンス * 2プロセス` や savestate 依存を最終形から外し、NSMB 側の通信/開始処理を解析して、最初から WAN adapter で成立する経路を作ること。

## 現在の判断

- savestate方式は不採用。表面上のプレイヤー座標が一致しても、動的stage objectや内部状態が一致せず、最終的なWAN対戦の土台にならない。
- DropMP方式も不採用。LocalMP由来の接続/transfer状態を途中で止めると、NSMB側の接続状態やobject状態が崩れる。
- 途中までLocalMPで進めてWANへ切り替える方式は不安定。切替時の停止や状態差分がそのままゲーム上重要なズレになる。
- 本筋は、NSMBが本来持っている入力同期/packet処理を活かしつつ、LocalMP transportだけをWANへ差し替える方向。
- ただし、UI操作なし/LocalMPなしでMvL開始状態を作れるなら、最初からWANへ接続する前処理を大幅に短縮できる。

## 実装済み

- 自動検証フック
  - 入力スクリプト再生
  - screenshot / framebuffer dump
  - MainRAM dump
  - game-state trace
  - ARM call trace
  - A2DJ向けメモリprobe
- NSMB packet系hook
  - `Net::getConsoleKeys()`
  - `Net::getPacketByte()`
  - `Net::getPacketTick()`
  - `Net::getPacketAction()`
  - `Net::Core::transferPacket()`
  - disconnect branch skip
- RNG検証hook
  - `Net::getRandom()`固定/自動patch
  - Big Star actor ID `0x00D2` 周辺のdump/probe
- DirectBoot検証hook
  - `Game::loadLevel` 直接呼び出し
  - VSConnect load-game submenu状態patch
  - VSConnect完了処理 `0x0214E0C0` 呼び出し
  - CourseSelect生成候補 `0x0214F830` 呼び出し

## 重要アドレス

- `Game::loadLevel`: `0x020068A8`
- `VSConnect::createLoadGameSM`: `0x021515B4`
- `VSConnect::updateLoadGameSM`: `0x021512B8`
- `VSConnect::renderLoadGameSM`: `0x0215125C`
- VSConnect完了処理候補: `0x0214E0C0`
- CourseSelect生成候補: `0x0214F830`
- `Net::getConsoleKeys(u16)`: `0x0200E700`
- `Net::getPacketByte(u16,u32)`: `0x0200E978`
- `Net::getPacketTick(u16)`: `0x0200E9BC`
- `Net::getPacketAction(u16)`: `0x0200E9DC`
- packet tick/key buffer: `0x02087F00`
- LocalMP packet slot status: `0x0208AE50`
- LocalMP packet buffer: `0x0208B040 + player * 0x3E`

## 直近の検証結果

- `logs\direct-mvl-boot-vsconnect-loadsm-attempt3`
  - VSConnect object探索は成功。
  - `createLoadGameSM`を外部から直接呼ぶと `ARM9 data abort (020448C0)`。
- `logs\direct-mvl-boot-vsconnect-patchsm-attempt2`
  - LocalMP成功時に近いVSConnect object/Net global状態を書いても、通常updateだけではロードへ進まない。
- `logs\direct-mvl-boot-vsconnect-startload-attempt2`
  - VSConnect完了処理 `0x0214E0C0` から `Game::loadLevel` へ到達。
  - `stageGroup=9` と `vsMode=1` は立つ。
  - ただし画面は「ルイージをさがしています」のまま、CourseSelect/Gameplay objectは生成されない。
- `logs\direct-mvl-boot-course-select-attempt1`
  - CourseSelect生成候補 `0x0214F830` を呼んでも画面遷移しない。
  - 現時点では、検索UIから直接CourseSelect/Gameへ飛ばすにはまだ状態が不足している。

## 現在の blocker

LocalMPなしで、NSMB側に「2人接続済み」「MvL course selectへ進める」状態を作れていない。

`Game::loadLevel`だけでは不十分で、VSConnect/CourseSelect objectの生成、シーン遷移、Net接続グローバル、player slot状態の組み合わせをさらに特定する必要がある。

## 次にやること

1. 実LocalMPで検索画面からCourseSelectへ遷移する瞬間のcall traceを取り、`0x0214F830` の呼び出し元と前提条件を特定する。
2. CourseSelect object `id=0x0005` の生成経路を確定し、LocalMPなしの1インスタンスでCourseSelect表示まで到達させる。
3. CourseSelect表示に到達したら、WAN packet adapterを最初から有効にし、2プロセスでコース選択/試合開始へ進める。
4. 成功判定は画面の見た目だけにしない。切断なし、remote key hit、Stage/Player object生成、主要object状態を検査する。

## 必要なもの

- 検証はユーザー提供の `roms/nsmb.nds` を前提にしている。
- ROM patch化する段階では、日本版 `A2DJ` に対するpatch生成/適用手順を別途作る。
