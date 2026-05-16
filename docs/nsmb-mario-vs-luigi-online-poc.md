# NSMB Mario vs Luigi Online PoC

## 目的

New Super Mario Bros. DS 日本版 `A2DJ` のローカル対戦専用モード「Mario vs Luigi」を、melonDSフォーク上でWAN越しに対戦できる形へ近づける。

最終的な狙いは「DSローカル無線フレームをそのままWANへ流す」ことではない。NSMBの対戦で意味のあるゲーム状態と入力を同期し、2台のPC間で同じ試合を維持する。

## 現在の方針

Local MPの完全な決定性だけに依存する方針は採用しない。

これまでの検証で、同一PC、同一ROM/save、固定RTC、JIT無効、固定RNG seedでも、melonDSの2 EmuInstance + Local MP経路は実行ごとに揺れることが分かった。試合開始や基本状態までは揃えられるが、RAM hashやWi-Fi/描画リスト系の内部状態は一致しないことがある。

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
- 軽量ゲーム状態traceにVS Battle Star候補Actor `id=0x010c` の出現有無、GUID、座標を追加。
- 軽量ゲーム状態traceにPlayer Actor `id=0x0015` 2体のGUID、settings、座標を追加。
- `MELONDS_NSML_GAME_STATE_TRACE_EXTENDED=1` による重い候補領域trace。
- `MELONDS_NSML_STATE_SYNC=1` によるnetplay中の軽量ゲーム状態hash交換。
- `MELONDS_NSML_STATE_SYNC_EXTENDED=1` による候補領域別hash交換。
- staged netplay smokeでhost/client別のゲーム状態traceとRAM dumpを出せるようにした。
- route smokeでもゲーム状態traceとRAM dumpを指定できるようにした。
- route smokeでRNG seed、RNGパッチ無効、VS Battle Star snap診断フックを指定できるようにした。
- route smokeが過去のstaged netplay環境変数を引き継がないようにし、固定RTC/JIT無効も明示した。
- staged netplay smokeでsavestate load/saveを指定できるようにした。到達済みMvsL状態から短い同期テストを回すための検証用。ただし、現状はWi-Fi/Local MP試合の継続復元には使えない。
- `tests/nsmb_after_state_star_probe.inputs` を追加した。frame-5000 MvL savestateから相対入力でスター取得を試す診断用。

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
| Wi-Fi/MB candidate block | `0x0208BE00..0x0208DFFF` | candidate, current diff target |
| render/process candidate block | `0x023F8300..0x023F853F` | candidate, current diff target |
| `Net::getRandom12()` | `0x0200E550` | verified |
| `Net::getRandom()` | `0x0200E5A0` | verified |
| `Net::syncRandomFull()` | `0x0200E5E8` | verified |
| `Net::syncRandomFast()` | `0x0200E5F4` | verified |
| `Net::Core::shareRandomSeed()` | `0x02010F04` | verified |
| Player actor | object id `0x0015` | traced |
| VS Battle Star candidate actor | object id `0x010c` | candidate, traced |

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
- `-StateSync` の軽量hashはVS Battle Star候補Actor座標込みでは5100フレームまでmismatchなしで通った。
- Player Actor座標も軽量hashへ入れると、staged netplayではmismatchが出る。
  - `logs\staged-player-vsstar-trace` では、星座標は一致する一方で、Player ActorのX/Yがframe4500以降にhost/clientでズレる。
  - これは「星の初期位置が一致しても、プレイヤー物理状態まで入力だけで完全一致する」とは言えないことを示す。
  - 以後はPlayer Actor位置も重要状態同期の候補に含める。
- `MELONDS_NSML_STATE_APPLY=1` のhost権威補正PoCを追加した。
  - WireGameStateにStar/Player/RNGの実値を載せ、client側で受信済み状態をMainRAMへ書き戻せる。
  - `logs\staged-state-apply-verified` では、host/clientのゲーム状態trace上のPlayer Actor座標が一致し、staged smokeのStateApply用trace比較もpass。
  - `logs\staged-state-apply-6500` と `logs\staged-state-apply-star-after6000` でも6500/7000フレームまで通信断なしでpass。後者ではStar Actor GUIDが `0x23` から `0x2c` へ変わる再ロード/再生成らしき状態もhost/clientで一致。
  - ただし既存の `game state mismatch` ログは「適用前に送られた状態」との比較でも出るため、StateApplyの成否判定にはtrace比較を使う必要がある。
- `-StateSyncExtended` ではmismatchが出るが、分解結果では `basic=1`、`playerGlobal=1`、`wifiCandidate=0`、`renderCandidate=0`。
- つまり、現在見えている差分はプレイヤーの得点・星・コイン等のglobal状態ではなく、Wi-Fi/MB候補領域とrender/process候補領域に集中している。
- frame 4500 RAM dump比較では以下の傾向。
  - player block `0x0208A964..0x0208AA23` はhost/clientで差分0。
  - `0x0208BE00..0x0208DFFF` は、公開シンボルのUSアドレスからJP移植オフセットを戻すと `Wifi::parentsBssDesc` 周辺に相当する。actor本体ではなくLocal MP/Wi-Fi側のBSS差分と見る。
  - render block `0x023F8300..0x023F853F` は描画リスト/プロセスリスト順序らしき差分。
  - このため、現時点で見えているextended mismatchは、ゲーム上重要なスター・スコア・プレイヤー状態の不一致とはまだ判断しない。
- `tools/nsmb_mvl_ram_probe.py --a2dj-process-lists` でJP process listを辿る診断を追加した。
  - `logs\staged-netplay-ramdump-4500` のhost/client inst0/inst1では、execute/render/create process listの意味的なobject集合は一致。
  - 少なくともこのフレームでは、top-level process list差分ではなくWi-Fi/描画内部リスト差分が主に見えている。
- `tools/nsmb_mvl_ram_probe.py --a2dj-object-scan` でheap上のBase/Actor風オブジェクトを直接列挙できるようにした。
  - `logs\mvl-seed-00000100-state-source-frame5000` のframe4100 -> frame5000比較では、frame5000で `id=0x010c` が新規出現する。
  - `id=0x010c` は座標 `x=0x00348000, y=0xfff28000, z=0x00080000` で、スクリーンショット上のVS Battle Star位置と対応する候補。
  - `logs\route-vsstar-trace` と `logs\staged-vsstar-trace` では、frame4380以降の `id=0x010c` がhost/clientおよび2 EmuInstance間で同じ `guid=0x23`、同じ座標になることを確認。
  - 現時点では `MvsLObject268/VSBattleStarCandidate` として扱い、次に取得・再生成時の同期対象にする。
- Player Actor座標traceにより、`tests\nsmb_mario_vs_luigi_star_probe.inputs` ではframe4380以降に操作対象がスター近傍へ寄ることを確認。
- `tests\nsmb_mario_vs_luigi_star_collect_after6000.inputs` を追加した。frame6000以降に左へ戻して、Star ActorのGUID変化を再現する診断用。
- 診断用の `MELONDS_NSML_VS_STAR_SNAP_FRAME` は、星Actor位置を書き換えられるが、それだけでは取得判定・再生成までは起きなかった。`id=0x010c` が単純な取得当たり判定本体ではない、または追加内部状態/別Actor/処理タイミングが必要な可能性がある。
- RNGパッチなしの過去ログではframe5071でスター取得・再生成由来らしい `Net::random` 消費が観測されているが、現行のクリーンroute再現では条件が一致しなかった。以後は固定RTC/JIT無効/RNG seed明示を前提に再検証する。
- savestate loadからの短いstaged netplayは、melonDSの状態hashとしては通るが、現状ではゲーム内通信復元に失敗している。
  - `logs\staged-netplay-state-load-no-rng-repatch`: 900フレーム、`-StateSync` mismatchなし。
  - ただしスクリーンショットは「通信が切断されました」画面。
  - state-load時にmatch seedが `Net::random.value` へ再注入される問題は修正済み。
  - savestate load直後に `MP_Begin` を補っても通信断は解消しなかった。
  - melonDS本体の `Wifi::DoSavestate()` にもWi-Fiとsavestateの相性問題が示されているため、state-load経路は本筋ではなく診断用に留める。

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

1. 過去ログのframe5071付近と現行ログを比較し、スター取得・再生成時に変わるActor/manager状態を特定する。
2. VS Battle Star `id=0x010c` が取得判定本体なのか、spawn marker/manager側状態なのかを切り分ける。
3. スター取得そのものを自動入力または追加フックで再現し、スコア/星数が増えるケースでStateApplyが保てるか確認する。
4. スター/プレイヤーの重要状態がズレる場合は、Actor座標・settings・RNG seed/call count・manager slot状態のどこを同期すべきか切り分け、最小メモリパッチを作る。
5. 8コインアイテムは自動化が難しいため一旦保留し、スター同期の見通しが立ってから同じActor trace方式で対象を特定する。
6. ランダムステージ、勝敗・タイマー・スコアなど、対戦で同期すべき状態を個別に特定する。
7. 入力同期netplayと重要状態同期を結合し、ローカル2プロセスで2PC相当の検証を継続する。
8. state-load経路は必要になった場合だけ追加調査する。現時点では最終対戦実現の主経路にしない。

## よく使う検証コマンド

```powershell
# 1インスタンス smoke
.\scripts\run-nsmb-smoke.ps1 -Frames 180

# 2 EmuInstance smoke
.\scripts\run-nsmb-two-instance-smoke.ps1 -Frames 180

# Mario vs Luigi route smoke
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4200

# route smokeでVS Battle Star候補Actor traceとRAM dumpを取る
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 5100 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -LogDir logs\route-vsstar-trace -GameStateTrace -GameStateTraceInterval 60 -RamDumpFrames 5000

# route smokeで診断用にVS Battle Star候補ActorをPlayer Actor座標へ寄せる
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 5600 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -LogDir logs\route-star-snap-4440-clean -GameStateTrace -GameStateTraceInterval 10 -VsStarSnapFrame 4440 -VsStarSnapPlayerSlot 0

# staged route + netplay smoke
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071

# 軽量ゲーム状態trace付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace

# 軽量ゲーム状態hash同期付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -StateSync

# VS Battle Star候補Actor込みのstaged trace
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -GameStateTrace -StateSync

# host権威の重要状態適用PoC。mismatchログは適用前比較でも出るため許容し、traceを比較する。
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -GameStateTrace -StateSync -StateApply -StateSyncInterval 10 -AllowStateMismatch

# Star Actor GUID変化を含む長めのStateApply検証
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 7000 -NetplayStartFrame 4500 -Port 8071 -InputScript tests\nsmb_mario_vs_luigi_star_collect_after6000.inputs -GameStateTrace -StateSync -StateApply -StateSyncInterval 10 -AllowStateMismatch

# 候補領域別hash同期。mismatch検出用なので失敗が期待結果になることがある。
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -StateSync -StateSyncExtended

# 指定フレームのMainRAM dump付き
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 5100 -NetplayStartFrame 4500 -Port 8071 -GameStateTrace -RamDumpFrames 4500

# MvL到達済みsavestateから短いnetplay同期テスト
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 900 -NetplayStartFrame 120 -Port 8071 -InputScript tests\nsmb_after_state_neutral.inputs -StateLoadDir logs\mvl-seed-00000100-state-source-frame5000\state-frame5000 -StateLoadFrame 1 -GameStateTrace -StateSync -WaitForPeerAtNetplayStart

# RAM dumpからA2DJ process listを比較
python tools\nsmb_mvl_ram_probe.py --rng-timeline-only --a2dj-process-lists logs\staged-netplay-ramdump-4500\ram-host\inst0_frame004500_mainram.bin logs\staged-netplay-ramdump-4500\ram-client\inst0_frame004500_mainram.bin

# RAM dumpからA2DJ object候補を比較
python tools\nsmb_mvl_ram_probe.py --rng-timeline-only --a2dj-object-scan logs\mvl-seed-00000100-state-source-frame5000\ram\inst0_frame004100_mainram.bin logs\mvl-seed-00000100-state-source-frame5000\ram\inst0_frame005000_mainram.bin
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
- `MELONDS_NSML_STATE_SYNC_EXTENDED=1`: player/Wi-Fi/render候補領域hashも相互送信。
- `MELONDS_NSML_STATE_SYNC_INTERVAL`: 状態hash送信間隔。デフォルト60フレーム。
- `MELONDS_NSML_HASH_LOG`: RAM hash CSV。
- `MELONDS_NSML_SCREEN_HASH=1`: hash CSVへframebuffer hashを追加。
- `MELONDS_NSML_RAM_DUMP_DIR`: MainRAM dump出力先。
- `MELONDS_NSML_RAM_DUMP_FRAMES`: RAM dump対象フレームまたは範囲。
- `MELONDS_NSML_SCREENSHOT_DIR`: PNG出力先。
- `MELONDS_NSML_STATE_LOAD_DIR`: savestate load元ディレクトリ。
- `MELONDS_NSML_STATE_LOAD_FRAME`: savestate loadを行うテストフレーム。
- `MELONDS_NSML_STATE_SAVE_DIR`: savestate save先ディレクトリ。
- `MELONDS_NSML_STATE_SAVE_FRAME`: savestate saveを行うテストフレーム。
- `MELONDS_NSML_FIXED_RTC`: RTC固定。
- `MELONDS_NSML_DISABLE_JIT=1`: JIT無効化。

## ユーザー依存

- ROMは `roms/nsmb.nds` に配置済みの日本版 `A2DJ` を前提にする。
- 実PC/WAN検証へ進む段階では、相手PC側にも同じmelonDSビルド、同じROM、同じ設定、必要なBIOS/firmware/save状態が必要。

## 運用ルール

- 実装状況、ブロッカー、次の作業はこのファイルを最新化する。
- 古い「次にやること」や解決済みブロッカーは残し続けず、現在の状態に合わせて書き換える。
