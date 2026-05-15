# NSMB Mario vs Luigi Online PoC

## Current Status

- 方針は、汎用DSローカル通信netplayではなく、NSMB Mario vs Luigi専用の同期・解析・必要ならROM/メモリパッチへ寄せている。
- 日本版ROM `A2DJ` を対象に、`Net::random` 主要シンボルと `Net::getRandom()` 周辺関数を優先解析中。
- `tests/nsmb_mario_vs_luigi_star_probe.inputs` で、Mario vs Luigi開始後にinst0/Marioが最初のスターを取り、次スターを再生成するところまで自動化した。
- `tools/nsmb_mvl_ram_probe.py --a2dj-rng-timeline --rng-timeline-only ...` で、MainRAM dump列から `Net::randomCallCount` / `Net::random.value` / `Net::randomBranchAddress` の遷移を抽出できる。
- 直近の検証では、スター取得後の次スター生成で frame `005071` に `Net::randomCallCount` が `0x92 -> 0x93` に進み、`Net::random.value` が `0x413B3BAA -> 0xF9D72FCA` に変化した。`Net::randomBranchAddress=0x0212D41C` なので、関連する呼び出し元は `0x0212D418` 周辺と見ている。
- `0x0212D418` 周辺の逆アセンブルで、ここがBig Starの空き配置スロット選択処理だと確認した。同じ入力ルートの2回目でも frame `005071` のRNG値と frame `006400` の次スター位置が再現した。
- frame `5000` に `Net::random.value` の4バイトだけを `0x12345678` へメモリパッチすると、次のRNG出力が `0x7544F5D5` に変わった。Big Star再生成は共有 `Net::random` streamに従っている。
- `MELONDS_NSML_NET_RANDOM_FRAME` / `MELONDS_NSML_NET_RANDOM_VALUE` を追加し、MainRAMパッチファイルなしで両インスタンスの `Net::random.value` を同一値へ注入できるようにした。frame `5000` で `0x12345678` を注入する検証は成功。

## Current Blockers / Next Actions

1. 8コインアイテム取得スクリプトを追加し、スター以外のランダム消費も同じtimelineで確認する。
2. MvsLロード直後のより早いフレームで `Net::random.value` を注入し、スター初期位置から安定するか確認する。
3. `0x0212D418` のBig Star選択処理はROMパッチ候補として保持するが、まずはmatch seed固定を優先する。

## 方針転換: NSMB特化解析・パッチ方向

2026-05-14 時点で、melonDS側だけでDSローカル通信を完全決定化する方向は、最終的なWAN越し2PC対戦の安定化に対してリスクが高いと判断する。

理由:

- 外側のframe barrier / serial runでは、`RunFrame()`内部のWi-Fi/LocalMP送受信待ちを制御できない
- LocalMP timeout調整、strict wait、timestamp単純固定ではスター位置や試合状態の再現性を安定させられなかった
- 差分の初期地点はARM7側の通信/OSワーク領域で、汎用DSローカル通信の決定性問題へ広がっている
- 最終目標は汎用DS netplayではなく、NSMB Mario vs Luigiだけが安定して遊べること

今後の基本方針:

1. melonDSフォークは、入力注入、スクリーンショット検証、MainRAM dump、write watch、メモリパッチ実行基盤として使う。
2. NSMB側のMario vs Luigi試合開始状態、スター生成、RNG seed、プレイヤー/オブジェクト状態を解析する。
3. まずROM改造ではなく、melonDS側のテスト用メモリパッチでスター位置やRNG候補を固定できるか試す。
4. 成功したら、入力同期netplayと結合する。
5. melonDS側メモリパッチで安定することを確認してから、必要に応じてROMパッチ化を検討する。

当面の調査対象:

- 手元ROMは `A2DJ` 日本版、公開Code ReferenceはUS版向けなので、USシンボルをJP向けに移植して扱う
- `docs/nsmb-a2dj-symbol-port.md` の優先シンボルをベースに、JP側の `Net::random` / `Stage::getRandom()` / actor symbolsを確定する
- JP版のまま進めるため、関数アドレスはGDB/Ghidraまたはシグネチャで個別に確定する
- frame 5000付近のスター位置差分をMainRAM dumpから特定する。ただし `0x0208xxxx` 近辺の差分は現時点ではスター位置そのものと断定しない
- スター生成だけでなく、8コイン時のランダムアイテムも `Net::random` / `Stage::getRandom()` 経由の同期対象として追う
- 同一入力で異なるスター位置になる2実行間に対して、候補アドレスの値を比較する
- 候補アドレスをmelonDS側から固定・コピーして、画面上のスター位置が揃うか確認する

## Web調査: 既存のMvsL解析情報

NSMB Central Wiki の Mario vs Luigi ページに、今回の方針に直結する情報があった。

重要点:

- MvsL modding は通常のシングルプレイlevel moddingより難しい
- MvsLでは Random Number Generator seed が接続確立時に一度だけ同期される
- Gameplay中は入力だけが送られる
- そのため、コードは両プレイヤー/両コンソールの状態を考慮する必要がある
- ローカルコンソール側のplayer変数だけを読むような実装は、各DSで違う値を読み、desyncを起こす
- Vanilla互換objectとして `Object ID 210: MvsL Big Star` が明記されている

この情報は、こちらの検証結果と一致している。つまり、NSMB MvsL自体が「接続時seed同期 + gameplay中入力同期」前提で動いており、スター位置やオブジェクト挙動を安定させるには、RNG seed、Big Star actor、両プレイヤー状態参照を直接追うのが有効。

参考:

- NSMB Central Wiki: Mario vs Luigi  
  https://bookstack.nsmbcentral.net/books/new-super-mario-bros-ds/page/mario-vs-luigi
- NSMB Central Wikiには Code Modification、GDB with Ghidra and melonDS、NCPatcher などのページもあり、ROM/コードパッチ方向の調査基盤として使える

## NSMB Code Reference 調査

2026-05-15 時点で、公開されている `MammaMiaTeam/NSMB-Code-Reference` を `external/NSMB-Code-Reference` に取得して調査した。これはローカル解析用の参照で、`external/` は `.gitignore` に入れてコミット対象外にする。

重要な発見:

- 参照リポジトリは New Super Mario Bros. DS の US 版向け。
- 手元の `roms/nsmb.nds` は ROM ヘッダ上 `A2DJ` で、日本版。
- そのため、Code Reference の固定アドレスをこの ROM の RAM dump にそのまま当てることはできない。
- ただし、関数名・構造体・同期設計の情報は有用。特に `Net::random`、`Net::getRandom()`、`Net::syncRandomFull()`、`Net::syncRandomFast()`、`Net::Core::shareRandomSeed()`、`Game::random`、`Stage::getRandom()` が直接関係する。
- `Stage::getRandom()` は `Net::getRandom()` を使うため、ステージ中のスター生成だけでなく、8コイン時のランダムアイテムも同じ同期対象として扱う必要がある。
- `Item` actor には `SpawnItem` / `ItemType` があり、8コイン時アイテム検証では `ItemType` や `spawnItem` の状態も追う必要がある。

次の判断:

- ユーザー方針により、日本版 `A2DJ` のまま進める。
- USシンボルをJPへ移植し、Ghidra/melonDS GDBまたはシグネチャでJP側の対応関数を特定する。
- どちらの場合も、最終的な同期対象は「スター位置」だけではなく、`Net::random` / `Stage::getRandom()` を経由するランダムイベント全体。

追加した解析補助:

- `tools/nsmb_mvl_ram_probe.py`
  - ROM gamecode を表示し、US版シンボルとROM地域が違う場合に警告する。
  - `--a2dj-symbols` で、現時点で移植した日本版優先シンボルの値をRAM dumpから表示する。
  - RAM dump内の `0x00d2`、つまり MvsL Big Star actor ID 候補を抽出する。
  - 既存dump同士のobject-like record差分を出す。
- `docs/nsmb-a2dj-symbol-port.md`
  - 日本版 `A2DJ` 向けに移植済み/候補のシンボルを整理した。

検証結果:

- `python tools\nsmb_mvl_ram_probe.py --rom roms\nsmb.nds --us-symbols logs\ram-gamepatch-a\inst0_frame005000_mainram.bin logs\ram-gamepatch-b\inst0_frame005000_mainram.bin` は成功。
- ROMは `A2DJ` と判定され、US版シンボルは要移植と警告された。
- `python tools\nsmb_mvl_ram_probe.py --rom roms\nsmb.nds --a2dj-symbols logs\ram-gamepatch-a\inst0_frame005000_mainram.bin logs\ram-gamepatch-a\inst1_frame005000_mainram.bin` は成功。
- `Game` / `Stage` グローバル群はUS版から `-0x9C0`、`Net` グローバル群はUS版から `-0x9E0` のshiftで、MvsL route RAM dumpと整合した。
- Net系の優先関数はUS版から `-0x154` のshiftで、JP版の復号済みARM9コードと整合した。
- `Net::getRandom()` は `0x0200E5A0`、`Net::getRandom12()` は `0x0200E550`、`Net::syncRandomFull()` は `0x0200E5E8`、`Net::syncRandomFast()` は `0x0200E5F4`、`Net::Core::shareRandomSeed()` は `0x02010F04`。
- 復号済みRAM上で `Net::getRandom()` への ARM `BL` call site は 61 個見つかった。Big Star / Item actor に近い呼び出し元を次に絞る。
- `MELONDS_NSML_RANDOM_TRACE` を追加し、A2DJ `Net::getRandom()` 入口で `caller`、`Net::random.value`、`Net::randomCallCount` をCSVログ化できるようにした。
- `MELONDS_NSML_RANDOM_TRACE` 有効時はJITを自動で無効化する。
- `Game::localPlayerID` は inst0 が `0`、inst1 が `1` で、`Game::stageGroup` は `9`、`Game::vsMode` は `1`。
- `Net::ggid` はJPの game group id `0x42`、`Net::random.value` 候補は inst0/inst1 で同一値。
- `Game::random.value`、`Net::random.value`、`Net::randomBranchAddress` のwatchでは、現在の自動ルート中に書き込みは出なかった。これは候補否定ではなく、乱数共有/初期ランダム化がwatch対象期間外で終わっているか、現在の入力スクリプトが追加ランダムイベントを起こしていない可能性がある。
- 次の検証では、8コインアイテムまたはスター取得後の再生成を意図的に起こす入力スクリプトが必要。
- `0x00d2` は RAM dump 内で複数ヒットしたが、現時点では「どれが実際のMvsL Big Star actor instanceか」は未確定。
- `0x0208xxxx` 近辺の差分は引き続き見えるが、100フレーム刻みdumpでは値がフレーム経過に追従して変わるため、スター位置そのものと断定しない。

ランダム要素の扱い:

- 事前選択のランダム性と、試合中gameplayのランダム性を分けて扱う。
- ランダムステージ選択のような事前選択は、乱数状態ではなく選択結果そのものを固定する。
- Big Star配置、8コインアイテム、tile/object randomization、actor effect/drop系は `Stage::getRandom()` / `Net::getRandom()` に集約して同期する。
- 最終的には個別のスター/アイテム座標を毎回固定するのではなく、`Net::random` のseedと消費順を一致させる方針。
- 61箇所の `Net::getRandom()` 呼び出し元分類は、全部を個別制御するためではなく、乱数消費順がズレたときにどのゲームシステムが余計に/不足して乱数を消費したかを特定するため。

## 目的

New Super Mario Bros. DS のローカル対戦専用モード「Mario vs Luigi」を、melonDS フォーク上でオンライン対戦できる形にする。

狙う方式は、DS のローカル無線通信そのものを WAN 越しに中継する方式ではなく、各 PC 上で 2 台分の DS を起動し、PC 内の Local MP で Mario vs Luigi を成立させたうえで、ネットワーク越しにはプレイヤー入力だけを同期する方式。

```text
Host PC:
  inst0: Mario 側 DS  <- Host 入力
  inst1: Luigi 側 DS  <- Client 入力をネット越しに注入

Client PC:
  inst0: Mario 側 DS  <- Host 入力をネット越しに注入
  inst1: Luigi 側 DS  <- Client 入力
```

## 現在の結論

入力同期だけではまだ対戦成立と言えない。最大の未解決点は、Mario vs Luigi の試合状態、特にスター位置などのランダム要素が実行間で一致しないこと。

2026-05-14 時点の検証では、同じ ROM、同じ入力スクリプト、固定 RTC、同じ Wi-Fi sync 値でも、2 回の実行でスター位置が変わった。

- `logs/screens-state-save-5100b/inst0_frame005000.png`: スターが画面左寄り
- `logs/screens-state-save-5100c/inst0_frame005000.png`: スターが下画面中央付近
- `logs/nsmb-state-save-5100b.stdout.txt` と `logs/nsmb-state-save-5100c.stdout.txt` の Wi-Fi sync 値はどちらも `0000000000443928`
- hash は frame 1200 までは一致し、frame 1400 以降に差分が出始めた

つまり、NSMB の RNG seed を melonDS の RTC だけで固定できる状態ではない。現在の 2 EmuThread + LocalMP の非同期実行タイミングが、ゲーム内部状態や通信タイミングに影響している可能性が高い。

## 実装済み

- Visual Studio 2022 Build Tools + Clang toolset で `debug-windows-x86_64` ビルドが通る
- `build/debug-windows-x86_64/melonDS.exe` が生成される
- 起動時クラッシュは `GPU::SetRenderer()` 初期化順を修正して解消済み
- `src/frontend/qt_sdl/NsmbNetplayPoC.*` を追加し、入力同期 PoC と自動検証フックを実装
- 1 プロセス内で 2 つの `EmuInstance` を起動する smoke test を追加
- Mario vs Luigi 到達用の入力スクリプトを追加
- スクリーンショット出力フックを追加
- full RAM hash ログ出力を追加
- route 到達後に ENet 入力同期へ接続する PoC を追加
- 固定 RTC フック `MELONDS_NSML_FIXED_RTC` を追加
- savestate 保存/ロード用テストフックを追加
- Local MP 共有キューを `localmp.bin` として保存/復元するテストフックを追加
- savestate ロード時に 2 台の DS 状態をロードし終えるまで待つバリアを追加
- `MELONDS_NSML_FRAME_BARRIER` によるテスト用フレーム境界バリアを追加
- MainRAM ダンプフックを追加

## 検証コマンド

### 1 インスタンス smoke

```powershell
.\scripts\run-nsmb-smoke.ps1 -Frames 180
```

### 2 プロセス入力同期 smoke

```powershell
.\scripts\run-nsmb-netplay-smoke.ps1 -Frames 180 -Port 8065
```

### 2 EmuInstance smoke

```powershell
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180
```

### Mario vs Luigi route smoke

```powershell
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200
```

### route + netplay smoke

```powershell
.\scripts\run-nsmb-mvl-netplay-route-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8070
```

## テスト環境変数

- `MELONDS_NSML_TEST=1`: 自動検証フックを有効化
- `MELONDS_NSML_TEST_INSTANCES`: 検証対象インスタンス数
- `MELONDS_NSML_TEST_FRAMES`: 自動終了フレーム
- `MELONDS_NSML_INPUT_SCRIPT`: 入力スクリプト
- `MELONDS_NSML_HASH_LOG`: hash CSV 出力先
- `MELONDS_NSML_HASH_INTERVAL`: hash 出力間隔
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG 出力先
- `MELONDS_NSML_SCREENSHOT_INTERVAL`: PNG 出力間隔
- `MELONDS_NSML_STATE_SAVE_DIR`: savestate 保存先
- `MELONDS_NSML_STATE_SAVE_FRAME`: savestate 保存フレーム
- `MELONDS_NSML_STATE_LOAD_DIR`: savestate 読み込み元
- `MELONDS_NSML_STATE_LOAD_FRAME`: savestate 読み込みフレーム。`0` なら起動直後の最初の `RunFrame()` 前
- `MELONDS_NSML_FIXED_RTC`: `YYYY-MM-DDTHH:MM:SS` 形式で RTC を固定
- `MELONDS_NSML_FRAME_BARRIER=1`: テスト時に複数インスタンスをフレーム境界で待ち合わせる
- `MELONDS_NSML_SERIAL_RUN=1`: テスト時に `inst0 frame N -> inst1 frame N -> ...` の順で外側から `RunFrame()` を直列化
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump 出力先
- `MELONDS_NSML_RAM_DUMP_INTERVAL`: MainRAM dump 出力間隔
- `MELONDS_NSML_RAM_DUMP_FRAMES`: `1348-1362,1500` のような指定フレーム/範囲だけをMainRAM dump
- `MELONDS_NSML_WATCH_ADDR`: MainRAM write watch 対象アドレス
- `MELONDS_NSML_WATCH_LEN`: watch 範囲長
- `MELONDS_NSML_WATCH_START_FRAME` / `MELONDS_NSML_WATCH_END_FRAME`: watch対象フレーム範囲
- `MELONDS_NSML_DISABLE_JIT=1`: JITを無効化。`MELONDS_NSML_WATCH_ADDR` 指定時も自動でJIT無効化
- `MELONDS_NSML_LOCALMP_LOG_TIMEOUTS=1`: Local MP の受信/返信待ち timeout をログ出力
- `MELONDS_NSML_LOCALMP_STRICT_WAIT=1`: Local MP の block wait を長めに待つ実験モード
- `MELONDS_NSML_LOCALMP_STRICT_WAIT_MS`: strict wait の最大待ち時間
- `MELONDS_NSML_LOCALMP_FIXED_TIMESTAMP`: Local MP packet timestamp を固定する実験モード

## savestate 方式の検証結果

### DS 本体の savestate だけ

`inst0.mln` / `inst1.mln` のみを保存してロードすると、片方のインスタンスだけが進み、もう片方が無線待ちで止まった。

原因候補は、melonDS の DS 本体 savestate には Local MP の共有キュー、read/write offset、セマフォ状態が入っていないこと。

### Local MP 共有状態も保存した場合

`inst0.mln` / `inst1.mln` / `localmp.bin` の 3 点セットでロードすると、2 台とも frame 300 までは進んだ。

ただし画面は「通信が切断されました」になった。保存時点の DS 状態と Local MP 共有キューが完全に同じ瞬間を表していない可能性が高い。

さらに保存側にバリアを追加したが、片方を保存フレームで止めるともう片方がその先の無線パケットを待って到達できず、5 秒でタイムアウトした。現行の 2 EmuThread 実行のままでは、通信中の状態を完全一致で切り出すのは難しい。

## フレームバリア方式の検証結果

`MELONDS_NSML_FRAME_BARRIER=1` で、2 つの `EmuThread` が同じフレーム境界で `RunFrame()` に入る/出るようにした。

結果:

- Wi-Fi sync 直後の frame 1571 付近で before/after barrier timeout が連続発生した
- Local MP は `RunFrame()` 中に相手の応答を待つため、単純なフレーム境界バリアは通信中の細粒度な待ち合わせと相性が悪い
- したがって「既存の 2 スレッドをフレーム単位で待たせる」だけでは決定性固定として不十分

この結果から、必要なのは単純な barrier ではなく、Local MP の送受信タイミングを含めて制御する専用 coordinator か、通信状態が完全に安定した地点からの別アプローチ。

## Local MP 決定性実験

### timeout 観測

`MELONDS_NSML_LOCALMP_LOG_TIMEOUTS=1` で frame 1600 まで確認した。

結果:

- 検証範囲では Local MP の受信/返信 timeout は発生しなかった
- したがって「25ms timeout が発生するかどうか」だけがスター位置ズレの主原因ではない

### strict wait

`MELONDS_NSML_LOCALMP_STRICT_WAIT=1` で受信待ちを長めにした。

結果:

- 再現性は改善しなかった
- 実行によっては同期成立タイミングが大きくズレ、bad channel ログが増えた
- 既存の非同期 `EmuThread` 実行のまま待ち時間だけ伸ばしても安定しない

### fixed timestamp

`MELONDS_NSML_LOCALMP_FIXED_TIMESTAMP=0x443928` で Local MP packet timestamp を固定した。

結果:

- 2回実行で画面は一致したが、frame 5000 時点でも「ルイージをさがしています」から進まず、Mario vs Luigi の試合に到達しなかった
- timestamp は通信成立に使われる値なので、単純固定は通信そのものを壊す

### serial run

`MELONDS_NSML_SERIAL_RUN=1` で外側から `inst0 frame N -> inst1 frame N` の順に `RunFrame()` を直列化した。

結果:

- Wi-Fi sync 直後の frame 1571 付近で timeout
- `inst0` が frame 1571 に進もうとしている一方、serial coordinator は `inst1 frame 1570` の完了を待っていた
- これは `RunFrame()` 内部でWi-Fi/Local MPの送受信待ちが発生しているため、外側のフレーム単位直列化では粒度が粗すぎることを示す

結論:

- 外側からの frame barrier / serial run / wait timeout 調整 / timestamp単純固定では不十分
- 必要なのは、Local MP の packet dispatch を「どのDSがどの時点で送受信できるか」まで決定的にする仕組み
- あるいは、DS通信成立後ではなく、NSMB側の試合状態生成処理を特定してゲーム状態を直接揃える方向

## MainRAM 差分調査

`MELONDS_NSML_RAM_DUMP_DIR` / `MELONDS_NSML_RAM_DUMP_INTERVAL` を追加し、同一条件の短距離2回実行を比較した。

条件:

- 同一 ROM
- 同一入力スクリプト
- `MELONDS_NSML_FIXED_RTC=2026-01-01T00:00:00`
- frame 1500 まで実行
- 100 frame ごとに MainRAM dump

結果:

- frame 1400 までは `inst0` / `inst1` ともに MainRAM が完全一致
- frame 1500 で `inst1` のみ 38 byte 差分
- `inst0` は frame 1500 でも完全一致
- 主な差分範囲:
  - `0x02084C90`
  - `0x0208F3D4-0x0208F3D7`
  - `0x020921F0-0x020921FD`
  - `0x0209232C-0x0209232F`
  - `0x020923F8-0x020923FB`
  - `0x02092424-0x0209242B`
  - `0x020927F4-0x020927FC`

差分はポインタ/リスト構造のように見える。現時点では「単一の RNG seed だけを melonDS 側から固定すれば解決」とはまだ言えない。

追加で `MELONDS_NSML_RAM_DUMP_FRAMES=1348-1362` を使って、より細かく比較した。

- `inst1` の最初の hash 差分: frame 1355
- `inst1` MainRAM は frame 1354 まで完全一致
- frame 1355 で最初に差分が出た MainRAM offset: `0x3F82BC`
- DS アドレス換算: `0x023F82BC`
- frame 1355 の初期差分例:
  - run A: `0x023F82BC = 01 00 00 00`
  - run B: `0x023F82BC = 00 00 00 00`
  - run B では近傍 `0x023F82C4` に `48 91 7F 02` が入る

`0x023Fxxxx` 近辺は MainRAM 末尾側で、通常のゲームロジック変数というより通信/ワーク領域/ヒープ構造の可能性がある。次はこの書き込み元を追う必要がある。

MainRAM write watch の結果:

- dump offset `0x3F82BC` は、CPUからは主に `0x027F82BC` のMainRAMミラーとして書かれていた
- 書き込みCPUは ARM7
- 代表的な書き込み元PC:
  - `0x0238014C`
  - `0x037FCD04-0x037FCD28`
  - `0x037FCA3C`
  - `0x037FCA50`
- この差分はNSMBのスター/RNG本体というより、ARM7側の通信/OSワーク領域の揺れを示している可能性が高い

したがって、現在の優先仮説は「NSMBのRNG seedを直接固定すれば済む」ではなく、「Local MP / ARM7通信処理のタイミング揺れが、最終的に試合開始状態やスター位置へ波及している」。

## 現在のブロッカー

1. 同一 PC 上の同一条件 2 回実行でも、Mario vs Luigi のスター位置が一致しない。
2. RTC 固定、同一 MAC、同一 Wi-Fi sync 値だけでは RNG 要素が固定できない。
3. savestate ペア方式は Local MP 共有状態を含めても、通信中状態の完全復元に失敗している。
4. 現行の複数 `EmuThread` による非同期進行では、入力フレームを揃えても通信タイミングが揺れる可能性がある。
5. 外側のフレーム境界制御では、`RunFrame()` 内部のWi-Fi送受信待ちを制御できない。

## 次にやること

優先度順:

1. `EmuInstance` を個別スレッドで自由走行させず、Mario/Luigi の 2 台を 1 つの coordinator が決定的に進める方式を検討する。
2. `RunFrame()` の外側ではなく、Wifi/LocalMP の送受信境界で協調できるフックを設計する。
3. ARM7側の通信/OSワーク領域の揺れが、どの時点でARM9側の試合状態へ伝播するかを追う。
4. Local MP の packet queue を、受信側の現在channelやMP状態を考慮して決定的にdispatchできるか検証する。
5. 必要なら ROM 改造ではなく melonDS 側のメモリパッチで、NSMB の RNG seed 候補を固定できるか試す。

## ROM / ユーザー依存

現在の検証は `roms/nsmb.nds` が存在する前提で動いている。追加の ROM 提供は現時点では不要。

## コミット履歴メモ

- `27b71d34 test: add NSMB netplay smoke harness`
- `be4b4ee2 test: add two-instance NSMB smoke`
- `65d2995e test: automate NSMB Mario vs Luigi route`
- `6df132fe test: bridge NSMB route into netplay`
- `a01687af docs: consolidate NSMB PoC status`
