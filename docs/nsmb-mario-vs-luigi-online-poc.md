# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、melonDSフォーク上でWAN越しに対戦できる形へ近づける。

最終的な狙いは「DSローカル無線フレームをそのままWANへ流す」ことではなく、NSMBの対戦で意味のあるゲーム状態と入力を同期し、2PC間で同じ試合を維持すること。

## 現在の方針

Local MPの完全決定性だけに賭ける方針は採らない。

これまでの検証で、同一PC・同一ROM/save・固定RTC・JIT無効・固定RNG seedでも、melonDSの2 EmuInstance + Local MP経路は実行ごとに揺れることが分かった。Wi-Fi reply slotの準備タイミングやCMD受信順がずれ、Mario vs Luigi到達後のRAM hashや画面状態が一致しないことがある。

そのため、今後は次の方針に寄せる。

1. **2 EmuInstance + Local MPは足場として使う**
   - Mario vs Luigiへ到達するための起動・観測・テスト基盤として維持する。
   - ただし、最終的な対戦の正しさをLocal MPの完全決定性には依存させない。

2. **NSMB側の重要状態同期を本筋にする**
   - stage / match setup
   - `Net::random` seed、call count、RNG消費順
   - Big Starや8コインアイテムなどのランダム生成結果
   - player / actor / score / timer / win state
   - これらをROMパッチ、メモリパッチ、またはmelonDS側のフックで固定・同期する。

3. **入力同期netplayは残すが、入力だけ同期では終わらせない**
   - 入力遅延lockstepは引き続き使う。
   - ただし、入力列が同じでもLocal MPやゲーム内部状態がずれる場合は、NSMB側の状態同期で補正する。

4. **長期的にはLocal MP依存を減らす**
   - まずはLocal MPで試合開始まで進め、重要状態を観測・固定する。
   - 必要なら段階的に、対戦中の重要状態をゲーム側同期へ置き換える。
   - 最終形は「NSMB Mario vs Luigi専用netplay runner / patch」に近いものになる可能性が高い。

## 実装済み

- NSMB Mario vs Luigi向け自動検証フック。
- 1プロセス内2 EmuInstance起動テスト。
- 入力スクリプト実行。
- スクリーンショット、RAM dump、RAM hash、任意フレームsavestate。
- 固定RTC、JIT無効化、frame barrier、netplay frame barrier。
- ENetによる入力遅延lockstep PoC。
- host/client match seed配布。
- `MELONDS_NSML_NET_RANDOM_AUTO=1` によるMario vs Luigi状態検出時の `Net::random.value` 注入。
- 日本版 `A2DJ` 向けの主要シンボル移植。
- `Net::getRandom()` / `Net::getRandom12()` / `Net::syncRandom*()` 周辺の解析メモ。
- Local MP packet / Wi-Fi MP reply slot trace。
- optional screen hash。
- `MELONDS_NSML_GAME_STATE_TRACE` によるNSMBゲーム状態CSV trace。

## 重要な解析済みアドレス

対象ROMは日本版 `A2DJ`。

| 項目 | アドレス | 状態 |
| --- | --- | --- |
| `Game::stageID` | `0x02085054` | verified |
| `Game::stageGroup` | `0x02085058` | verified |
| `Game::vsMode` | `0x020850C4` | verified |
| `Net::ggid` | `0x02087E78` | verified |
| `Net::randomBranchAddress` | `0x02087E7C` | candidate |
| `Net::randomCallCount` | `0x02088068` | candidate |
| `Net::random.value` | `0x02088088` | candidate |
| `Net::getRandom12()` | `0x0200E550` | verified |
| `Net::getRandom()` | `0x0200E5A0` | verified |
| `Net::syncRandomFull()` | `0x0200E5E8` | verified |
| `Net::syncRandomFast()` | `0x0200E5F4` | verified |
| `Net::Core::shareRandomSeed()` | `0x02010F04` | verified |

## 現在のブロッカー

- Local MP経路は試合開始の足場としてまだ揺れる。これは最終方針の中核ブロッカーではないが、観測自動化の安定性には影響する。
- `Net::random.value` を固定しても、RNG消費順やゲーム上重要な状態が同じ順に進まない場合はスターやアイテムがずれる。
- Big Star以外に、8コインアイテム、ランダムステージ、actor生成順なども同期対象になり得る。
- どの状態を同期すれば対戦として成立するか、ゲーム側の状態traceを増やして分類する必要がある。

## 次にやること

1. `MELONDS_NSML_GAME_STATE_TRACE` を使い、Mario vs Luigi到達、RNG seed注入、スター生成前後で、host/clientと複数runの状態差分を比較する。
2. 差分がRNG streamだけなら `Net::random` 同期を強化する。
3. 差分がactor/player/state machine側なら、次の同期対象アドレスを特定する。
4. 8コインアイテムは自動化が難しいため、Big Starと試合開始状態の安定化後に扱う。

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
- `MELONDS_NSML_GAME_STATE_TRACE`: `stageID` / `stageGroup` / `vsMode` / `ggid` / `Net::random` 状態をCSV出力。
- `MELONDS_NSML_GAME_STATE_TRACE_INTERVAL`: ゲーム状態traceの出力間隔。デフォルトは60フレーム。
- `MELONDS_NSML_GAME_STATE_TRACE_EXTENDED=1`: player系グローバルと候補領域hashもCSV出力。重いので差分調査時だけ使う。
- `MELONDS_NSML_HASH_LOG`: RAM hash CSV。
- `MELONDS_NSML_SCREEN_HASH=1`: hash CSVへframebuffer hashを追加。負荷でタイミングが変わる可能性があるため必要時のみ使う。
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump出力先。
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG出力先。
- `MELONDS_NSML_FIXED_RTC`: RTC固定。
- `MELONDS_NSML_DISABLE_JIT=1`: JIT無効化。

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にする。
- 実PC/WAN検証に進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ設定、必要なBIOS/firmware/save状態が必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを最新化する。
- 古い「次にやること」や解消済みブロッカーは残し続けず、現在の状態に合わせて書き換える。

## 直近の検証

- `cmake --build build\debug-windows-x86_64 --target melonDS --config Debug` は成功。
  - `applocal.ps1` が `dumpbin` / `llvm-objdump` / `objdump` 不在警告を出すが、実行ファイルのリンク自体は成功している。
- `MELONDS_NSML_GAME_STATE_TRACE` を有効にして `.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 600` が成功。
  - `logs\game-state-trace-smoke\game-state.csv` に両インスタンスのゲーム状態traceが出力された。
- trace無効時はゲーム状態読み取り自体を行わないようにした。Local MPルートへの不要なタイミング影響を避けるため。
- Debugビルドの1500フレーム付近アクセス違反は解消済み。
  - `65d2995e` と `798b2475` は `run-nsmb-mvl-route-smoke.ps1 -Frames 1800` 成功。
  - `18017082` で初回再現。原因は `LocalMP::SendPacketGeneric()` の `type &= 0xFFFF` をヘッダ作成前へ移動したことで、reply packetの上位16bit AIDが消えたこと。
  - その結果 `RecvReplies()` が `aid=0` と解釈し、`packets[(aid-1)*1024]` へ書いてメモリ破壊していた。
  - 修正後、通常route 4200フレーム、ゲーム状態trace有効1800フレーム、Local MP trace有効1800フレームが成功。
- ゲーム状態traceは軽量版をデフォルトに戻した。
  - player/global/candidate hashを常時出すとLocal MP到達が揺れるケースがあった。
  - 拡張traceは `MELONDS_NSML_GAME_STATE_TRACE_EXTENDED=1` の明示指定時だけ有効。
  - 軽量trace有効の固定RNG route 4200フレームは成功。
