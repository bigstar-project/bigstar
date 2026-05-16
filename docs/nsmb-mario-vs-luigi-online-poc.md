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
- PlayerをVS Battle Starへ複数フレーム固定する診断フックを追加した。現在位置、前フレーム位置、速度をまとめて補正する。
- route smokeが過去のstaged netplay環境変数を引き継がないようにし、固定RTC/JIT無効も明示した。
- staged netplay smokeでsavestate load/saveを指定できるようにした。到達済みMvsL状態から短い同期テストを回すための検証用。ただし、現状はWi-Fi/Local MP試合の継続復元には使えない。
- `tests/nsmb_after_state_star_probe.inputs` を追加した。frame-5000 MvL savestateから相対入力でスター取得を試す診断用。
- `WireGameState` / `StateApply` にプレイヤーの星数、コイン、スコア、表示星数、死亡数、取得星候補を追加した。
- `WireGameState` / `StateApply` にPlayer Actorの前フレーム座標と速度を追加した。
- `tools/nsmb_mvl_ram_probe.py --a2dj-object-dump` を追加し、指定Actorの周辺メモリをフレーム間比較できるようにした。

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
| VS Battle Star actor candidate | object id `0x0022`, settings `0x00000001` | candidate, traced |
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
- 逆アセンブルの `actor creation r0=0x22` とRAM scanから、スター実体に近い候補として `id=0x0022 settings=0x00000001` を追加した。
  - `logs\route-vsstar-actor-trace` では `guid=0x1b, x=0x00370000, y=0xffef0000` として出現し、画面上のスター位置と対応する。
  - `logs\staged-state-apply-vsstar-actor` では、このActor候補をStateApply trace比較に含めても5100フレームまでpass。
- Player Actor座標traceにより、`tests\nsmb_mario_vs_luigi_star_probe.inputs` ではframe4380以降に操作対象がスター近傍へ寄ることを確認。
- `tests\nsmb_mario_vs_luigi_star_collect_after6000.inputs` を追加した。frame6000以降に左へ戻して、Star ActorのGUID変化を再現する診断用。
- 診断用の `MELONDS_NSML_VS_STAR_SNAP_FRAME` は、星Actor位置を書き換えられるが、それだけでは取得判定・再生成までは起きなかった。`id=0x010c` が単純な取得当たり判定本体ではない、または追加内部状態/別Actor/処理タイミングが必要な可能性がある。
- 診断用の `MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME` も追加した。`logs\route-player-snap-to-star-4380` ではPlayer Actorを `id=0x010c` 座標へ、`logs\route-player-snap-to-star-actor-z-4380` では `id=0x0022 settings=1` 座標へ寄せたが、どちらも取得状態は確認できなかった。座標だけでは取得判定を起こせず、追加内部状態/速度/当たり判定処理/manager slot状態が必要な可能性が高い。
- `MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME` / `END_FRAME` でPlayerをスター位置へ数十フレーム固定すると、`player0BattleStars` が `0x1` から `0x2` へ増えるケースを再現できた。
  - `logs\route-player-stick-state-fields-4380-4440`: route smokeでframe4640以降に星数増加を確認。
  - 1フレームsnapだけでは不十分で、前フレーム座標と速度を含めた継続補正が必要だった。
- `StateApply` はプレイヤーglobal状態込みで、スター取得後の重要状態をhost/client間で揃えられるところまで進んだ。
  - `logs\staged-state-apply-player-star-score-start4200-pass4670`: netplay開始4200、スター取得診断4380-4440、比較開始4670でpass。
  - 取得直後から数十フレームは受信済み状態の適用ラグが見えるため、StateApplyの検証では補正ウォームアップ後のtrace比較を使う。
- Player Actorの前フレーム座標・速度をStateApplyへ含めた後も、スター取得相当のstaged検証はpass。
  - `logs\staged-state-apply-player-transform-star-score`: 比較開始4670でpass。
  - 既存の `game state mismatch` ログはframe4385付近だけになり、座標traceは4440以降でhost/clientが一致している。
- Star Actorはhost/clientでGUIDが異なる再生成ケースがあるため、StateApply時にGUID一致で見つからない場合は `id=0x0022/settings=1` のローカルActorへ座標適用するフォールバックを追加した。
  - `logs\staged-state-apply-current-star-score-regression`: 最新コードでもスター取得相当の4700フレームstaged検証はpass。
  - `logs\staged-state-apply-star-fallback-respawn-7000`: 7000フレームstaged自体はpassしたが、この実行ではスター取得が起きていなかったため、再生成同期の確定証拠には使わない。
- staged host/clientの到達ルートはまだLocal MP探索タイミングに揺れがある。
  - 失敗時はクラッシュではなく、片側が「ルイージをさがしています」または参加確認画面に残る。
  - `tests\nsmb_mario_vs_luigi_star_probe.inputs` にMario/Luigi両側の確認A再送を追加し、`logs\staged-state-apply-menu-retries-6200` では6200フレーム検証がpass。
  - この6200フレーム検証では、スター取得後もhost/client双方で `player0BattleStars=0x4`、実体Star Actor座標、RNG call countが一致した。
  - `scripts\run-nsmb-mvl-netplay-staged-retry.ps1` を追加した。`logs\staged-retry-6200` では1回目が到達フレークで失敗し、2回目で6200フレーム検証がpass。
  - ただしLocal MP探索の揺れ自体は残っているため、長期的には起動到達の自動リトライ、またはNSMB側patchで参加確認を短絡する必要がある。
- `logs\staged-retry-7000\attempt-02` では7000フレーム検証がpassしたが、スクリーンショット上はhost/clientのStageCamera/表示がまだズレる。
  - Player Actor、実体Star Actor、星数、RNG call countは一致している。
  - `logs\staged-retry-6700-ram6600\attempt-02` のRAM dump比較では、StageCamera `id=0x013c` と `MvsLObject267 id=0x010b` 周辺に差分が出ている。
  - 次はStageCamera / MvsLObject267 / stage表示状態をStateApply対象に追加する。
- `id=0x010c` は再生成タイミングがhost/clientで1 trace tick程度ずれることがあるため、strictなStateApply比較からは外した。実体スター同期の判定は `id=0x0022/settings=1` のStar Actor座標とプレイヤーglobal状態を優先する。
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
- 2026-05-16に最新コードでも `.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 1800 -LogDir logs\debug-crash-regression-1800 -Seed 0x00000100` がpass。

## 次にやること

1. staged host/clientがMvsLへ入れない揺れを減らす。まずは入力再送・起動待ち・自動リトライで検証足場を安定化し、必要ならNSMB側patchで参加確認を短絡する。
2. StageCamera / MvsLObject267 / stage表示状態をStateApply対象に追加し、スクリーンショット上のhost/client差分を減らす。
3. スター取得後に次スターが再生成されるまで長く走らせ、実体Star Actor / RNG timeline / player global / 画面表示がhost/clientで揃うか確認する。
4. StateApplyの適用ラグをさらに減らす。現状は通信経由の補正なので、trace上は数フレーム遅れのmismatchがあり得る。
5. 診断フックなしの入力スクリプトでスター取得できるルートを作る。難しければ、しばらくはstick診断を回帰テストとして使う。
6. Star Actor側も必要なら前フレーム座標・速度・stateTypeをStateApply対象に拡張する。
7. 8コインアイテムは自動化が難しいため一旦保留し、スター同期の見通しが立ってから同じActor trace方式で対象を特定する。
8. ランダムステージ、勝敗・タイマー・スコアなど、対戦で同期すべき状態を個別に特定する。
9. 入力同期netplayと重要状態同期を結合し、ローカル2プロセスで2PC相当の検証を継続する。
10. state-load経路は必要になった場合だけ追加調査する。現時点では最終対戦実現の主経路にしない。

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

# route smokeで診断用にPlayer ActorをVS Battle Star候補座標へ寄せる
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 5300 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -LogDir logs\route-player-snap-to-star-4380 -GameStateTrace -GameStateTraceInterval 10 -PlayerSnapToStarFrame 4380 -PlayerSnapToStarSlot 0

# route smokeで診断用にPlayer ActorをVS Battle Starへ固定し、スター取得相当の星数増加を確認する
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Frames 4700 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -LogDir logs\route-player-stick-state-fields-4380-4440 -GameStateTrace -GameStateTraceInterval 10 -GameStateTraceExtended -PlayerStickToStarStartFrame 4380 -PlayerStickToStarEndFrame 4440 -PlayerStickToStarSlot 0

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

# スター取得相当の星数増加を含むStateApply検証
.\scripts\run-nsmb-mvl-netplay-staged-smoke.ps1 -Frames 4700 -NetplayStartFrame 4200 -Port 8071 -InputScript tests\nsmb_mario_vs_luigi_star_probe.inputs -GameStateTrace -GameStateTraceInterval 10 -GameStateTraceExtended -StateSync -StateApply -StateApplyCompareStartFrame 4670 -StateSyncInterval 5 -AllowStateMismatch -PlayerStickToStarStartFrame 4380 -PlayerStickToStarEndFrame 4440 -PlayerStickToStarSlot 0

# Local MP到達フレークを自動リトライしながら6200フレームまで検証
.\scripts\run-nsmb-mvl-netplay-staged-retry.ps1 -Attempts 2 -Frames 6200 -Port 8140 -LogDir logs\staged-retry-6200 -WaitTimeoutMs 480000

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

# 指定Actorの周辺メモリをフレーム間比較
python tools\nsmb_mvl_ram_probe.py --rng-timeline-only --a2dj-object-dump --object-id 0x0022 --object-settings 0x1 --object-size 0x120 logs\route-player-stick-star-4380-4440-long\ram-mvl-route\inst0_frame004440_mainram.bin logs\route-player-stick-star-4380-4440-long\ram-mvl-route\inst0_frame004455_mainram.bin
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
- `MELONDS_NSML_STATE_APPLY=1`: client側でhostから受け取った重要状態をMainRAMへ適用。
- `MELONDS_NSML_PLAYER_SNAP_TO_STAR_FRAME`: 診断用に指定フレームでPlayerをスター位置へ移動。
- `MELONDS_NSML_PLAYER_STICK_TO_STAR_START_FRAME` / `END_FRAME`: 診断用にPlayerをスター位置へ継続固定。
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
