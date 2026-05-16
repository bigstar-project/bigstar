# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、melonDSフォーク上でWAN越しに対戦できる形へ近づける。

最終的な狙いは「DSローカル無線フレームをそのままWANへ流す」ことではない。NSMBの対戦で意味のあるゲーム状態と入力を同期し、2台のPC間で同じ試合を維持する。

## 現在の方針

Local MPの完全な決定性だけに依存する方針は採用しない。

これまでの検証で、同一PC、同一ROM/save、固定RTC、JIT無効、固定RNG seedでも、melonDSの2 EmuInstance + Local MP経路は実行ごとに揺れることが分かった。試合開始や基本状態までは揃えられるが、RAM hashや描画順序、actor候補領域は一致しないことがある。

そのため、今後は次の方針に寄せる。

1. 2 EmuInstance + Local MPは起動・到達・観測用の土台として使う。
2. 対戦の正しさはNSMB側の重要状態同期で担保する。
3. 入力同期netplayは残すが、入力だけで全状態が一致する前提にはしない。
4. 長期的には、Local MP依存を減らし、NSMB Mario vs Luigi専用 runner / patch に寄せる。

## 実装済み

- NSMB Mario vs Luigi向け自動検証フック。
- 1プロセス内2 EmuInstance起動テスト。
- 入力スクリプト実行。
- スクリーンショット、RAM dump、RAM hash、任意フレームsavestate。
- 固定RTC、JIT無効化、frame barrier、netplay frame barrier。
- ENetによる入力遅延lockstep PoC。
- host/client match seed配布。
- `MELONDS_NSML_NET_RANDOM_AUTO=1` によるMario vs Luigi状態検出時の `Net::random.value` 注入。
- 日本版 `A2DJ` 向け主要シンボル移植。
- `Net::getRandom()` / `Net::getRandom12()` / `Net::syncRandom*()` 周辺の解析メモ。
- Local MP packet / Wi-Fi MP reply slot trace。
- `MELONDS_NSML_GAME_STATE_TRACE` による軽量ゲーム状態CSV trace。
- `MELONDS_NSML_GAME_STATE_TRACE_EXTENDED=1` による重い候補領域trace。
- `MELONDS_NSML_STATE_SYNC=1` によるnetplay中の軽量ゲーム状態hash交換。
- `MELONDS_NSML_STATE_SYNC_EXTENDED=1` による候補領域別hash交換。
- staged netplay smokeでhost/client別のゲーム状態traceとRAM dumpを出せるようにした。

## 重要な解析済みアドレス

対象ROMは日本版 `A2DJ`。

| 項目 | アドレス | 状態 |
| --- | --- | --- |
| `Game::stageID` | `0x02085054` | verified |
| `Game::stageGroup` | `0x02085058` | verified |
| `Game::localPlayerID` | `0x020850BC` | verified |
| `Game::vsMode` | `0x020850C4` | verified |
| `Net::ggid` | `0x02087E78` | verified |
| `Net::randomBranchAddress` | `0x02087E7C` | candidate |
| `Net::randomCallCount` | `0x02088068` | candidate |
| `Net::random.value` | `0x02088088` | candidate |
| player global candidate block | `0x0208A964..0x0208AA23` | candidate, current trace target |
| actor candidate block | `0x0208BE00..0x0208DFFF` | candidate, current diff target |
| render/process candidate block | `0x023F8300..0x023F853F` | candidate, current diff target |
| `Net::getRandom12()` | `0x0200E550` | verified |
| `Net::getRandom()` | `0x0200E5A0` | verified |
| `Net::syncRandomFull()` | `0x0200E5E8` | verified |
| `Net::syncRandomFast()` | `0x0200E5F4` | verified |
| `Net::Core::shareRandomSeed()` | `0x02010F04` | verified |

## 現在分かっていること

- `Net::random.value` を固定しても、RNG消費順やゲーム上重要な状態が同じ順で進まない場合はスターやアイテムがズレる。
- ただし現在のstaged netplayでは、軽量状態はhost/client間で揃えられる。
  - `stageID`
  - `stageGroup`
  - `vsMode`
  - `localPlayerID`
  - `ggid`
  - `Net::random.value`
  - `Net::randomCallCount`
  - `Net::randomBranchAddress`
- `-StateSync` の軽量hashは5100フレームまでmismatchなしで通る。
- `-StateSyncExtended` ではmismatchが出るが、分解結果では `basic=1`、`playerGlobal=1`、`actorCandidate=0`、`renderCandidate=0`。
- つまり、現在見えている差分はプレイヤーの得点・星・コイン等のglobal状態ではなく、actor候補領域とrender/process候補領域に集中している。
- frame 4500 RAM dump比較では以下の傾向。
  - player block `0x0208A964..0x0208AA23` はhost/clientで差分0。
  - actor block `0x0208BE00..0x0208DFFF` は少数のID/ポインタ/リスト順序らしき差分。
  - render block `0x023F8300..0x023F853F` は描画リスト/プロセスリスト順序らしき差分。

## Debugビルドクラッシュの原因と修正

DebugビルドでNSMB起動中に落ちていた問題は解消済み。

- 最初に壊れたコミットは `18017082`。
- 原因は `LocalMP::SendPacketGeneric()` で `type &= 0xFFFF` をpacket header作成前に移動したこと。
- `SendReply()` は `2 | (aid << 16)` の上位16bitにAIDを載せるため、この変更でreply packetのAIDが消えていた。
- 結果として `RecvReplies()` が `aid=0` と解釈し、`packets[(aid-1)*1024]` に書いてメモリ破壊していた。
- 修正済み。packet headerには元の `type` を使い、FIFO/command分岐にだけ `type & 0xFFFF` を使う。

検証:

- `cmake --build build\debug-windows-x86_64 --target melonDS --config Debug`
- `.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 1800`
- `.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200`
- Local MP trace有効の1800フレームroute smoke

## 次にやること

1. actor/render候補領域の差分をさらに分類し、ゲーム上重要なactor状態か、単なるリスト順序・描画順序かを切り分ける。
2. Big Star actor、8コインアイテム、ランダムステージ、勝敗・タイマー・スコアなど、対戦で同期すべき状態を個別に特定する。
3. 重要状態だけを同期・固定するメモリパッチまたはROMパッチの最小実装を作る。
4. 入力同期netplayと重要状態同期を結合し、ローカル2プロセスで2PC相当の検証を継続する。

## よく使う検証コマンド

```powershell
# 1インスタンス smoke
.\scripts\run-nsmb-smoke.ps1 -Frames 180

# 2 EmuInstance smoke
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180

# Mario vs Luigi route smoke
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200

# staged route + netplay smoke
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071

# 軽量ゲーム状態trace付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace

# 軽量ゲーム状態hash同期付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -StateSync

# 候補領域別hash同期。mismatch検出用なので失敗が期待結果になることがある。
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -StateSync -StateSyncExtended

# 指定フレームのMainRAM dump付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -RamDumpFrames 4500
```

## 主要な環境変数

- `MELONDS_NSML_TEST=1`: 自動検証フックを有効化。
- `MELONDS_NSML_TEST_INSTANCES`: 起動するテスト用インスタンス数。
- `MELONDS_NSML_TEST_FRAMES`: 自動終了フレーム。
- `MELONDS_NSML_INPUT_SCRIPT`: 入力スクリプト。
- `MELONDS_NSML_POC=1`: 入力同期netplay PoCを有効化。
- `MELONDS_NSML_ROLE=host|client`: netplay role。
- `MELONDS_NSML_PEER`: client側の接続先。
- `MELONDS_NSML_PORT`: ENet port。
- `MELONDS_NSML_LOCAL_INSTANCE`: ローカル入力を担当するインスタンス。
- `MELONDS_NSML_NETPLAY_START_FRAME`: 入力同期開始フレーム。
- `MELONDS_NSML_NET_RANDOM_AUTO=1`: Mario vs Luigi状態検出時に `Net::random.value` を自動注入。
- `MELONDS_NSML_NET_RANDOM_VALUE`: 注入するRNG seed。
- `MELONDS_NSML_RANDOM_TRACE`: `Net::getRandom()` のcaller/value/countをCSV出力。
- `MELONDS_NSML_GAME_STATE_TRACE`: 軽量ゲーム状態をCSV出力。
- `MELONDS_NSML_GAME_STATE_TRACE_INTERVAL`: ゲーム状態trace間隔。デフォルト60フレーム。
- `MELONDS_NSML_GAME_STATE_TRACE_EXTENDED=1`: 重い候補領域hashもCSV出力。通常検証では使わない。
- `MELONDS_NSML_STATE_SYNC=1`: netplay中に軽量ゲーム状態hashを相互送信。
- `MELONDS_NSML_STATE_SYNC_EXTENDED=1`: player/actor/render候補領域hashも相互送信。
- `MELONDS_NSML_STATE_SYNC_INTERVAL`: 状態hash送信間隔。デフォルト60フレーム。
- `MELONDS_NSML_HASH_LOG`: RAM hash CSV。
- `MELONDS_NSML_SCREEN_HASH=1`: hash CSVへframebuffer hashを追加。
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump出力先。
- `MELONDS_NSML_RAM_DUMP_FRAMES`: RAM dump対象フレームまたは範囲。
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG出力先。
- `MELONDS_NSML_FIXED_RTC`: RTC固定。
- `MELONDS_NSML_DISABLE_JIT=1`: JIT無効化。

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にする。
- 実PC/WAN検証へ進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ設定、必要なBIOS/firmware/save状態が必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを最新化する。
- 古い「次にやること」や解決済みブロッカーは残し続けず、現在の状態に合わせて書き換える。
