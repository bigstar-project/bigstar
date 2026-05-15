# NSMB A2DJ Symbol Port

JP版 `A2DJ` 向けに、US版 `NSMB-Code-Reference` の優先シンボルを移植するための作業メモ。

## 現在の結論

まず必要な乱数同期まわりでは、`Game` / `Stage` グローバルと `Net` グローバルの一部をRAM dumpから移植できた。

ただし、関数アドレスはまだ未確定。ROMパッチに入る前に、`Net::getRandom()`、`Net::syncRandomFull()`、`Net::syncRandomFast()`、`Net::Core::shareRandomSeed()`、`Stage::getRandom()` のJP関数アドレスをGhidraまたはシグネチャで確定する必要がある。

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

## Shift Notes

- `Game` / `Stage` グローバル群は、US版アドレスから `-0x9C0` でA2DJの現在値と合う。
- `Net` グローバル群は、US版アドレスから `-0x9E0` でA2DJの現在値と合う。
- 関数アドレスにも同じshiftを適用できる保証はない。ROMパッチ対象にする前に別途確定する。

## Verification Commands

```powershell
python tools\nsmb_mvl_ram_probe.py --rom roms\nsmb.nds --a2dj-symbols logs\ram-gamepatch-a\inst0_frame005000_mainram.bin logs\ram-gamepatch-a\inst1_frame005000_mainram.bin
```

Additional watch experiments:

- `MELONDS_NSML_WATCH_ADDR=0x020850B0` for `Game::random.value` produced no writes during frame 4500-5100.
- `MELONDS_NSML_WATCH_ADDR=0x02088088` for `Net::random.value` produced no writes during frame 0-5100.
- `MELONDS_NSML_WATCH_ADDR=0x02087E7C` for `Net::randomBranchAddress` produced no writes during frame 0-5100.

Interpretation:

- These negative watch results do not invalidate the symbol candidates.
- The current route may finish seed sharing / initial randomization before the watched gameplay window, or the tested path may not call `Net::getRandom()` after the match state is reached.
- To verify random-event behavior, the next input script should deliberately trigger an 8-coin item or star pickup/respawn while these addresses are watched.

## Next Actions

1. `Net::getRandom()` のJP関数アドレスを確定する。
2. `Stage::getRandom()` の呼び出し先がJP版でも `Net::getRandom()` であることを確認する。
3. Big Star生成と8コイン時アイテム生成がどちらも `Net::random` 系を使うか、write watch / Ghidraで確認する。
4. 乱数固定は `Game::random` ではなく、まず `Net::random` 側を優先する。
