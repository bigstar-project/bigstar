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
| `Input::consoleKeys` | `0x02086C90` | candidate | US `0x02087650 - 0x9C0`; game-state trace shows held keys during scripted input |
| `Input::playerKeysHeld` | `0x02086CA0` | candidate | US `0x02087660 - 0x9C0`; host/client both show `0x10` for scripted RIGHT input |
| `Input::playerKeysPressed` | `0x02086CA4` | candidate | US `0x02087664 - 0x9C0`; added to game-state trace for local/remote input diagnosis |
| `Net::ggid` | `0x02087E78` | verified | JP game group id `0x42` |
| `Net::randomBranchAddress` | `0x02087E7C` | candidate | US `0x0208885C - 0x9E0`; pointer-like value |
| `Net::sendPacket` | `0x02087F00` | verified | packet-like bytes differ per console and match active transfer state |
| `Net::randomCallCount` | `0x02088068` | candidate | US `0x02088A48 - 0x9E0`; value `0x92` at frame 5000 |
| `Net::marker` | `0x0208806C` | candidate | US `0x02088A4C - 0x9E0` |
| `Net::randomShareStep` | `0x02088070` | candidate | US `0x02088A50 - 0x9E0` |
| `Net::random.value` | `0x02088088` | candidate | US `0x02088A68 - 0x9E0`; same value on inst0/inst1 at frame 5000 |
| `Net::packetFreeBytesRecvBitmap` | `0x020880A4` | candidate | US `0x02088A84 - 0x9E0`; packet API trace target |
| `Net::packetFreeBytes` | `0x020880B4` | candidate | US `0x02088A94 - 0x9E0`; packet API trace target |
| `Net::packetSequenceBuilder` | `0x020880D4` | candidate | US `0x02088AB4 - 0x9E0`; packet API trace target |
| `Net::packetSequencers` | `0x020880FC` | candidate | US `0x02088ADC - 0x9E0`; packet API trace target |

## A2DJ Priority Functions

| Function | A2DJ address | Status | Evidence |
| --- | ---: | --- | --- |
| `Net::getRandom12()` | `0x0200E550` | verified | US `0x0200E6A4 - 0x154`; reads `Net::randomCallCount`, `Net::randomBranchAddress`, `Net::random.value` |
| `Net::getRandom()` | `0x0200E5A0` | verified | US `0x0200E6F4 - 0x154`; reads `Net::randomCallCount`, `Net::randomBranchAddress`, `Net::random.value` |
| `Net::syncRandomFull()` | `0x0200E5E8` | verified | veneer to `Net::Core::shareRandomSeed()` |
| `Net::syncRandomFast()` | `0x0200E5F4` | verified | references `Net::randomShareStep`, `Net::random.value`, `Net::randomCallCount` |
| `Net::getConsoleTouchPad(u16)` | `0x0200E67C` | candidate | US `0x0200E7D0 - 0x154`; packet input helper |
| `Net::getConsoleKeys(u16)` | `0x0200E700` | candidate | US `0x0200E854 - 0x154`; reads packet header offset `+2` |
| `Net::getPacketByte(u16,u32)` | `0x0200E978` | candidate | US `0x0200EACC - 0x154`; byte read from wireless packet buffer |
| `Net::setPacketByte(u32,u8)` | `0x0200E9AC` | candidate | US `0x0200EB00 - 0x154`; byte write into wireless packet buffer |
| `Net::getPacketTick(u16)` | `0x0200E9BC` | candidate | US `0x0200EB10 - 0x154`; packet helper |
| `Net::getPacketAction(u16)` | `0x0200E9DC` | candidate | US `0x0200EB30 - 0x154`; packet helper |
| `Net::getPacket(u16)` | `0x0200E9FC` | candidate | US `0x0200EB50 - 0x154`; packet helper |
| `Net::Core::readUserInfo(MBUserInfo*)` | `0x0200F320` | candidate | Local MP payload/write-watch candidate; US `0x0200F474` |
| `Net::Core::transferPacket(Net::PacketAction)` | `0x0200F98C` | candidate | Local MP payload/write-watch candidate; US `0x0200FAE0` |
| `Net::update()` | `0x0200FF40` | candidate | Local MP payload/write-watch candidate; US `0x02010094` |
| `Net::updatePacket()` | `0x020101E4` | candidate | calls A2DJ `Net::Core::readUserInfo`; US `0x0201031C` |
| `Net::onPacketPollingDefault()` | `0x02010810` | verified | stored in `Net::onPacketPolling` during `setDefaultHandlers()` |
| `Net::onRenderSignalStrengthDefault()` | `0x02010828` | verified | stored in `Net::onRenderSignalStrength` during `setDefaultHandlers()` |
| `Net::setDefaultHandlers()` | `0x02010930` | verified | loads `0x02010810`, `0x020173B0`, `0x02010828` and branches through handler setter |
| `Net::Core::createPacketSequencer(u8**,u8,callback,void*)` | `0x02010D0C` | candidate | US `0x02010E60 - 0x154`; packet sequencer setup target |
| `Net::Core::readPacketInt(u16,u32)` | `0x02010DAC` | candidate | US `0x02010F00 - 0x154`; packet API trace target |
| `Net::Core::readPacketByte(u16,u32)` | `0x02010E14` | candidate | US `0x02010F68 - 0x154`; packet API trace target |
| `Net::Core::writePacketInt(u32,u32)` | `0x02010E4C` | candidate | US `0x02010FA0 - 0x154`; packet API trace target |
| `Net::Core::writePacketByte(u32,u8)` | `0x02010E80` | candidate | US `0x02010FD4 - 0x154`; packet API trace target |
| `Net::Core::freePacketBytes(u32,u32)` | `0x02010E90` | candidate | US `0x02010FE4 - 0x154`; packet free-byte management target |
| `Net::Core::allocPacketBytes(u32)` | `0x02010EBC` | candidate | US `0x02011010 - 0x154`; packet free-byte management target |
| `Net::Core::shareRandomSeed()` | `0x02010F04` | verified | target of `Net::syncRandomFull()` veneer |
| `Net::Core::processRecvPacket()` | `0x02011360` | candidate | US `0x020114B4 - 0x154`; receives `Net::sendPacket`-shaped buffer |
| `Net::Core::processSendPacket()` | `0x02011428` | candidate | US `0x0201157C - 0x154`; sends `Net::sendPacket`-shaped buffer |
| `Net::Core::clearPacket()` | `0x02011504` | candidate | US `0x02011658 - 0x154`; packet API trace target |
| `Net::Core::initPacket()` | `0x020115A8` | candidate | US `0x020116FC - 0x154`; packet API trace target |
| `Net::PacketSequenceBuilder::nextByte()` | `0x0201166C` | candidate | US `0x020117C0 - 0x154`; high-frequency packet sequencer helper |
| `Net::PacketSequenceBuilder::pushPacket(u8,u8,const u8*)` | `0x02011748` | candidate | US `0x0201189C - 0x154`; packet sequence builder entry |

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

Star-related RNG verification:

- `tests/nsmb_mario_vs_luigi_star_probe.inputs` reaches the match and moves inst0/Mario near the first visible star.
- Earlier notes treated frame `005071` as star pickup / next-star spawn, but later Actor/state trace did not confirm a score/star-count pickup there. Treat this as a star-area RNG/Actor transition until the actual pickup state is isolated.
- RAM timeline command:
  - `python tools\nsmb_mvl_ram_probe.py --a2dj-rng-timeline --rng-timeline-only logs\ram-star-rng-window\inst0_frame*_mainram.bin`
- Observed transition:
  - frame `002850`: `Net::randomCallCount=0x91`, `Net::random.value=0x97C1D7D6`, `Net::randomBranchAddress=0x020B4460`
  - frame `002900`: `Net::randomCallCount=0x92`, `Net::random.value=0x413B3BAA`, `Net::randomBranchAddress=0x0212D41C`
  - frame `005071`: `Net::randomCallCount=0x93`, `Net::random.value=0xF9D72FCA`, `Net::randomBranchAddress=0x0212D41C`
- Interpretation: a star-area transition consumes the shared `Net::random` stream once. `0x0212D41C` is likely the return address for the random caller, so the associated BL site is around `0x0212D418`.
- Disassembly around `0x0212D418` confirms this is star-slot selection:
  - `0x0212D418: bl 0x0200E5A0`
  - the result is masked/multiplied by slot count from `[r8+0x47A]`
  - occupied slots are checked with bitmask `[r8+0x460]`
  - selected slot coordinates are loaded from tables near `0x020C8878` / `0x020C88CC`
  - actor creation is called with `r0=0x22`
- A second run using the same input route reproduced frame `005071` with `Net::randomCallCount=0x93`, `Net::random.value=0xF9D72FCA`, `Net::randomBranchAddress=0x0212D41C`, and the same minimap/star-area position at frame `006400`.

Seed/value patch experiment:

- A melonDS memory patch at frame `5000` that changes only `Net::random.value` (`0x02088088-0x0208808B`) from `0x413B3BAA` to `0x12345678` changes the next RNG output.
- Dedicated test hook added:
  - `MELONDS_NSML_NET_RANDOM_FRAME=5000`
  - `MELONDS_NSML_NET_RANDOM_VALUE=0x12345678`
  - This writes A2DJ `Net::random.value` for each test instance without a MainRAM patch file.
- Value-only patch command shape:
  - `MELONDS_NSML_MEM_PATCH_FRAME=5000`
  - `MELONDS_NSML_MEM_PATCH_RANGES=0x088088-0x08808B`
  - source file is a MainRAM-sized binary with only that 4-byte value changed.
- Observed patched transition:
  - frame `005000`: `count=0x92`, `value=0x413B3BAA`
  - frame `005071`: `count=0x93`, `value=0x7544F5D5`
- Interpretation: the Big Star respawn path uses the shared `Net::random.value`; changing the stream state before the respawn changes the generated RNG result without directly patching the star actor.

Initial star seed injection:

- `MELONDS_NSML_NET_RANDOM_FRAME=2800` is early enough to affect the initial MvsL star placement.
- `MELONDS_NSML_NET_RANDOM_FRAME=2900` is too late for the initial star; it leaves the initial placement unchanged and only affects later RNG state.
- `MELONDS_NSML_NET_RANDOM_AUTO=1` now injects the seed automatically when the JP MvsL state is detected:
  - `Game::stageGroup == 9`
  - `Game::vsMode == 1`
  - `Net::ggid == 0x42`
  - `Net::randomCallCount == 0`
- With auto injection and `MELONDS_NSML_NET_RANDOM_VALUE=0x12345678`, the hook fired at frame `2676` for inst0 and frame `2659` for inst1, before the initial star RNG consumption.
- Auto injection reproduced the fixed-frame `2800` result:
  - frame `002800`: `count=0x00`, `value=0x12345678`, `branch=0x020CBF24`
  - frame `002850`: `count=0x91`, `value=0x2D4F3DCB`, `branch=0x020B4460`
  - frame `002900`: `count=0x92`, `value=0x24EB777B`, `branch=0x0212D41C`
- Match seed network distribution:
  - `MELONDS_NSML_POC=1` host now sends a reliable seed packet to the client.
  - Host uses `MELONDS_NSML_MATCH_SEED` when provided, otherwise generates a session seed.
  - Client receives the seed and enables `Net::random.value` auto injection with that value.
  - Localhost host/client validation with `MELONDS_NSML_MATCH_SEED=0x12345678` confirmed both processes reached the same RNG timeline:
    - frame `002800`: `count=0x00`, `value=0x12345678`, `branch=0x020CBF24`
    - frame `002900`: `count=0x92`, `value=0x24EB777B`, `branch=0x0212D41C`
- With `MELONDS_NSML_NET_RANDOM_VALUE=0x12345678` at frame `2800`, two runs matched:
  - frame `002850`: `count=0x91`, `value=0x2D4F3DCB`, `branch=0x020B4460`
  - frame `002900`: `count=0x92`, `value=0x24EB777B`, `branch=0x0212D41C`
  - frame `004100` screenshot shows the same initial star position.
- With `MELONDS_NSML_NET_RANDOM_VALUE=0x87654321` at frame `2800`, the initial star position changed, and the RNG timeline became:
  - frame `002850`: `count=0x91`, `value=0x3B5FB922`, `branch=0x020B4460`
  - frame `002900`: `count=0x92`, `value=0x7866CA1D`, `branch=0x0212D41C`
- Interpretation: injecting the shared match seed around frame `2800` controls the initial star placement as well as later respawn RNG.

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

1. 8コインアイテム取得用の入力スクリプトを追加し、同じ `Net::random` timeline で消費箇所を確認する。
2. seed配布済みのhost/client 2プロセスで、入力同期開始後のremote input timeoutを減らす。
3. seed配布済みの2プロセスでスター取得まで走らせ、再生成スターのRNG timelineもhost/clientで一致するか確認する。
4. `0x0212D418` のBig Star選択処理はROMパッチ候補として保持するが、直接固定するより、まずはmatch seed固定を優先する。

## Next Actions

1. `Stage::getRandom()` のJP関数アドレスまたはinline呼び出し箇所を確認する。
2. Big Star生成と8コイン時アイテム生成がどちらも `Net::random` 系を使うか、write watch / Ghidraで確認する。
3. 乱数固定は `Game::random` ではなく、まず `Net::random` 側を優先する。
4. ROMパッチ前に、melonDS側のメモリパッチで `Net::random.value` / `Net::randomCallCount` 固定実験を行う。
