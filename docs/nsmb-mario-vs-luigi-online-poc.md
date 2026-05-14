# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS のローカル対戦限定モード **Mario vs Luigi** を、melonDSフォーク上でオンライン対戦できるようにする。

狙う方式は、DSローカル無線通信そのものをWAN越しに中継する方式ではない。各PC上で2台分のDSを動かし、PC内ではmelonDSのLocal MPで通信を完結させ、ネットワーク越しにはプレイヤー入力だけを同期する。

```text
Host PC:
  instance 0: Mario側DS  <- Host入力
  instance 1: Luigi側DS  <- Client入力をネット越しに受信

Client PC:
  instance 0: Mario側DS  <- Host入力をネット越しに受信
  instance 1: Luigi側DS  <- Client入力
```

## 現在の結論

route入力とnetplay入力同期の結合PoCはできている。ただし、まだ対戦成立とは言えない。

最新の重要ブロッカーは、host/clientでMario vs Luigi試合開始後の状態が一致していないこと。スクリーンショット上でスター位置がズレており、full RAM hashも一致していない。入力同期だけを後から開始しても、試合開始時点の状態がズレている限り同期対戦は成立しない。

次の優先課題は、入力同期の改善ではなく **試合開始時点の決定性固定**。

## 実装済み

### ビルド/起動

- VS2022 Build Tools + Clang toolsetで `debug-windows-x86_64` ビルドが成功
- 生成物: `build/debug-windows-x86_64\melonDS.exe`
- `GPU::SetRenderer()` 初期化時クラッシュを修正
  - 既存rendererがある場合だけ `SyncAllVRAMCaptures()` を呼ぶ
  - `roms/nsmb.nds` のコマンドライン起動で以前のアクセス違反が再発しないことを確認

### PoC入力同期

追加ファイル:

- `src/frontend/qt_sdl/NsmbNetplayPoC.h`
- `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`

主な環境変数:

- `MELONDS_NSML_POC=1`: 入力同期PoCを有効化
- `MELONDS_NSML_ROLE=host|client`
- `MELONDS_NSML_PEER`
- `MELONDS_NSML_PORT`
- `MELONDS_NSML_LOCAL_INSTANCE`
- `MELONDS_NSML_DELAY`
- `MELONDS_NSML_NETPLAY_START_FRAME`
- `MELONDS_NSML_NO_LOCAL_WAIT`

現在のPoCはENetでhost/clientを接続し、ローカル担当インスタンスの入力をフレーム番号付きで送信し、非担当インスタンスへリモート入力を注入する。

`MELONDS_NSML_NO_LOCAL_WAIT=1` はroute+netplay結合テスト用の暫定モード。ローカル担当インスタンスは送信を継続し、リモート担当インスタンスだけ受信待ちする。完全なロックステップ実装ではない。

### 自動検証フック

主な環境変数:

- `MELONDS_NSML_TEST=1`
- `MELONDS_NSML_TEST_INSTANCES`
- `MELONDS_NSML_TEST_FRAMES`
- `MELONDS_NSML_INPUT_SCRIPT`
- `MELONDS_NSML_HASH_LOG`
- `MELONDS_NSML_HASH_INTERVAL`
- `MELONDS_NSML_WAIT_TIMEOUT_MS`
- `MELONDS_NSML_SCREENSHOT_DIR`
- `MELONDS_NSML_SCREENSHOT_INTERVAL`

できること:

- 1プロセス内で2つの `EmuInstance` を自動起動
- 指定ROMを両インスタンスへロード
- フレーム範囲指定の入力スクリプト再生
- `inst0` / `inst1` / `all` によるインスタンス別入力
- 状態hash CSV出力
- 上画面/下画面を縦連結した `256x384` PNG出力
- テストフレーム上限到達時の自動終了

### Mario vs Luigi route

入力スクリプト:

- `tests/nsmb_mario_vs_luigi.inputs`

自動化済みの導線:

- タイトルメニューで「マリオVSルイージ」を選択
- Multi-Card Play側の「ソフトを持っている人と対戦」を選択
- instance 0はマリオ、instance 1はルイージを選択
- 検索/承認画面で両者を接続
- 対戦設定をOK
- 最初のコースを選択
- 対戦ステージまで到達

## 検証コマンド

### 1インスタンス smoke

```powershell
.\scripts\run-nsmb-smoke.ps1 -Frames 180
```

成功済み。

### 2プロセス入力同期 smoke

```powershell
.\scripts\run-nsmb-netplay-smoke.ps1 -Frames 180 -Port 8065
```

成功済み。host/clientのhashログが180フレームまで一致した。

### 2 EmuInstance smoke

```powershell
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180
```

成功済み。instance `0` / `1` の両方が180フレームまで進み、hashログが出る。

### Mario vs Luigi route smoke

```powershell
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200
```

成功済み。両インスタンスがMario vs Luigiの対戦ステージへ到達する。

### route + netplay smoke

```powershell
.\scripts\run-nsmb-mvl-netplay-route-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8070
```

成功済みの意味:

- host/client双方で `peer connected`
- host/client双方で `frame limit reached at frame=5100 instances=2`
- `remote input timeout` なし
- netplay開始後スクリーンショットが出力される

注意:

- これは「route後に入力同期へ切り替えて進行できる」検証。
- 「host/clientの試合状態が一致している」検証ではない。
- 実際、スター位置とfull RAM hashは一致していない。

## 入力スクリプト形式

基本形式:

```text
start-end buttons [touchX,touchY]
```

インスタンス指定:

```text
inst0 1260-1267 A
inst1 1260-1267 DOWN
all 1328-1499 NONE
```

ボタン:

```text
A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L, X, Y
```

複数ボタンは `+` で結合する。入力なしは `NONE` または `NEUTRAL`。

タッチ例:

```text
inst0 2280-2287 NONE 128,170
```

## 現在のブロッカー

### 試合状態の不一致

`logs/screens-mvl-netplay-host/` と `logs/screens-mvl-netplay-client/` のnetplay開始後スクリーンショットで、スター位置がhost/client間で一致していない。

これはMario vs Luigiの初期試合状態が一致していないことを意味する。ここが一致しない限り、入力同期をどれだけ正しくしても対戦は成立しない。

想定原因:

- NSMB内部RNG seed差分
- RTC/時刻由来の差分
- MAC/firmware/save差分
- Wi-Fi/Local MP同期タイミング差
- 2プロセス同時起動時のLocal MP干渉
- EmuInstanceごとのフレーム進行タイミング差

### RNG seedについて

melonDSが汎用的にNSMBのRNG seedを直接決める仕組みはない。RNG seedはNSMBのゲーム内部RAMにある値で、melonDSはRTC、MAC、firmware、フレーム進行、Wi-Fiタイミングなどの外部要因を提供しているだけ。

ROM改造が必須とはまだ言えない。選択肢は以下。

1. 試合開始直後の2台分savestateを正として保存し、両PCが同じsavestateペアからnetplay開始する
2. melonDS側でRTC/MAC/firmware/save/フレーム進行を固定し、routeから自然にスター位置が一致するか確認する
3. NSMBのRNG seed RAMアドレスが分かれば、ROM改造ではなくmelonDS側のメモリパッチでseed固定する
4. それでも不安定ならROMパッチでseed/初期配置を固定する

## 次にやること

優先順:

1. 試合開始直後の状態を一致させる方針を決める
2. まずはsavestateペア方式で、同じスター位置から開始できるか検証する
3. savestate開始後、入力同期で数千フレーム維持できるか検証する
4. 並行して、RTC/MAC/firmware/save固定でroute開始からスター位置が一致するか調べる
5. 必要ならNSMB固有のRNG seedアドレス調査、またはROMパッチ方針を検討する

## コミット履歴メモ

- `27b71d34 test: add NSMB netplay smoke harness`
- `be4b4ee2 test: add two-instance NSMB smoke`
- `65d2995e test: automate NSMB Mario vs Luigi route`
- `6df132fe test: bridge NSMB route into netplay`

