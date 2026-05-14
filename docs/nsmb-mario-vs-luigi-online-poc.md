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

### 現在のブロッカー

- ROM起動クラッシュはコマンドライン起動では解消確認済み
- ユーザー操作での「Open ROM...」導線でも同じく落ちないか確認が必要
- CLI smoke testは未確定。GUI起動と2プロセスHost/Client接続確認が次のブロッカー
- 実ROMなしではNSMB Mario vs LuigiのMulti-Card Play到達確認とdesync検証はできない

### 次にやること

1. GUIの `Open ROM...` 操作で `roms/nsmb.nds` が落ちずに起動するか確認する
2. `MELONDS_NSML_POC=1` でmelonDSを2プロセス起動し、Host/Client接続ログが出るか確認する
3. 実ROMを使って2インスタンス/Multi-Card Playの導線を確認する
4. 同一savestate開始でdesync検証を行う
5. 実行確認で問題が出たらPoCコードを修正する

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
