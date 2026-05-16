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
- 1プロセス内で2つの `EmuInstance` を起動し、Mario vs Luigiへ到達する入力スクリプトを実行できる。
- スクリーンショット、RAM dump、RAM hash、savestate、Local MP共有状態保存、固定RTC、フレームバリア、random traceを追加済み。
- 日本版 `A2DJ` 向けに `Net::random` / `Net::getRandom()` 周辺の主要アドレスを移植済み。
- `MELONDS_NSML_NET_RANDOM_AUTO=1` により、Mario vs Luigi状態を検出して `Net::random.value` へ共通seedを自動注入できる。
- host/client間でmatch seedを配布するENetパケットを追加済み。
- 入力遅延lockstepは `syncFrame + delay` のfuture input送信方式に寄せ、短い入力同期テストはtimeoutなしで完走する。
- `MELONDS_NSML_NET_RANDOM_VALUE` を指定した場合、その値を起動前に確定済みmatch seedとして扱うようにした。起動後のseed待機でLocal MP進行を乱さないため。
- `MELONDS_NSML_WAIT_FOR_PEER=1` を指定した場合だけhostが開始前peer待機するようにした。通常のPoCでは、ロビー段階でseedを確定してからエミュレーションを走らせる方針。
- `MELONDS_NSML_DEFER_NETWORK_UNTIL_START=1` で、`NetplayStartFrame` 直前までENet pump/seed送信を遅延できる。
- `MELONDS_NSML_NETPLAY_FRAME_BARRIER=1` で、入力同期区間だけ2インスタンスをフレーム境界で揃えられる。
- `scripts/run-nsmb-mvl-netplay-staged-smoke.ps1` で、hostを先にMario vs Luigi状態まで進めてからclientを起動する疑似2PC検証を自動化した。

## 現在の検証結果

- `0x00000100` は、既存スター取得スクリプトで取れる初期Big Star位置として有効。
  - Marioの左側に出る、ミニマップ上では右端寄りの位置。
  - `logs/star-seed-00000100-baseline/screens/inst0_frame004000.png` で確認済み。
- 1プロセスbaselineでは、`0x00000100` 注入後にスター取得と次Big Star再生成RNG消費まで到達できた。
  - frame 2800: `count=0x00 value=0x00000100 branch=0x020CBF24`
  - frame 2900: `count=0x92 value=0xE79BEE4F branch=0x0212D41C`
  - frame 6400: `count=0x93 value=0x661B81F0 branch=0x0212D41C`
- localhost 2プロセス検証では、ROM/saveを共有するとLocal MP到達や乱数注入の結果が壊れる。実2PC相当の検証では、host/clientでROMコピーとsave派生ファイルを分離する必要がある。
- ROM/saveを分離してhost/clientを並行起動すると、CPU負荷やスケジューリングの影響で片側がMario vs Luigi前の相手待ちに残ることがある。
- 単独起動の `role=client` ではMario vs Luigiへ到達できるため、client roleそのものではなく、localhost上の疑似2PC検証方法の問題として扱っている。
- `logs/mvl-seed-00000100-state-source/state-frame4100` に、`0x00000100` 注入後のMario vs Luigi状態から `inst0.mln`、`inst1.mln`、`localmp.bin` を保存済み。
- 上記savestateの単独ロード自体は成功するが、復元後にNSMB側が通信切断扱いになる。Local MP snapshotとNDS savestateの保存タイミング、または復元時のLocal MP内部状態がまだ不十分。
- savestate保存は、保存後に片方のインスタンスを待機させるとLocal MP進行を乱すため、非ブロッキング保存へ変更した。
- frame 5000での保存元作成は成功したが、ロード後は引き続き通信切断になる。savestate方式は検証補助として保留し、最終目標に近い「通常ルートで試合開始後にnetplayへ入る」方向を優先する。
- localhost疑似2PCの負荷回避として、hostを先に進めてからclientを起動する段階起動を採用した。
- `MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START=1` でエミュレーション途中にhostを止める方式は、Local MP進行を崩すため保留。現在はhostが `NetplayStartFrame` に到達したことをhash CSVで確認してからclientを起動する。
- `scripts/run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500` は完走する。
  - 最新確認ログ: `logs/netplay-staged-fixedrtc-nojit-00000100`、`logs/netplay-staged-netplaybarrier-00000100`
  - host/clientとも `0x00000100` 注入、peer接続、lockstep開始、frame limit到達を確認済み。
  - remote input timeoutは出ていない。
- ただし、入力同期開始後のスクリーンショット/RAM hashはまだhost/clientで完全一致していない。
  - Big Star位置は揃うが、4800フレーム時点でMarioの姿勢/表示状態がズレる。
  - `MELONDS_NSML_NO_LOCAL_WAIT` を外し、`MELONDS_NSML_NETPLAY_FRAME_BARRIER=1`、固定RTC、JIT無効を入れても完全一致には至っていない。
- netplayなしの通常routeを同一条件で2回走らせてもRAM hashが一致しないことを確認した。
  - `logs/route-determinism-a` vs `logs/route-determinism-b`
  - 固定RTC、JIT無効、`0x00000100` seed注入でも1500フレーム以降で差分が出る。
  - `MELONDS_NSML_LOCALMP_FIXED_TIMESTAMP` だけでは解消しない。
  - 現在の主ブロッカーはWAN入力同期ではなく、1プロセス内2インスタンスLocal MPルート自体の決定性不足。

## 現在のブロッカー

1. **Local MPルート自体の非決定性**
   - 同一PC、同一ROM/save、同一入力、固定RTC、JIT無効、同一seedでも、通常routeのRAM hashが実行ごとに一致しない。
   - この状態ではWAN入力同期を入れても最終的にdesyncする。
   - 次は2つの `EmuInstance` の実行順、Local MP packet/reply処理、Wi-Fi timestamp/timeoutをさらに固定する必要がある。

2. **savestate復元後のLocal MP通信切断**
   - `inst0.mln`、`inst1.mln`、`localmp.bin` は保存/ロードできる。
   - ただしロード後に「通信がせつだんされました」画面になる。
   - 最終実装に必須ではないので、現時点では深追いしない。必要になったらLocal MPだけでなくWi-Fi側の復元状態も見る。

3. **入力同期後のhost/client状態不一致**
   - staged smokeはtimeoutなしで完走するが、4800フレーム時点のスクリーンショット/RAM hashが一致しない。
   - 現状ではBig Star位置は揃っているが、プレイヤー表示/状態がズレる。
   - 通常route単体の決定性不足が先にあるため、まずLocal MP determinismを固める。

4. **Big Star以外のランダム要素**
   - 8コイン取得時アイテムなども `Net::random` / `Stage::getRandom()` の消費順に依存する可能性がある。
   - ただし8コインアイテム自動化は一旦保留。まずはBig Star取得/再生成と入力同期の安定化を優先する。

## 次にやること

1. 通常route単体の決定性を固める。
   - 同一条件で `run-nsmb-mvl-route-smoke.ps1` を2回走らせ、RAM hashと主要スクリーンショットが一致する状態を目標にする。
   - `SERIAL_RUN` は現状かなり遅いので、全フレーム逐次実行ではなく、Local MP送受信タイミングだけを安定化できないか見る。
   - Local MPの `RecvReplies` / `RecvHostPacket` のtimeout、packet timestamp、host/client packet順を重点的に追う。
2. 通常routeが一致したら、staged netplay smokeでhost/clientの4800/5100フレームを一致させる。
3. host/clientの終了合意を追加する。
   - 片側だけがframe limitへ到達してpeer disconnectし、もう片側がremote input timeoutになる状態をなくす。
4. localhost疑似2PCの試合中入力同期が安定したら、同一LAN、WANの順に入力遅延とtimeoutを調整する。

## 実装済みの重要機能

- `src/frontend/qt_sdl/NsmbNetplayPoC.*`
  - 自動検証フック
  - 2インスタンス起動テスト
  - ENet入力同期PoC
  - host/client match seed配布
  - preconfigured match seed
  - netplay開始フレームでhostがpeer接続を待つ段階起動用フック
  - netplay開始直前までENet処理を遅延する検証フック
  - netplay区間だけの2インスタンスフレームバリア
  - NSMB `Net::random.value` 自動注入
  - 入力遅延future-frame送信
  - lockstep開始ウォームアップ
  - テスト終了時のENet flush grace
  - input packet送受信trace
  - スクリーンショット/RAM dump/hash/random trace
- `tools/nsmb_mvl_ram_probe.py`
  - ROM gamecode確認
  - `A2DJ` シンボル表示
  - RNG timeline抽出
  - Big Star actor ID候補検出
- `tests/nsmb_mario_vs_luigi_star_probe.inputs`
  - Mario vs Luigi開始後、inst0/Marioが初期スターを取り、次スター再生成まで進める入力スクリプト。
- `docs/nsmb-a2dj-symbol-port.md`
  - 日本版 `A2DJ` 向けの移植済みシンボルと解析メモ。

## A2DJ解析メモ

- 対象ROMは日本版 `A2DJ`。
- 公開 `MammaMiaTeam/NSMB-Code-Reference` はUS版向けなので、固定アドレスはそのまま使えない。
- 現在の重要アドレス:
  - `Net::getRandom()` = `0x0200E5A0`
  - `Net::getRandom12()` = `0x0200E550`
  - `Net::syncRandomFull()` = `0x0200E5E8`
  - `Net::syncRandomFast()` = `0x0200E5F4`
  - `Net::Core::shareRandomSeed()` = `0x02010F04`
  - `Net::random.value` = `0x02088088`
  - `Net::randomCallCount` = `0x02088068`
- `Net::getRandom()` へのARM `BL` call siteは61個見つかっている。
- Big Star再生成時の関連呼び出し先は `0x0212D418` 周辺。
- MvsL seed注入点は現在、`Game::stageGroup == 9`、`Game::vsMode == 1`、`Net::ggid == 0x42` を満たす最初のタイミングとしている。注入時に `Net::randomCallCount` を0へ戻す。

## よく使う検証コマンド

```powershell
# 1インスタンス smoke
.\scripts\run-nsmb-smoke.ps1 -Frames 180

# 2 EmuInstance smoke
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180

# Mario vs Luigi route smoke
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200

# route + netplay smoke
.\scripts\run-nsmb-mvl-netplay-route-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8070

# staged route + netplay smoke
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071
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
- `MELONDS_NSML_NETPLAY_WARMUP_FRAMES`: 入力同期開始直後にremote waitへ入る前のウォームアップフレーム数。
- `MELONDS_NSML_QUIT_GRACE_MS`: テスト終了前にENetをpump/flushする猶予時間。
- `MELONDS_NSML_MATCH_SEED`: hostが配布するmatch seed。
- `MELONDS_NSML_NET_RANDOM_AUTO=1`: MvsL状態検出時に `Net::random.value` を自動注入。
- `MELONDS_NSML_NET_RANDOM_VALUE`: 注入するRNG seed。指定時はpreconfigured match seedとしても扱う。
- `MELONDS_NSML_WAIT_FOR_PEER=1`: hostがframe 0でpeer接続を待つ。通常は使わず、ロビー段階でseedを事前確定する。
- `MELONDS_NSML_WAIT_FOR_PEER_AT_NETPLAY_START=1`: hostが `MELONDS_NSML_NETPLAY_START_FRAME` でpeer接続を待つ。localhost疑似2PCでhostを先行させる検証用。
- `MELONDS_NSML_DEFER_NETWORK_UNTIL_START=1`: `NetplayStartFrame` 直前までENet pump/seed送信を遅延する。
- `MELONDS_NSML_NETPLAY_FRAME_BARRIER=1`: 入力同期区間だけ2インスタンスを同じフレーム境界で揃える。
- `MELONDS_NSML_RANDOM_TRACE`: `Net::getRandom()` のcaller/value/countをCSV出力。
- `MELONDS_NSML_INPUT_TRACE`: input packetの送受信をログ出力。
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump出力先。
- `MELONDS_NSML_RAM_DUMP_FRAMES`: dump対象フレーム。
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG出力先。
- `MELONDS_NSML_FIXED_RTC`: RTC固定。
- `MELONDS_NSML_DISABLE_JIT=1`: JIT無効化。
- `MELONDS_NSML_LOCALMP_STRICT_WAIT=1`: Local MP受信待ちをテスト用に厳密化。
- `MELONDS_NSML_LOCALMP_FIXED_TIMESTAMP`: Local MP packet timestampを固定する。現時点ではこれだけでは通常routeの非決定性は解消しない。

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にする。
- 実2PC/WAN検証に進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ設定、必要なBIOS/firmware/セーブ状態が必要。

## 運用ルール

- 実装状況が変わったら、このファイルの「現在の到達点」「現在の検証結果」「現在のブロッカー」「次にやること」を更新する。
- 古い検証ログは長く追記し続けず、必要な結論だけを「現在の検証結果」に統合する。
