# NSMB A2DJ Symbol Port

JP版 `A2DJ` 向けに、US版 `NSMB-Code-Reference` の優先シンボルを移植するための作業メモ。

## 現在の結論

まず必要な乱数同期まわりでは、`Game` / `Stage` グローバルと `Net` グローバルの一部をRAM dumpから移植できた。

Net系の優先関数は、JP RAM上の復号済みARM9コードで先頭命令と参照リテラルを確認できた。ROMパッチに入る前の残件は、`Stage::getRandom()` と Big Star / 8コインアイテム生成側の呼び出し元特定。

## A2DJ Priority Symbols

| Symbol | A2DJ address | Status | Evidence |
| --- | ---: | --- | --- |
| `Game::stageID` | `0x02085054` | verified | MvsL stage 0 in route RAM dump |
| `Game::stageGroup` / `Stage::stageGroup` | `0x02085058` | verified | MvsL group `9` in route RAM dump |
| `Game::randomCallCount` | `0x02085094` | candidate | US `0x02085A54 - 0x9C0`; currently `0` in MvsL dump |
| `Game::random.value` | `0x020850B0` | candidate | US `0x02085A70 - 0x9C0`; currently `1` in MvsL dump |
| `Game::localPlayerID` | `0x020850BC` | verified | inst0 is `0`, inst1 is `1` |
| `Game::vsMode` | `0x020850C4` | verified | MvsL route has `1` |
| `Net::ggid` | `0x02087E78` | verified | JP game group id `0x42` |
| `Net::randomBranchAddress` | `0x02087E7C` | candidate | US `0x0208885C - 0x9E0`; pointer-like value |
| `Net::sendPacket` | `0x02087F00` | verified | packet-like bytes differ per console and match active transfer state |
| `Net::randomCallCount` | `0x02088068` | candidate | US `0x02088A48 - 0x9E0`; value `0x92` at frame 5000 |
| `Net::marker` | `0x0208806C` | candidate | US `0x02088A4C - 0x9E0` |
| `Net::randomShareStep` | `0x02088070` | candidate | US `0x02088A50 - 0x9E0` |
| `Net::random.value` | `0x02088088` | candidate | US `0x02088A68 - 0x9E0`; same value on inst0/inst1 at frame 5000 |

## A2DJ Priority Functions

| Function | A2DJ address | Status | Evidence |
| --- | ---: | --- | --- |
| `Net::getRandom12()` | `0x0200E550` | verified | US `0x0200E6A4 - 0x154`; reads `Net::randomCallCount`, `Net::randomBranchAddress`, `Net::random.value` |
| `Net::getRandom()` | `0x0200E5A0` | verified | US `0x0200E6F4 - 0x154`; reads `Net::randomCallCount`, `Net::randomBranchAddress`, `Net::random.value` |
| `Net::syncRandomFull()` | `0x0200E5E8` | verified | veneer to `Net::Core::shareRandomSeed()` |
| `Net::syncRandomFast()` | `0x0200E5F4` | verified | references `Net::randomShareStep`, `Net::random.value`, `Net::randomCallCount` |
| `Net::onPacketPollingDefault()` | `0x02010810` | verified | stored in `Net::onPacketPolling` during `setDefaultHandlers()` |
| `Net::onRenderSignalStrengthDefault()` | `0x02010828` | verified | stored in `Net::onRenderSignalStrength` during `setDefaultHandlers()` |
| `Net::setDefaultHandlers()` | `0x02010930` | verified | loads `0x02010810`, `0x020173B0`, `0x02010828` and branches through handler setter |
| `Net::Core::shareRandomSeed()` | `0x02010F04` | verified | target of `Net::syncRandomFull()` veneer |

## Shift Notes

- `Game` / `Stage` グローバル群は、US版アドレスから `-0x9C0` でA2DJの現在値と合う。
- `Net` グローバル群は、US版アドレスから `-0x9E0` でA2DJの現在値と合う。
- Net系の関数群は、US版アドレスから `-0x154` でA2DJの復号済みARM9コードと合う。
- 他モジュールやoverlay関数にも同じshiftを適用できる保証はない。個別に確定する。

## Verification Commands

```powershell
python tools\nsmb_mvl_ram_probe.py --rom roms\nsmb.nds --a2dj-symbols logs\ram-gamepatch-a\inst0_frame005000_mainram.bin logs\ram-gamepatch-a\inst1_frame005000_mainram.bin
python tools\nsmb_mvl_ram_probe.py --rom roms\nsmb.nds --a2dj-functions logs\ram-gamepatch-a\inst0_frame005000_mainram.bin
```

melonDS runtime trace hook:

- `MELONDS_NSML_RANDOM_TRACE=1` enables runtime tracing.
- `MELONDS_NSML_RANDOM_TRACE_ADDR` defaults to A2DJ `Net::getRandom()` at `0x0200E5A0`.
- `MELONDS_NSML_RANDOM_TRACE_ADDRS` accepts a comma/space separated address list for tracing known call sites.
- `MELONDS_NSML_RANDOM_TRACE_VALUE_ADDR` defaults to A2DJ `Net::random.value` at `0x02088088`.
- `MELONDS_NSML_RANDOM_TRACE_CALLCOUNT_ADDR` defaults to A2DJ `Net::randomCallCount` at `0x02088068`.
- `MELONDS_NSML_RANDOM_TRACE_START_FRAME` / `MELONDS_NSML_RANDOM_TRACE_END_FRAME` bound logging by frame.
- `MELONDS_NSML_RANDOM_TRACE_LOG` writes CSV rows: `nds,frame,pc,caller,lr,random_value,random_call_count`.
- `MELONDS_NSML_RANDOM_TRACE` no longer disables JIT automatically. RAM timeline analysis is currently more reliable than PC tracing for exact RNG consumption.

Additional watch experiments:

- `MELONDS_NSML_WATCH_ADDR=0x020850B0` for `Game::random.value` produced no writes during frame 4500-5100.
- `MELONDS_NSML_WATCH_ADDR=0x02088088` for `Net::random.value` produced no writes during frame 0-5100.
- `MELONDS_NSML_WATCH_ADDR=0x02087E7C` for `Net::randomBranchAddress` produced no writes during frame 0-5100.

Interpretation:

- These negative watch results do not invalidate the symbol candidates.
- The current route may finish seed sharing / initial randomization before the watched gameplay window, or the tested path may not call `Net::getRandom()` after the match state is reached.
- To verify random-event behavior, the next input script should deliberately trigger an 8-coin item or star pickup/respawn while these addresses are watched.

Call-site scan:

- `python tools\nsmb_mvl_ram_probe.py --find-arm-bl-to 0x0200E5A0 logs\ram-gamepatch-a\inst0_frame005000_mainram.bin`
- Result: 61 ARM `BL` call sites to `Net::getRandom()`.
- Notable call sites include:
  - `0x0209D29C`
  - `0x0209D2D4`
  - `0x020AC154`
  - `0x020ADEB4`
  - `0x020B4248` through `0x020B445C`
  - `0x0212D418`
  - `0x0212D44C`
  - `0x02141694`
  - `0x021547C0`
- This confirms that `Net::getRandom()` is the shared random primitive used by many stage/actor paths. The next step is to map the call sites around Big Star and Item actors specifically.

Star pickup RNG verification:

- `tests/nsmb_mario_vs_luigi_star_probe.inputs` reaches the match, moves inst0/Mario, collects the first visible star, and causes the next star to spawn.
- RAM timeline command:
  - `python tools\nsmb_mvl_ram_probe.py --a2dj-rng-timeline --rng-timeline-only logs\ram-star-rng-window\inst0_frame*_mainram.bin`
- Observed transition:
  - frame `002850`: `Net::randomCallCount=0x91`, `Net::random.value=0x97C1D7D6`, `Net::randomBranchAddress=0x020B4460`
  - frame `002900`: `Net::randomCallCount=0x92`, `Net::random.value=0x413B3BAA`, `Net::randomBranchAddress=0x0212D41C`
  - frame `005071`: `Net::randomCallCount=0x93`, `Net::random.value=0xF9D72FCA`, `Net::randomBranchAddress=0x0212D41C`
- Interpretation: first-star collection / next-star spawn consumes the shared `Net::random` stream once. `0x0212D41C` is likely the return address for the random caller, so the associated BL site is around `0x0212D418`.

## Randomness Scope and Fix Strategy

Randomness should be split into two layers.

Pre-match selection randomness:

- Example: random MvsL stage selection.
- Fix by forcing the resolved selection, not by relying on later gameplay RNG state.
- For MvsL this means fixing `Game::stageGroup=9` and `Game::stageID=0..4`, or hooking the menu / `loadLevel(...)` path so both peers load the same explicit stage.

In-match gameplay randomness:

- Known examples: Big Star placement and 8-coin item generation.
- Likely additional examples: tile/object randomization, actor effects that call `Stage::getRandom()` / `Net::getRandom()`, enemy/item shatter/drop behavior, and any MvsL-only actor using the shared Net RNG.
- Fix by controlling `Net::random.value`, `Net::randomCallCount`, and the seed sharing path around `Net::syncRandomFull()` / `Net::syncRandomFast()` / `Net::Core::shareRandomSeed()`.

Final expected implementation:

- Choose one authoritative match seed at netplay session start.
- Force both local two-instance melonDS processes to use the same seed before or during MvsL load.
- Force match setup inputs/results that are outside gameplay RNG, especially stage selection.
- During gameplay, do not force every star/item address individually if avoidable. Prefer patching/hooking `Net::getRandom()` or seed sharing so all random events consume the same sequence.
- Keep a debug mode that logs `Net::getRandom()` call count and caller address. If call count diverges, stop and report desync before visual state diverges.
- Only patch individual actor fields, such as Big Star slot or item type, as a fallback or diagnostic tool.

Why classify the 61 `Net::getRandom()` call sites:

- It is not for controlling all 61 sites individually.
- The main control point should be `Net::getRandom()` or the seed sharing functions.
- Classification is for desync diagnosis. If call count diverges, the caller address tells which game system consumed an extra/missing random value.
- It also separates acceptable random users from setup users. For example, random stage selection should be fixed as a match setting, while gameplay random users should consume the shared sequence.
- If one caller is inherently local-console dependent, such as reading only `Game::localPlayerID`, that caller may need a small game patch even if the RNG seed is shared.

Expected RNG synchronization contract:

- Both peers start from the same `Net::random.value`.
- Both peers start from the same `Net::randomCallCount`.
- Both peers must call `Net::getRandom()` in the same order with the same caller sequence.
- If the caller sequence differs, forcing only the seed is insufficient because the random stream is consumed at different positions.
- Input lockstep is what should normally keep the caller sequence equal; caller logging is how we prove it.

## Current Next Actions

1. `0x0212D418` 周辺を逆アセンブルして、Big Star再配置の呼び出し元か確認する。
2. 同じ入力スクリプトを2回実行し、frame `005071` の `Net::randomCallCount` / `Net::random.value` / 次スター位置が完全一致するか確認する。
3. 8コインアイテム取得用の入力スクリプトを追加し、同じ `Net::random` timeline で消費箇所を確認する。
4. ROMパッチ前に、melonDS側メモリパッチで `Net::random.value` / `Net::randomCallCount` を固定して、スター再配置が再現できるか検証する。

## Next Actions

1. `Stage::getRandom()` のJP関数アドレスまたはinline呼び出し箇所を確認する。
2. Big Star生成と8コイン時アイテム生成がどちらも `Net::random` 系を使うか、write watch / Ghidraで確認する。
3. 乱数固定は `Game::random` ではなく、まず `Net::random` 側を優先する。
4. ROMパッチ前に、melonDS側のメモリパッチで `Net::random.value` / `Net::randomCallCount` 固定実験を行う。
