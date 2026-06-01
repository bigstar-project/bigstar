# NSMB Mario vs Luigi Rollback Design Notes

## 2026-06-01 latest rollback snapshot focus

ユーザー指示により、delay方式とのhybrid検討はいったん外し、軽いcheckpoint/snapshotとして現実的なrollback方式を実験している。

現在の最有力候補は `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200`。これはMain RAM全体や通常savestateではなく、NSMB向けMain RAM range snapshotに、CPU/timer/scheduler/DMA/IRQ/IPC/WRAMなどの小さいcore進行状態と、GPU3DのFIFO/matrix/pipeline/register系だけを足す方式。checkpoint sizeは最新range補強後で `251,095` bytesまでに収まり、`savestate` や `corelite` よりかなり実用寄り。

ただし、まだ完全な「ROM解析でactor/global構造を静的に確定した案D」ではない。現在のrange setは、coredelta/restore diffと実行時Main RAM観測で見つけたNSMB global/actor/heap周辺を使っている。ROMの関数・構造体・actor tableを本格的に逆引きして、必要状態を名前付き構造として確定する作業はまだ途中ではなく、これからの段階。

今回の追加実験では、`MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL` を追加し、動的object/range探索結果を数十フレーム単位でキャッシュできるようにした。`scanInterval=30` では、`0x200` 候補のcheckpoint bytesは約 `248-251KB`、保存平均時間は約 `7.9ms` から約 `0.39-0.41ms` まで下がった。

実rollback検証用に `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO` も追加した。これはテスト時だけ予測remote inputを周期的に1bit外し、通常のprediction mismatch/resimulate経路を強制する。restore diffで見つけた未復元ページを追加し、game/global周辺とheap/object周辺を数KB補強した。

Verification:

- Build: `cmake --build --preset release-windows-x86_64 --parallel` passed.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-2600-20260601`: 2600-frame split local-input smoke passed. Client frame 2520: `bytesLast=247,355`, `saveAvgUs=391`, `scanInt=30`, `scanRefresh=55`, `scanCacheHits=1595`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-3600-20260601`: 3600-frame split local-input smoke passed. Client frame 3600: `bytesLast=247,355`, `saveAvgUs=393`, `scanRefresh=91`, `scanCacheHits=2639`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-restoreprobe-2600-20260601`: 2600-frame smoke passed, but restore probe did not force restoreOps. その後、prediction probeで通常のmismatch/resimulate経路を強制できるようにした。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe30-extra-gameglobals-2600-20260601`: prediction probe modulo 30で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=248,287`, `saveAvgUs=395`, `restoreAvgUs=7,512`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe10-more-diff-ranges-2600-20260601`: prediction probe modulo 10で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=251,095`, `saveAvgUs=391`, `restoreAvgUs=7,786`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe-all-more-diff-ranges-2600-20260601`: prediction probe modulo 1はframe 1890で `inputPlayer0Held` のspeculative差分によりwrapper比較が停止した。全予測外れstressは現実的WANより過剰だが、追加range探索用の負荷として残す。

Current blocker:

- 実用候補としてはかなり近づいたが、まだ「実行時delta-discovered range + 小さいemulator進行状態」。ROM解析でNSMB actor/global構造を確定している段階ではない。
- `scanInterval=30` は現ルートで成功しているが、spawn/despawnや別ルートでrangeが変わる場面の安全余裕は追加検証が必要。
- 全予測外れstressでは、入力保持フィールドなどspeculative状態の比較で止まる。実用評価では、通常WAN相当のprediction頻度、settle後のactor/object/score収束、体感カクつきの確認を分けて測る必要がある。

Next actions:

- prediction probe modulo 10程度を継続stressとして使い、別input routeや長時間で `251KB / saveAvg 0.4ms / restoreAvg 8ms前後` が維持できるか測る。
- delta-discovered rangeをROM/メモリ解析に戻し、NSMB global/actor/manager/camera/RNG相当へ名前付きで切り分ける。
- `scanInterval` のデフォルト値を上げてよいかは、別ルート・長時間・spawn/despawn検証後に判断する。現時点ではデフォルト1で保守的にしている。

## 2026-06-01 current experiment status

ユーザー指示により、delay方式とのhybrid検討はいったん外し、軽いcheckpoint/snapshotが作れるかだけに焦点を戻した。

Completed:

- 案C寄りのPoCとして、通常savestate互換を捨てたrollback専用 `corelite` backendを追加した。
- `melonDS::NDS::DoRollbackSavestate()` を追加し、通常 `DoSavestate()` がNTRでも常に保存していた16MB Main RAMを、実際の `MainRAMMask + 1` だけ保存するようにした。その他のCPU、DMA、timer、scheduler、GPU/SPU/Wifi等の既存savestate対象は維持している。
- `MELONDS_NSML_ROLLBACK_BACKEND=corelite` / `-RollbackBackend corelite` でPoC rollback backendを選べる。
- `coresparse` backendを追加し、Main RAMのゼロページを省略できるかを試した。
- `coredelta` backendを追加し、keyframeのMain RAMを基準に、各checkpointでは変更ページだけを保存できるようにした。
- 案D寄りのサイズ探索として `nsmbranges` backendを追加した。NSMBのplayer/global/net/stage周辺と検出できる主要actor/object風メモリ範囲だけを保存する。
- `MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL` でdelta keyframe間隔、`MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE` でMain RAMページサイズを調整できる。
- rollback traceへ checkpoint byte/time stats、delta/keyframe数、Main RAM base copy量、delta page size を追加した。

Verification:

- Build: `cmake --build --preset release-windows-x86_64 --parallel` passed.
- Core-lite rollback probe: `logs/codex-rollback-corelite-trace-20260601` passed short split local-input smoke with artificial input delay, prediction mismatches, and resimulation.
- `corelite` checkpoint size was stable at `6,645,137` bytes.
- Same short probe with normal `savestate` backend in `logs/codex-rollback-savestate-trace-20260601` showed `19,228,045` bytes.
- This is about 65% smaller than full savestate, mainly by removing unused NTR upper Main RAM from rollback checkpoints.
- `logs/codex-rollback-corelite-gamestate-20260601` ran both host/client to frame 1500 with game-state traces and rollback resimulation, but the wrapper-level comparison failed at the outer movement-probe check because the short run did not provide the expected movement probe rows. The inner host/client route smoke completed and no rollback restore/resim failure was logged.
- Longer game-state comparison: `logs/codex-rollback-corelite-gamestate-2600-20260601` passed 2600-frame split local-input smoke with game-state comparison enabled. It exercised prediction mismatches and rollback resimulation. Final client-side trace at frame 2520 showed 10 mismatches and 10 resimulations, with checkpoint size still `6,645,137` bytes.
- Timing probe: `logs/codex-rollback-corelite-timing-20260601` showed `corelite` save average around `4.5ms` and restore average around `15-18ms` in the short JIT-enabled synthetic run. `logs/codex-rollback-savestate-timing-20260601` showed normal `savestate` save average around `9.4-9.6ms` and restore average around `19.6ms` under the same style of run.
- `coresparse` timing probe: `logs/codex-rollback-coresparse-timing-20260601` passed, but size was only reduced to `6,054,749` bytes. Save average was around `5.0ms`; zero-page省略だけでは効果が小さい。
- `coredelta` keyframe interval 10: `logs/codex-rollback-coredelta-k10-timing-20260601` passed. Delta checkpoint was around `2.53-2.55MB`, average was around `2.95MB`, restore average was around `18-23ms`.
- `coredelta` keyframe interval 20: `logs/codex-rollback-coredelta-k20-timing-20260601` passed. Average was around `2.75MB`; restore average remained around `21-23ms`.
- `coredelta` keyframe interval 30 with 4KB page: `logs/codex-rollback-coredelta-k30-timing-20260601` passed. Average was around `2.67MB`; delta size was still around `2.53-2.55MB`.
- `coredelta` keyframe interval 30 with 1KB page: `logs/codex-rollback-coredelta-k30-page1024-timing-20260601` passed. Delta size was around `2.48-2.50MB`; average was around `2.62MB`.
- `coredelta` keyframe interval 30 with 256B page: `logs/codex-rollback-coredelta-k30-page256-timing-20260601` passed. Delta size was around `2.46-2.47MB`; average was around `2.60MB`.
- Longer game-state comparison for best current candidate: `logs/codex-rollback-coredelta-k30-page256-gamestate-2600-20260601` passed 2600-frame split local-input smoke with game-state comparison enabled. Host/client both exercised prediction mismatches and resimulation without restore failure. Final traces around frame 2520 showed average checkpoint bytes around `2.60MB`, save average around `3.4-3.5ms`, restore average around `18ms`.
- `nsmbranges` short timing probe: `logs/codex-rollback-nsmbranges-timing-20260601` passed without game-state comparison. Checkpoint size was around `58KB`, restore average was around `0.2-0.6ms`, but save average was around `6.6ms` because the PoC scans Main RAM for objects on every checkpoint.
- `nsmbranges` 2600-frame game-state comparison with the first fixed range set: `logs/codex-rollback-nsmbranges-gamestate-2600-20260601` failed at frame 1950 (`playerActor1X` mismatch).
- `nsmbranges` with all scanned object-like ranges: `logs/codex-rollback-nsmbranges-allobjects-gamestate-2600-20260601` still failed, now at frame 930 (`playerActor0Y` mismatch). Checkpoint size stayed small at around `62-64KB`; restore stayed below `1ms`, but correctness was insufficient.
- `nsmbcoreranges` diagnostic backend was added to split the failure cause. It saves melonDS core state with Main RAM skipped, then applies the NSMB range snapshot. Short timing probe `logs/codex-rollback-nsmbcoreranges-timing-20260601` passed without game-state comparison. Size was around `2,513,397` bytes, save average around `11ms`, restore average around `12-14ms`.
- `nsmbcoreranges` 2600-frame game-state comparison `logs/codex-rollback-nsmbcoreranges-gamestate-2600-20260601` still failed at frame 930 (`playerActor0Y` mismatch). Restoring core state did not fix the failure.
- `nsmbcoreranges` with broad diagnostic ranges (`MELONDS_NSML_ROLLBACK_NSMB_WIDE_RANGES=1`, adding `0x02080000..0x020E0000` and `0x023C0000..0x02400000`) also failed at frame 930 in `logs/codex-rollback-nsmbcoreranges-wide-gamestate-2600-20260601`. Size rose to around `3,144,901` bytes, but correctness did not improve.
- `coredelta`の成功経路に `MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE` を追加し、Main RAM差分ページを256B単位で出せるようにした。930フレーム前後では、既存NSMB rangeが `0x0208xxxx` のgame/global、`0x0219xxxx`/`0x021Bxxxx`/`0x02288400` 付近のheap/object、`0x023FFC00` 付近を取り逃がしていた。
- `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` を追加し、delta/restore diffで見つけた小さな追加rangeをNSMB snapshotへ反映できるようにした。
- `MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1` を追加し、NSMB range復元直後に診断用Main RAM shadow copyと比較して、未復元ページを直接出せるようにした。
- 追加rangeの初回反映だけでは `nsmbcoreranges` は同じ930フレームで失敗したが、restore diffで `0x02085B00`、`0x02088000`、`0x021B4B00` などの未復元ページを追加した後、`logs/codex-rollback-nsmbcoreranges-delta-discovered-more-heap-gamestate-2600-20260601` が2600-frame split local-input smokeを通過した。最終traceは checkpoint bytes `2,534,821`、save average 約`11.0ms`、restore average 約`11.1ms`。
- 同じ追加rangeで `nsmbranges` 単体も試したが、`logs/codex-rollback-nsmbranges-delta-discovered-more-heap-gamestate-2600-20260601` は1290フレームの入力状態で不一致になった。入力rangeを外す `MELONDS_NSML_ROLLBACK_NSMB_SKIP_INPUT_RANGES=1` でも `logs/codex-rollback-nsmbranges-delta-discovered-skip-input-gamestate-2600-20260601` は同じ1290付近で不一致になった。
- `MELONDS_NSML_ROLLBACK_CORE_SKIP_MASK` を追加し、`nsmbcoreranges` のMain RAM以外core stateからCart/GPU/SPU/Mic+SPI+RTC/Wifiを実験的に外せるようにした。`0x08`（Mic/SPI/RTC skip）は `logs/codex-rollback-nsmbcoreranges-core-skip-0x08-gamestate-2600-20260601` で2600-frame smokeを通過したが、サイズは約`2.54MB`のままで実用上の削減はほぼなかった。`0x02`（GPU skip）は1290フレーム、`0x04`（SPU skip）は1950フレーム、`0x0E`（GPU+SPU+Mic/SPI/RTC skip）は1620フレームで不一致になった。
- `nsmbtinycore` backendを追加した。NSMB range snapshotに、CPU/timer/scheduler/DMA/IRQ/IPC/WRAMなどの小さいcore stateだけを足す実験用backendで、通常savestate互換からさらに離れて案Dへ寄せるための切り分け。
- `nsmbtinycore + delta-discovered ranges` は checkpoint size 約`238KB`まで下がったが、1290フレームで `playerActor0Y` が不一致になった。GPU/SPU等の大きいdevice stateを完全に捨てるにはまだ足りない。
- `MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=1`（GPU timing/2D registerだけ追加）でも1290フレームで不一致。`=2`（full GPU追加）は1950フレームまで進んで `playerActor0X` 不一致になった。`=6`（full GPU+SPU追加）は `logs/codex-rollback-nsmbtinycore-fullgpu-spu-gamestate-2600-20260601` で2600-frame smokeを通過したが、checkpoint sizeは約`2.49MB`で `nsmbcoreranges` から約50KBしか減らない。
- GPU subset診断を追加し、`MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS` の高位bitで palette/OAM、VRAM、full GPU3D、light GPU3D を個別保存できるようにした。
- `0x0C4`（SPU + palette/OAM + VRAM）は `logs/codex-rollback-nsmbtinycore-gpuvram-spu-gamestate-2600-20260601` で1620フレーム `movingHazardX` 不一致。sizeは約`916KB`で軽いが、GPU3D状態なしでは不足。
- `0x104`（full GPU3D + SPU、VRAM/palette/OAMなし）は `logs/codex-rollback-nsmbtinycore-gpu3d-spu-novram-nopaloam-gamestate-2600-20260601` で2600-frame smoke通過。sizeは約`1.81MB`。
- `0x200`（light GPU3Dのみ、SPU/VRAM/palette/OAMなし）は `logs/codex-rollback-nsmbtinycore-gpu3dlight-nospu-fixed-gamestate-2600-20260601` で2600-frame smoke通過。checkpoint sizeは `247,355` bytes、save averageは約`7.9ms`。light GPU3DはFIFO、matrix、pipeline、register系を戻すが、VertexRAM/PolygonRAM/RenderPolygonRAMは戻さない。
- 人工送信遅延/jitter付きの `0x200` 追加検証 `logs/codex-rollback-nsmbtinycore-gpu3dlight-delayjitter-gamestate-2600-20260601` も2600-frame smoke通過。client側で `restoreOps=1`、`resims=1` を踏み、sizeは同じ `247,355` bytes、restore averageは約`8.2ms`。

Current blocker:

- 現在の最有力は `nsmbtinycore + delta-discovered ranges + light GPU3D`。これは完全な案D actor/global snapshotではないが、DS全体savestateではなく、NSMB range snapshotにCPU/timer/scheduler/DMA/IRQ/IPC/WRAMとGPU3Dの小さい進行状態だけを足す形なので、かなり案D寄り。
- 2600-frame synthetic routeでは `247,355` bytesまで下がった。まだ実行時diffで見つけたMain RAM rangeに依存しており、ROM静的解析でactor/global構造を完全確定した状態ではない。
- GPU3D lightで通る一方、GPU3Dなしの約`916KB`構成は1620フレームで壊れる。戻すべきなのは描画メモリ本体ではなく、GPU3D FIFO/matrix/pipeline/register系の進行状態らしい。
- SPUは今回の最小候補 `0x200` では不要だった。前のfull GPUのみ失敗との違いは再確認余地があるが、少なくとも現候補ではSPU保存は必須ではない。
- delta/restore diffで発見した範囲は実行時メモリ解析ベースであり、ROM静的解析でactor/global構造を確定した状態ではない。
- Real WAN jitter patterns and longer sessions are not measured yet.

Next actions:

- 次は `0x200` 候補をより長いframe数、別input route、rollback restore probeで検証する。人工遅延/jitterでは復元経路を1回踏んで通ったが、復元回数はまだ少ない。
- 並行して、delta-discovered rangeをROM/メモリ解析へ戻し、`戻すべきNSMB global/actor` と `毎フレーム再注入されるvolatile input/net packet` を分ける。
- `nsmbranges` 単体の案D完全形へ寄せるには、light GPU3Dで戻しているFIFO/matrix/pipeline/register相当のうち、ゲーム進行に本当に効く要素をさらに削る。

この文書は、Mario vs Luigi online PoCで検討したrollback方式の議論を、後で再開できるように分離して残す設計メモ。

## 背景

現在の本線は、`InputDelayFrames=4` 前後の低ディレイ入力同期方式。手動確認では4フレーム遅延なら実用に届く可能性がある。

一方で、高遅延・jitterが大きいWAN環境では、固定4フレーム遅延だけではremote inputが間に合わず、停止やカクつきが出る可能性がある。そのため、rollback方式も将来候補として検討した。

## これまでに試したこと

### melonDS full savestate rollback

既存のmelonDS savestateを使い、過去フレームのcheckpointへ戻して、保存済み入力履歴で現在フレームまで再実行する方式。

良い点:

- 正しさは高い。CPU、RAM、デバイス状態など、melonDSが通常savestateで保持する状態をまとめて戻せる。
- PoC実装は比較的早く作れる。
- `InputDelayFrames=0` でも、remote input未着時に予測入力で進める土台は動いた。

問題:

- 1 checkpointが約19MBあり、保存/復元/再実行が重い。
- rollback発生時に体感で止まる、またはカクつく。
- 同一PCでhost/clientを両方動かす検証では、実用感から遠い場面があった。
- 毎フレームcheckpointは現実的ではなく、checkpoint intervalを広げると再実行距離が伸びる。

現時点の評価:

- 正しさ確認用、または低頻度rollbackの保険としては使える。
- ゼロ遅延rollbackの主力として使うには重い。

### ARM9 Main RAM 4MB snapshot

ARM9 Main RAM最大4MBだけを`memcpy`で保存/復元する軽量backendを試した。公開フレームカウンタとして `NumFrames` / `NumLagFrames` / `LagFrameFlag` も小さいヘッダに入れて復元した。

良い点:

- checkpointが約4MB + 40byteになり、full savestateよりかなり軽い。
- 短距離の保存/復元/resimulate自体は動作した。

問題:

- CPUレジスタ、timer、DMA、scheduler、VRAM、Wi-Fi、IPCなどが戻らない。
- 人工送信遅延6フレーム + jitter4の検証で、rollback後にhost側のmoving hazardが止まり、client側だけ進む不一致が出た。
- RAMだけでは「過去のエミュレータ状態」ではなく、「過去の一部メモリを現在のCPU状態へ貼り直した状態」になってしまう。

現時点の評価:

- 軽いが正しさ不足。
- 実用候補ではない。
- ここから正しくするには、結局core側の状態をかなり追加保存する必要がある。

## 検討したrollback案

### Tango調査から得た示唆

`external/tango` にTango本体をcloneして、`tango-pvp` のrollback実装を確認した。

Tangoの重要な構造:

- ゲームごとのROM hook/trapを持ち、通信処理、入力読み取り、round開始/終了、RNG初期化などをゲーム別に差し替える。
- live primary emulatorとは別に、remote peerをローカルで再現する `shadow` emulator を持つ。
- 再実行専用のheadless `Fastforwarder` emulatorを持つ。描画を飛ばして高速に再実行する。
- `settled_state` は実remote inputで確定済みの単一checkpointとして保持する。
- `speculative tail` は `settled_state` から一時的にfastforwardして表示用stateを作る。ここで作った予測stateを次のseedへ混ぜない。
- ユーザー設定のframe delayを、両者共通の `input_delay = min(local, remote)` と、各ローカルだけの `presentation_delay = local - input_delay` に分ける。
- 入力はwire上ではraw inputを送る。local側ではdelay line、remote側ではqueue prefillで同じ共有input delayを実現する。
- 先行しすぎた側だけFPS targetを下げるthrottlerを持ち、双方が無制限にズレていくのを防ぐ。

Tangoで特に参考になる点:

1. rollbackを「毎回過去へ戻る処理」ではなく、`settled checkpoint` から表示用stateを毎フレーム作る仕組みにしている。
2. 予測stateを確定checkpointに混ぜない。確定checkpointは実inputだけで進める。
3. 共有input delayを使って、rollback深度そのものを先に削っている。
4. presentation delayはローカル表示だけの問題として扱い、ネットワーク上のtickとは分離している。
5. round lifecycleを明示的に管理し、roundをまたいだ古いinputを捨てる。
6. remote packet予測はゲームごとのpacket構造を理解した上で行っている。

NSMBへの適用可能性:

- `input_delay + presentation_delay` 分割は、そのまま採用する価値が高い。
- `settled checkpoint` と `speculative display state` を分ける設計も採用候補。
- 先行側だけを緩やかに減速するthrottlerは、host/clientのframe lead制御より自然にできる可能性がある。
- `shadow emulator` はDSだとコストが高い。NSMBの場合、今は「remote packetを再生成する」より「remote inputを同じゲームへ入れる」構造なので、Tangoのshadowをそのまま持ち込む必要は薄い。
- TangoのmGBA stateはGBAなので軽い。一方melonDS savestateは約19MBあり、同じ頻度で使うと重い。ここはそのまま真似できない。

NSMB向けに取り込むなら、次の順が現実的:

```text
1. 現在の低ディレイ方式を、Tango風に input_delay / presentation_delay に整理する。
2. 現在の InputMaxFrameLead を、Tango風の frame advantage + throttler に置き換えるか比較する。
3. rollbackを使う場合も、確定checkpointは実remote inputだけで進める。
4. 予測stateを次のcheckpointへ混ぜないルールを徹底する。
5. full savestate rollbackは短距離・低頻度に限定する。
```

現時点の判断:

- Tangoは「ゼロ遅延rollbackを力技で回している」のではなく、input delay、presentation delay、settled checkpoint、speculative tail、throttlingを組み合わせてrollback深度を管理している。
- これは今のNSMB方針と相性がよい。
- ただし、TangoはGBAでsavestateが軽く、ゲーム別通信packetもかなり解析済み。DS/NSMBへそのまま移植はできない。
- 参考にすべきなのはコードの部品より、`settled checkpointを汚さない`、`rollback深度をinput delayで削る`、`先行側をthrottleする` という設計。

### 案A: ゼロ遅延full rollback

`InputDelayFrames=0`で常に即時反映し、remote inputが後から違っていたらrollbackする。

評価:

- 操作感は理想に近い。
- ただしrollback頻度が高くなりやすい。
- DSエミュ全体のsavestateが重いため、現状ではカクつきが大きい。
- 快適化するには、かなり深いcheckpoint最適化が必要。

結論:

- 最終的にできれば強いが、今の実装難度とリスクは高い。

### 案B: 小入力遅延 + 小rollback

`InputDelayFrames=3〜4`を残し、通常はremote inputが間に合うようにする。packetが少し遅れた時だけ、最大4〜6フレーム程度を予測入力で進め、後着入力が違っていた場合だけ短距離rollbackする。

想定動作:

```text
通常:
  3〜4フレーム遅延で入力を適用する
  ほとんどのpacketは間に合うのでrollbackしない

packetが少し遅れた時:
  1〜4フレームだけ予測入力で進める
  後から本物の入力が来たら短距離rollbackする

大きく遅れた時:
  rollbackし続けず、一時停止して待つ
```

評価:

- 実装難度と実現性のバランスが最も良い。
- rollback頻度とrollback距離を小さくできる。
- full savestate backendでも、発生頻度を抑えれば体感カクつきを許容範囲にできる可能性がある。
- 国内WANの安定回線では、4フレーム遅延で大半の入力が間に合う見込みがある。

結論:

- rollbackを主役にせず、低ディレイ方式の保険にする。
- 将来rollbackを再開するなら、この案が第一候補。

候補設定:

```text
InputDelayFrames: 3〜4
InputMaxFrameLead: 4〜6
MaxRollbackFrames: 4〜6
RollbackBackend: savestate
RollbackCheckpointInterval: 1〜2
InputUnreliable: enabled
InputBundleHistory: 8〜12
Rollback over limit: stall
```

### 案C: core側の軽量checkpoint API

melonDS core側に、rollback専用の軽量checkpoint APIを作る。通常savestateと同じ正しさを目指しつつ、ファイル互換性、圧縮、不要メタデータなどを削り、必要な内部状態だけを高速保存/復元する。

必要になりうる状態:

- ARM9/ARM7 CPU state
- timers
- DMA
- IRQ
- scheduler/event queue
- Main RAM / WRAM / VRAM / OAM / palette
- IPC/FIFO
- Wi-Fi
- SPUのゲーム進行に影響する部分
- JIT cache invalidation policy

評価:

- 正しくできれば最もきれい。
- ただしmelonDS coreへの深い改造になる。
- 何か1つ漏れるとrollback後に不一致が出る。
- 実装・検証コストは高い。

結論:

- 中長期候補。
- まず小入力遅延 + 小rollbackでfull savestateを使い、どうしても重い場合に検討する。

### 案D: NSMBゲーム状態snapshot

DS全体ではなく、NSMB MvsLのゲーム側状態だけをsnapshotする。たとえばplayer actor、敵、Big Star、coin/item、RNG、MvsL global stateなどを保存/復元する。

必要になりうる状態:

- Mario/Luigi actor状態、座標、速度、アニメーション、死亡/復帰状態
- 敵、ブロック、土管、スター、コイン、アイテム、エフェクト
- object manager / actor list / spawn/despawn状態
- collision/physics内部状態
- MvsL score、残機、勝敗、timer、stage state
- RNG state
- input/communication tick
- camera、HUD、sound/event queueの一部

良い点:

- 成功すれば非常に軽い。
- NSMB MvsL専用に割り切れる。
- DS core全体のrollbackよりゲーム目的に近い。

問題:

- 解析難度が高い。
- 漏れた状態が1つあるだけで数秒後にズレる。
- ROM/メモリ構造への依存が強くなる。
- actor listやspawn/despawn管理を完全に理解する必要がある。

現実的なPoC順:

```text
1. player actor 2体
2. Big Star actor
3. moving hazard / enemy actor数体
4. RNG state
5. MvsL global state
```

結論:

- DS core軽量checkpointとは別方向の中長期候補。
- 「NSMB MvsL専用ゲーム状態rollback」を作る覚悟が必要。
- 今すぐ本線にするより、低ディレイ方式が限界に達した後の研究対象。

## 現時点の推奨方針

最終目標が「快適なWAN越し対戦」なら、現時点の最有力は次のハイブリッド方針。

```text
国内・安定回線:
  3〜4F delay + packet bundle + ほぼrollbackなし

不安定な瞬間:
  最大4〜6Fだけrollbackで吸収

それ以上の遅延:
  rollbackし続けず、一時停止して同期維持
```

理由:

- 4フレーム入力遅延は手動確認で実用に届く可能性がある。
- rollbackを常用しないため、full savestateの重さを避けやすい。
- packet bundleと組み合わせると、短いpacket lossやjitterは吸収できる可能性が高い。
- 実装が現実的で、現在のPoCから段階的に進められる。

避けたい方針:

- `InputDelayFrames=0` を前提にした常時rollback。
- ARM9 RAMだけを戻す不完全rollback。
- いきなりcore全体の軽量checkpointを作る。
- いきなりNSMBゲーム状態snapshotを完全実装する。

## 後で再開する場合の次アクション

1. 低ディレイ本線で `InputDelayFrames=3/4/5` の実用性を実2PCまたはLAN分散で測る。
2. `MaxRollbackFrames` を明示的に導入し、rollback距離を4〜6フレームに制限する。
3. rollback距離超過時はstallへ落とす。
4. full savestate backendのまま、小rollbackだけで体感カクつきが許容範囲か確認する。
5. それでも重い場合だけ、core軽量checkpointまたはNSMBゲーム状態snapshotのPoCへ進む。
