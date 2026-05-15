# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、melonDSフォーク上でオンライン対戦できる形にする。

狙う方式は、DSローカル無線通信をWAN越しに中継する方式ではない。各PC上で2台分のDSを起動し、PC内のLocal MPでMario vs Luigiを成立させたうえで、PC間では入力と試合開始時の同期情報だけを交換する。

```text
Host PC:
  inst0: Mario側DS  <- hostのローカル入力
  inst1: Luigi側DS  <- client入力をネットワーク経由で注入

Client PC:
  inst0: Mario側DS  <- host入力をネットワーク経由で注入
  inst1: Luigi側DS  <- clientのローカル入力
```

## 現在の到達点

- melonDS側にNSMB Mario vs Luigi用の自動検証フックと入力同期PoCを追加済み。
- 1プロセス内で2つの `EmuInstance` を起動するテストがある。
- Mario vs Luigi到達用の入力スクリプト、スクリーンショット、RAM dump、RAM hash、savestate、Local MP共有状態保存、固定RTC、フレームバリア、ランダムtraceを追加済み。
- 日本版 `A2DJ` 向けに `Net::random` / `Net::getRandom()` 周辺の主要アドレスを移植済み。
- `MELONDS_NSML_NET_RANDOM_AUTO=1` により、Mario vs Luigi状態を検出して `Net::random.value` へ共通seedを自動注入できる。
- host/client間でmatch seedを配布するENetパケットを追加済み。
- localhostの2プロセス検証で、hostが送ったmatch seedをclientが受信し、両側で同じRNG timelineになることを確認済み。

## 現在のブロッカー

1. **入力同期timeout**
   - seed同期後、テスト終了付近でhost側がremote inputを待ち続けることがある。
   - 原因は、テスト終了条件、入力送信継続、remote input queueのdrain条件がまだlockstep向けに整理されていないこと。

2. **試合中の長時間同期未検証**
   - 初期seed同期とRNG timeline一致までは確認済み。
   - まだ「スター取得後の次スター再生成」「実操作中のMario/Luigi挙動」「長時間desyncなし」まではhost/client 2プロセスで確認できていない。

3. **Big Star以外のランダム要素**
   - 8コイン取得時アイテムなども `Net::random` / `Stage::getRandom()` の消費順に依存する可能性がある。
   - ただし8コインアイテム自動化は一旦保留。まずは本筋の2プロセス入力同期とBig Star再生成を優先する。

## 今後の大まかな道筋

1. 入力同期timeoutを直す。
   - テスト終了付近ではremote input待ちを発生させない。
   - host/clientが同じフレーム範囲を安定して走り切れるようにする。

2. ローカル疑似2PCテストを安定させる。
   - 1台のPCでhostプロセスとclientプロセスを起動する。
   - 各プロセス内で2つの `EmuInstance` を起動する。
   - これにより最終構成の「2PC x 各2インスタンス」をlocalhostで再現する。

3. Mario vs Luigi試合状態で同期確認する。
   - 初期Big Star位置がhost/clientで一致すること。
   - スター取得後の次Big Star位置も一致すること。
   - `Net::randomCallCount` / `Net::random.value` / `Net::randomBranchAddress` が一致すること。
   - スクリーンショット、RAM dump、RNG timelineで差分を確認できること。

4. 入力同期を試合中の操作へ結合する。
   - host入力とclient入力を対応する仮想DSへ同一フレームで注入する。
   - 入力遅延ありlockstepとして、必要なremote inputが届くまで該当フレームを進めない。

5. localhostで安定したら、同一LAN、WANの順に検証する。
   - WANでは入力遅延、timeout、停止/再開、切断時の扱いを調整する。

## 実装済みの重要機能

- `src/frontend/qt_sdl/NsmbNetplayPoC.*`
  - 自動検証フック
  - 2インスタンス起動テスト
  - ENet入力同期PoC
  - host/client match seed配布
  - NSMB `Net::random.value` 自動注入
  - スクリーンショット/RAM dump/hash/random trace
- `tools/nsmb_mvl_ram_probe.py`
  - ROM gamecode確認
  - `A2DJ` シンボル表示
  - RNG timeline抽出
  - Big Star actor ID候補抽出
- `tests/nsmb_mario_vs_luigi_star_probe.inputs`
  - Mario vs Luigi開始後、inst0/Marioが最初のスターを取り、次スター再生成まで進める入力スクリプト。
- `docs/nsmb-a2dj-symbol-port.md`
  - 日本版 `A2DJ` 向けの移植済みシンボルと解析メモ。

## A2DJ解析メモ

- 手元ROMは日本版 `A2DJ`。
- 公開 `MammaMiaTeam/NSMB-Code-Reference` はUS版向けなので、固定アドレスはそのまま使えない。
- `Game` / `Stage` グローバル群はUS版から概ね `-0x9C0`、`Net` グローバル群は概ね `-0x9E0` のshiftで整合。
- Net系優先関数はUS版から概ね `-0x154` のshiftで整合。
- 現在の重要アドレス:
  - `Net::getRandom()` = `0x0200E5A0`
  - `Net::getRandom12()` = `0x0200E550`
  - `Net::syncRandomFull()` = `0x0200E5E8`
  - `Net::syncRandomFast()` = `0x0200E5F4`
  - `Net::Core::shareRandomSeed()` = `0x02010F04`
  - `Net::random.value` = `0x02088088`
- `Net::getRandom()` へのARM `BL` call siteは61個見つかっている。
- Big Star再生成時の関連呼び出し元は `0x0212D418` 周辺。
- `Game::stageGroup == 9`、`Game::vsMode == 1`、`Net::ggid == 0x42`、`Net::randomCallCount == 0` を満たすタイミングをMvsL seed注入点として使っている。

## 検証結果の要点

- 同じROM、同じ入力、固定RTCだけではスター位置は安定しなかった。
- Local MP timeout調整、strict wait、timestamp固定、外側のframe barrier、serial runだけでは、汎用的なDSローカル通信の決定性問題は解決しなかった。
- NSMB Centralの既存情報とも一致し、MvsLは「接続時seed同期 + gameplay中入力同期」の設計になっている。
- `Net::random.value` を共通seedへ注入すると、初期Big Star位置とRNG timelineを制御できた。
- host/client間のmatch seed配布により、localhost 2プロセスでframe `002800` / `002900` のRNG timeline一致を確認した。
- 現在残っている最大の実装問題は、seed同期ではなく入力lockstepの終了条件/queue制御。

## 検証コマンド

```powershell
# 1インスタンス smoke
.\scripts\run-nsmb-smoke.ps1 -Frames 180

# 2プロセス入力同期 smoke
.\scripts\run-nsmb-netplay-smoke.ps1 -Frames 180 -Port 8065

# 2 EmuInstance smoke
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180

# Mario vs Luigi route smoke
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200

# route + netplay smoke
.\scripts\run-nsmb-mvl-netplay-route-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8070
```

## 重要な環境変数

- `MELONDS_NSML_TEST=1`: 自動検証フックを有効化。
- `MELONDS_NSML_TEST_INSTANCES`: 起動するテスト用インスタンス数。
- `MELONDS_NSML_TEST_FRAMES`: 自動終了フレーム。
- `MELONDS_NSML_INPUT_SCRIPT`: 入力スクリプト。
- `MELONDS_NSML_POC=1`: NSMB netplay PoCを有効化。
- `MELONDS_NSML_ROLE=host|client`: netplay role。
- `MELONDS_NSML_PEER`: client側の接続先。
- `MELONDS_NSML_PORT`: ENetポート。
- `MELONDS_NSML_LOCAL_INSTANCE`: ローカル入力を担当するインスタンス。
- `MELONDS_NSML_NETPLAY_START_FRAME`: 入力同期開始フレーム。
- `MELONDS_NSML_MATCH_SEED`: hostが配布するmatch seed。
- `MELONDS_NSML_NET_RANDOM_AUTO=1`: MvsL状態検出時に `Net::random.value` を自動注入。
- `MELONDS_NSML_NET_RANDOM_VALUE`: 注入するRNG seed。
- `MELONDS_NSML_NET_RANDOM_FRAME`: 固定フレームでRNG seedを注入する検証用。
- `MELONDS_NSML_RANDOM_TRACE`: `Net::getRandom()` のcaller/value/countをCSV出力。
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump出力先。
- `MELONDS_NSML_RAM_DUMP_FRAMES`: dump対象フレーム。
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG出力先。
- `MELONDS_NSML_FIXED_RTC`: RTC固定。
- `MELONDS_NSML_DISABLE_JIT=1`: JIT無効化。

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にする。
- 実2PC/WAN検証に進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ設定、必要なBIOS/firmware/セーブ状態が必要。

## 運用ルール

- 実装状況が変わったら、このファイルの「現在の到達点」「現在のブロッカー」「次にやること」を更新する。
- 古い検証ログは長く追記し続けず、必要な結論だけを「検証結果の要点」に統合する。
