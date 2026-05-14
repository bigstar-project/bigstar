# NSMB Mario vs Luigi Online PoC Plan

## 目的

New Super Mario Bros. DS のローカル対戦限定モードである **Mario vs Luigi** を、melonDS のフォーク上でオンライン対戦できるようにする。

最終的に目指す形は、DS のローカル無線通信そのものをインターネット越しに流す方式ではなく、各PCで同じ2台分のDSを動かし、ネットワーク越しにはプレイヤー入力だけを同期する方式。

```text
Host PC:
  melonDS instance 0: Mario側DS  <- Host入力
  melonDS instance 1: Luigi側DS  <- Client入力をネット越しに受信
  DSローカル通信はPC内で完結

Client PC:
  melonDS instance 0: Mario側DS  <- Host入力をネット越しに受信
  melonDS instance 1: Luigi側DS  <- Client入力
  DSローカル通信はPC内で完結
```

## この方式を選ぶ理由

DS のローカル通信は非常に低遅延前提で、NiFi/ローカル無線フレームをそのままWAN越しに中継するのは実用上かなり厳しい。

一方で、各PC内で2台のDSローカル通信を完結させ、外にはコントローラー入力だけを送るなら、格闘ゲーム系のロックステップ/ロールバックに近い構造にできる。

最初のPoCではロールバックは狙わず、実装が単純な **入力遅延ありロックステップ** を使う。

## 入力遅延ありロックステップの考え方

各フレームで現在の入力をすぐゲームに反映せず、数フレーム後に使う。

例: 6フレーム遅延の場合

```text
Frame 100で押された入力 -> Frame 106で使う
Frame 101で押された入力 -> Frame 107で使う
Frame 102で押された入力 -> Frame 108で使う
```

Frame 106を実行する時点で、Host入力とClient入力の両方が揃っていなければ、そのフレームは進めず待つ。

これにより、両PCが同じ初期状態・同じROM・同じ入力列で進む限り、同じ試合を再現できる可能性がある。

## 最初のPoCのゴール

最初のゴールは「快適にオンライン対戦できる完成品」ではなく、以下を確認すること。

1. melonDS上で2インスタンスのローカル通信を維持できる
2. 各インスタンスの入力をフレーム単位で差し替えられる
3. Host/Client間で入力だけを送受信できる
4. 入力遅延ぶん待ってからフレームを進められる
5. 状態ハッシュをログ出力し、desyncの有無を確認できる
6. 実ROMで Mario vs Luigi まで進めたとき、数分間同期が保てるか検証できる

## PoCで縛る条件

決定性の問題を減らすため、最初はかなり条件を固定する。

- 両PCで同一ROMを使う
- 両PCで同一BIOS/firmwareを使う
- 両PCで同一savestateから開始する
- Multi-Card Playを優先する
- JITは無効化する
- 3Dレンダラはソフトウェア寄りにする
- RTC/時刻差分をなるべく固定する
- セーブデータ差分を避ける
- まずは2人対戦だけを対象にする
- タッチ入力は必要になるまで最小扱いにする

## 実装方針

melonDS既存のローカルMP実装は、同一PC内の2インスタンス間通信としてそのまま使う。

PoCでは、通常のネットワークMP実装とは別に、NSMB用の小さい実験レイヤーを追加する。

主な責務:

- 環境変数でPoCモードを有効化する
- Host/ClientをENetで接続する
- ローカル入力をフレーム番号つきで送信する
- リモート入力をフレーム番号つきで受信・保存する
- `inputDelay` フレーム前の入力を各インスタンスへ注入する
- 必要なリモート入力が未着ならフレーム実行前に待つ
- 60フレームごとなどに状態ハッシュを出力する

## 想定する起動例

Host側:

```powershell
$env:MELONDS_NSML_POC="1"
$env:MELONDS_NSML_ROLE="host"
$env:MELONDS_NSML_PORT="8065"
$env:MELONDS_NSML_LOCAL_INSTANCE="0"
.\melonDS.exe
```

Client側:

```powershell
$env:MELONDS_NSML_POC="1"
$env:MELONDS_NSML_ROLE="client"
$env:MELONDS_NSML_PEER="HOST_IP"
$env:MELONDS_NSML_PORT="8065"
$env:MELONDS_NSML_LOCAL_INSTANCE="1"
.\melonDS.exe
```

## 必要になるROM/データ

PoCのコード骨格はROMなしでも作れる。

ただし、以下の検証には実際の New Super Mario Bros. DS のROMが必要。

- Multi-Card Playで2インスタンス対戦が成立するか
- Mario vs Luigiの開始地点まで進めるか
- 同一savestate開始で両PCが同期するか
- 数千フレーム単位でdesyncしないか
- 入力遅延が何フレーム必要か

ROMが必要になった時点で、こちらから明確に依頼する。

## 次にやること

1. melonDSの通常ビルドが通るか確認する
2. PoC用の入力同期レイヤーをビルド対象に入れる
3. `EmuThread` のフレーム実行直前にPoC入力差し替えを挟む
4. `EmuThread` のフレーム実行後に状態ハッシュログを出す
5. Host/Clientを同一PC内で起動して、入力パケットが交換されるか確認する
6. 2インスタンスのローカルMPと併用して止まらないか確認する
7. 実ROMでMulti-Card Playの導線を確認する
8. 同一savestate開始でdesync検証を行う

## 現在の実装メモ

追加済みファイル:

- `src/frontend/qt_sdl/NsmbNetplayPoC.h`
- `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`

変更済みファイル:

- `src/frontend/qt_sdl/CMakeLists.txt`
- `src/frontend/qt_sdl/EmuThread.cpp`
- `src/frontend/qt_sdl/main.cpp`

現在のPoCは以下を行う。

- `MELONDS_NSML_POC=1` のときだけ有効化
- ENetでHost/Client接続
- ローカル担当インスタンスの入力をフレーム番号つきで送信
- リモート入力を受信してフレーム番号ごとに保存
- `MELONDS_NSML_DELAY` フレーム後に入力を反映
- 遅延後は、どちらのインスタンスもリモート入力が届くまで待つ
- 60フレームごとに簡易状態ハッシュをstdoutへ出力
- 終了時にENetを破棄

現時点ではUIはない。環境変数で動かす実験用フック。

## 現在のビルド状況

PoCモジュール単体は Visual Studio 2019 の `cl` で構文コンパイル済み。

ただし、この環境ではフルCMake構成が未完了。

理由:

- melonDSのWindowsプリセットはvcpkgでQt 6.10を取得する
- Qt 6.10 は Visual Studio 2022相当のMSVC 1930以上を要求する
- この環境には Visual Studio 2019 / MSVC 1927 しか入っていない

確認されたエラー:

```text
Qt requires at least Visual Studio 2022 (MSVC 1930 or newer),
you're building against version 1927.
```

次にフルビルドするには、以下のどちらかが必要。

- Visual Studio 2022 Build Toolsを入れて `debug-windows-x86_64` を再構成する
- QtのMSVC最小バージョンチェックを外す。ただしQt 6.10側の保証外なのでPoC検証用に限定する

## 進捗トラッカー

### 完了

- melonDS upstream を `C:\Users\Sugiyama\melon-ds-mario` に取得
- PoC方針を文書化
- `AGENTS.md` に、作業進捗を `docs/` へ随時記録するルールを追加
- `NsmbNetplayPoC` 実験モジュールを追加
- PoCモジュールをQt/SDLフロントエンドのCMake対象へ追加
- `EmuThread` にフレーム直前の入力差し替えフックを追加
- `EmuThread` にフレーム実行後の状態ハッシュログを追加
- `main.cpp` にPoC終了処理を追加
- `vcpkg/` を `.gitignore` に追加
- `NsmbNetplayPoC.cpp` 単体のVS2019構文コンパイルを確認
- Visual Studio 2022 Build Tools に C++ compiler component を追加
- VS2022の `cl.exe` を確認: `VC\Tools\MSVC\14.44.35207\bin\Hostx86\x64\cl.exe`
- vcpkg依存関係の取得/ビルドが完了
- Qt 6.10関連パッケージのvcpkg installが成功
- VS2022 Build Tools に LLVM/Clang toolset を追加
- `clang.exe`, `clang++.exe`, `llvm-rc.exe` を確認
- Clang構成、`ARCHITECTURE=x86_64`、`ENABLE_JIT=OFF` でCMake再生成に成功
- Debugビルドが成功
- 生成物を確認: `build/debug-windows-x86_64\melonDS.exe`
- `melonDS.exe --help` は即時終了せずタイムアウトした。残プロセスはなし
- `roms/nsmb.nds` 起動時クラッシュを調査。Windowsイベントログでは `GPU::SyncAllVRAMCaptures()` 付近のアクセス違反だった
- 原因候補として、初期構築時の `GPU::SetRenderer()` が `Rend == nullptr` のままVRAM capture同期へ入れる経路を確認
- `GPU::SetRenderer()` を、既存rendererがある場合だけ `SyncAllVRAMCaptures()` するよう修正
- 修正後にDebugビルド成功
- `roms/nsmb.nds` をコマンドライン起動して10秒間維持できることを確認。以前の `GPU::SyncAllVRAMCaptures()` アクセス違反は再発しなかった
- テストモード実装を追加開始
  - `MELONDS_NSML_TEST=1`
  - `MELONDS_NSML_TEST_FRAMES`
  - `MELONDS_NSML_INPUT_SCRIPT`
  - `MELONDS_NSML_HASH_LOG`
  - `MELONDS_NSML_HASH_INTERVAL`
- サンプル入力スクリプト `tests/nsmb_smoke.inputs` を追加
- `scripts/run-nsmb-smoke.ps1` を追加
- `scripts/run-nsmb-netplay-smoke.ps1` を追加
- 単体smoke test成功: `scripts/run-nsmb-smoke.ps1 -Frames 180`
- 2プロセスnetplay smoke test成功: `scripts/run-nsmb-netplay-smoke.ps1 -Frames 180 -Port 8065`
- netplay smokeではhost/clientのhashログが180フレームまで一致

### 現在のブロッカー

- ROM起動クラッシュはコマンドライン起動では解消確認済み
- ユーザー操作での「Open ROM...」導線でも同じく落ちないか確認が必要
- 自動テスト基盤の最初の段階は通った
- まだ「1プロセス内2インスタンス + Local MP + 入力振り分け」の自動検証は未実装
- 実ROMなしではNSMB Mario vs LuigiのMulti-Card Play到達確認とdesync検証はできない

### 次にやること

1. 1プロセス内で2つのEmuInstanceを自動起動できるテスト導線を作る
2. 2つのEmuInstanceで同じROMを開き、Local MPが有効な状態まで自動化する
3. NSMBのメニュー入力を記録/調整してMario vs LuigiのMulti-Card Play導線を入力スクリプト化する
4. 2台分の状態hashをログ化し、host/client間で比較する
5. desyncが出た場合はRTC/MAC/firmware/save/JIT/rendererなどの固定化を進める

## 自動検証方針

最終的にCodexだけでMario vs Luigi到達まで確認できるように、Qt/SDLのGUIを外からクリックするのではなく、melonDS内部へテスト用フックを入れる。

追加予定のテスト機能:

- `MELONDS_NSML_TEST=1` でテストモードを有効化
- `MELONDS_NSML_TEST_FRAMES` で指定フレーム後に自動終了
- `MELONDS_NSML_INPUT_SCRIPT` でフレーム範囲ごとの入力を再生
- `MELONDS_NSML_HASH_LOG` へ一定間隔で状態ハッシュを書き出す
- `MELONDS_NSML_HASH_INTERVAL` でhash間隔を指定
- `MELONDS_NSML_WAIT_TIMEOUT_MS` でテスト時のリモート入力待ち上限を指定
- PowerShellからhost/clientプロセスを起動し、終了コード、stdout、Windows EventLog、hashログを比較する

この形にすると、Codexが `build -> run -> log確認 -> 修正 -> rerun` のループを自分で回せる。

## 主要リスク

### 決定性

同じ入力列でも、PC間で内部状態がズレる可能性がある。

主な原因:

- RTC
- firmware差分
- MACアドレス
- セーブデータ差分
- JIT
- スレッドタイミング
- 音声同期
- Wi-Fi内部キューのタイミング

### 操作遅延

ロックステップでは入力遅延が避けられない。

まずは6フレーム前後で試し、止まる場合は8から10フレームへ増やす。

### ロールバック未対応

このPoCでは、リモート入力が遅れた場合は待つ。

快適性を上げるには将来的にロールバックが必要だが、まずは同期可能性の確認を優先する。

## 完成品までの段階

```text
Phase 1: 入力同期PoC
  入力交換、遅延適用、待機、状態ハッシュ

Phase 2: 実ROM検証
  NSMB Mario vs Luigiまで進めてdesync確認

Phase 3: 専用UI/設定
  Host/Client設定、遅延設定、接続状態表示

Phase 4: 安定化
  deterministic設定の固定、savestate同期、エラー処理

Phase 5: 快適化
  遅延自動調整、軽い予測、必要ならロールバック検討
```

## 現時点の判断

melonDSフォークでのPoCは現実的。

ただし、成功の鍵は「オンライン通信部分」そのものではなく、同じ初期状態と同じ入力列で、2台分のDSローカル通信を含むエミュレーションがPC間で決定論的に一致するかどうか。

そのため、最初は完成度よりも desync 検出と再現性を重視する。

## 2026-05-14 作業ログ

### 完了

- 入力同期/自動検証ハーネスを `27b71d34 test: add NSMB netplay smoke harness` としてコミットした
- テストモードで2つの `EmuInstance` を同一プロセス内に自動起動するため、`MELONDS_NSML_TEST_INSTANCES` の実装を開始した
- 2つの `EmuInstance` で同じROMを開き、両方の状態hashが出ることを確認する `scripts/run-nsmb-two-instance-smoke.ps1` を追加した

### 現在の確認対象

- `MELONDS_NSML_TEST_INSTANCES=2` で `roms/nsmb.nds` を2つの `EmuInstance` にロードできるか
- `MELONDS_NSML_HASH_LOG` に instance `0` と `1` の両方の行が出るか
- 指定フレーム数で自動終了できるか
- 初回確認では2つの `EmuInstance` が180フレームまで到達し、両方のhashは出た。ただしQt終了処理中に `0xC0000005` アクセス違反が出たため、テストモードではフレーム上限到達後にログをflushして即終了する方針に変更した

### 次にやること

1. VS2022/ClangのDebugビルドを通す
2. `scripts/run-nsmb-two-instance-smoke.ps1 -Frames 180` を実行する
3. 失敗した場合はstdout/hashログから、2つ目の `EmuInstance` 起動かROMロードか終了制御のどこで止まっているか切り分ける
4. 成功後、Mario vs Luigi到達用の入力スクリプト作成手順を整理する

### ブロッカー

- 現時点ではなし
- Mario vs Luigiのメニュー導線自動化には、最初の入力タイミング/画面状態の対応付けが必要。必要なら次の段階でスクリーンショットダンプ用のテストフックを追加する

### 入力スクリプトの作り方

現在の入力スクリプトは `MELONDS_NSML_INPUT_SCRIPT` で指定する。サンプルは `tests/nsmb_smoke.inputs`。

形式:

```text
start-end buttons [touchX,touchY]
```

例:

```text
# 0から119フレームまでは何もしない
0-119 NONE

# 120から125フレームまでSTARTを押す
120-125 START

# 180から185フレームまでAを押す
180-185 A

# 240から245フレームまで十字キー右とAを同時押し
240-245 RIGHT+A

# タッチ入力が必要な場合。座標はDSタッチスクリーン座標
300-305 NONE 128,96
```

使えるボタン:

```text
A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L, X, Y
```

複数ボタンは `+` で結合する。入力なしは `NONE` または `NEUTRAL`。

実行例:

```powershell
$env:MELONDS_NSML_TEST="1"
$env:MELONDS_NSML_TEST_INSTANCES="2"
$env:MELONDS_NSML_TEST_FRAMES="600"
$env:MELONDS_NSML_INPUT_SCRIPT="tests\nsmb_mario_vs_luigi.inputs"
$env:MELONDS_NSML_HASH_LOG="logs\nsmb-mvl.hash.csv"
.\build\debug-windows-x86_64\melonDS.exe .\roms\nsmb.nds
```

現時点では画面内容を自動判定するフックはまだないため、Mario vs Luigi開始までの初回スクリプト作成には、人間が画面を見て「何フレーム目にどの入力を入れるか」を決める必要がある。

ただし、次の段階でスクリーンショット/フレームバッファダンプを追加すれば、Codex側で `入力スクリプト修正 -> 実行 -> 画面確認 -> 再修正` のループを回せるようになる。

### 2インスタンスsmoke結果

`scripts/run-nsmb-two-instance-smoke.ps1 -Frames 180` は成功。

確認できたこと:

- 1プロセス内で2つの `EmuInstance` を自動起動できる
- 2つの `EmuInstance` が `roms/nsmb.nds` をロードできる
- instance `0` と `1` の両方が180フレームまで進む
- instance `0` と `1` の両方のhashログが `logs/nsmb-two-instance.hash.csv` に出る
- テスト完了後のアクセス違反ダイアログは、テストモード専用の即終了経路で回避した

## 2026-05-14 スクリーンショットフック作業

### 目的

Codex側で画面状態を確認しながら、NSMBのメニュー入力を調整して Mario vs Luigi まで到達できるようにする。

### 実装予定

- `MELONDS_NSML_SCREENSHOT_DIR` でPNG出力先を指定する
- `MELONDS_NSML_SCREENSHOT_INTERVAL` で何フレームごとに保存するか指定する
- 各 `EmuInstance` の上画面/下画面を縦に連結した `256x384` PNGを保存する
- ファイル名に instance ID と frame を入れる

### 現在の次アクション

1. `NDS::GPU.GetFramebuffers()` からソフトウェアフレームバッファを取得してPNG保存する
2. neutral入力でタイトル画面までのスクリーンショットを確認する
3. 入力スクリプトを段階的に追加し、Mario vs Luigi導線を進める

### 完了

- `MELONDS_NSML_SCREENSHOT_DIR` と `MELONDS_NSML_SCREENSHOT_INTERVAL` を追加した
- テストモードで各 `EmuInstance` の上画面/下画面を縦に連結した `256x384` PNGを出力できるようにした
- 入力スクリプトに `inst0` / `inst1` / `all` のインスタンス別指定を追加した
- `tests/nsmb_mario_vs_luigi.inputs` を追加し、以下の導線を自動化した
  - タイトルメニューで「マリオVSルイージ」を選択
  - Multi-Card Play側の「ソフトを持っている人と対戦」を選択
  - instance 0 はマリオ、instance 1 はルイージを選択
  - 検索/承認画面で両者を接続
  - 対戦設定をOK
  - 最初のコースを選択
- `logs/screens-mvl-route7/inst0_frame004200.png` と `logs/screens-mvl-route7/inst1_frame004200.png` で、Mario vs Luigiの対戦画面に入っていることを確認した
- 再現用スクリプト `scripts/run-nsmb-mvl-route-smoke.ps1` を追加した

### 検証コマンド

```powershell
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200
```

検証結果:

- 成功: `NSMB Mario vs Luigi route smoke passed: frames=4200 rows=28 screenshots=70`
- 最終確認画像: `logs/screens-mvl-route/inst0_frame004200.png`, `logs/screens-mvl-route/inst1_frame004200.png`
- 4200フレーム時点で両インスタンスとも対戦ステージに入っている

### 入力スクリプトのインスタンス別指定

既存形式:

```text
620-627 DOWN
```

インスタンス別形式:

```text
inst0 1260-1267 A
inst1 1260-1267 DOWN
all 1328-1499 NONE
```

`inst0` は1つ目の `EmuInstance`、`inst1` は2つ目の `EmuInstance`。指定がない行は従来通り全インスタンスに適用される。

## 2026-05-14 route + netplay 結合作業

### 目的

各PC/各プロセスで同じ Mario vs Luigi 試合状態まで進めた後、そこから先だけ入力同期netplayへ切り替える。

### 実装方針

- `MELONDS_NSML_NETPLAY_START_FRAME` を追加する
- `syncFrame < MELONDS_NSML_NETPLAY_START_FRAME` の間は、ネット入力ではなくローカルの入力スクリプトをそのまま使う
- 開始フレーム以降は、既存の `MELONDS_NSML_LOCAL_INSTANCE` に従ってローカル側入力を送信し、非ローカル側インスタンスにはリモート入力を注入する
- route smokeではhostを `localInstance=0`、clientを `localInstance=1` にする

### 現在の次アクション

1. `NsmbNetplayPoC` に `MELONDS_NSML_NETPLAY_START_FRAME` を追加する
2. host/client両プロセスで `tests/nsmb_mario_vs_luigi.inputs` を再生するrunnerを作る
3. 4200フレーム到達後に両プロセスが接続済みで、以降もフレーム上限まで進むことを確認する

### 完了

- `MELONDS_NSML_NETPLAY_START_FRAME` を追加した
- netplay開始フレームまではroute入力をそのまま使い、開始後は `MELONDS_NSML_LOCAL_INSTANCE` に応じて入力送受信へ切り替えるようにした
- 2インスタンス同時実行時のテスト用に `MELONDS_NSML_NO_LOCAL_WAIT=1` を追加した
  - ローカル操作側インスタンスは入力送信を継続し、リモート操作側インスタンスだけリモート入力待ちを行う
  - これはroute+netplay結合PoC用の暫定モード。完全なロックステップには、2つの `EmuInstance` を同じcoordinatorでフレーム同期させる追加実装が必要
- `scripts/run-nsmb-mvl-netplay-route-smoke.ps1` を追加した
- host/client両プロセスで `tests/nsmb_mario_vs_luigi.inputs` を再生し、4500フレームからnetplay入力同期へ切り替えるsmokeを通した

### 検証コマンド

```powershell
.\scripts\run-nsmb-mvl-netplay-route-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8070
```

検証結果:

- 成功: `NSMB Mario vs Luigi netplay route smoke passed: frames=5100 start=4500 compareFrame=5100`
- host/client双方で `peer connected` を確認
- host/client双方で `frame limit reached at frame=5100 instances=2` を確認
- `remote input timeout` は出ていない
- `logs/screens-mvl-netplay-host/` と `logs/screens-mvl-netplay-client/` にnetplay開始後のスクリーンショットを出力済み

### 残課題

- full RAM hashはhost/client間でまだ一致しない。RTC、Wi-Fiタイミング、Local MP同期タイミングなどの差分が含まれるため、次の段階で決定性固定が必要
- 現在のroute+netplay smokeは「route後に入力同期へ切り替えて進行できる」ことの検証であり、「完全ロックステップで両PCが同一状態を保つ」検証ではない
