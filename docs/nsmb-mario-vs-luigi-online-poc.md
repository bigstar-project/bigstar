# NSMB Mario vs Luigi Online PoC

## Current pipe-course camera Y investigation - 2026-06-12

- User-reported issue: pipe course only, after wall-kicking upward and making the camera move up, descending does not fully bring the camera back down.
- Follow-up: the issue appears to reproduce only after rematch, i.e. on game 2 or later, not on the first MvL game.
- User capture `logs/pipe-camera-repro-2g` reproduced the bug on game 2. The captured `recorded-inputs/host.inputs` was converted into a role-aware replay with common bootstrap input and host-only post-start movement for automated CSV verification.
- Root cause found: `MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD` cleared `0x020CA880` only in game 1 because `ClearMvlCameraInitHoldApplied[instance]` stayed true across the auto-restart checkpoint restore. On game 2, `cameraDbgCA880` stayed `0x08`, and `stageCameraPositionY` stayed high after Mario landed.
- Fix: auto-restart startup hook reset now clears `ClearMvlCameraInitHoldApplied[instance]`, and auto-restart frame rebasing also rebases the camera-init-hold clear start/end frames.
  - Before fix replay: in `logs/codex-pipe-camera-repro2g-before-fix-trace2`, frame 4900-5400 stayed `cameraDbgCA880=0x08` and `stageCameraPositionY=0x000C8000` after Mario returned to `playerActor0Y=0xffe80000`.
  - After fix replay: in `logs/codex-pipe-camera-repro2g-after-fix-trace-pass`, the host clears `0x020CA880` again at frame 3573 on game 2 (`old=0x08 value=0x00`), frame 4900-5400 stays `cameraDbgCA880=0x00`, and `stageCameraPositionY` settles lower at `0x00088000` for the same landing state.
- Current finding: the visible Y camera reported by the symptom is most likely the StageCamera object fields (`stageCameraTargetY` / `stageCameraPositionY`), not the global `Stage::cameraY[player]` slots.
  - In `logs/codex-pipe-camera-clearhold-extended-20260612`, `Stage::cameraY[0/1]` stayed fixed at `0x000E0000` and height stayed `0x000C0000`.
  - In the same run, `stageCameraPositionY` changed independently. Host/local player 0 reached `0x000F8000` and stayed there through frame 3600, while client/local player 1 returned to `0x00000000`.
  - That run used stage 3 (`mvlSceneSettings=0x00B7FF00`), `ClearMvlCameraInitHold`, input netplay, JIT, no draw/audio, and extended game-state trace.
- `MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD` still behaves as intended under normal GUI/manual-peer conditions:
  - `logs/codex-pipe-camera-clearhold-extended-20260612` shows `0x020CA880` cleared at frame 859 (`old=0x08 value=0x00`).
  - After that, `cameraDbgCA880` only showed `0x10` then `0x00`; the old init-hold bit `0x08` did not reappear in this trace.
- Reproduction capture support added: melonDS can now write the effective per-frame input stream to a replay-compatible `.inputs` file when `MELONDS_NSML_INPUT_RECORD_FILE` is set.
  - Use `scripts/run-nsmb-mvl-manual-local.ps1 -RecordInput` for local repro capture. It starts both host/client windows and writes `recorded-inputs\host.inputs` / `recorded-inputs\client.inputs` under the log directory by default.
  - `manual-local` now enables JIT by default for playable capture speed. Use `-NoJit` only for a deterministic comparison run where speed is not important.
  - `manual-local` no longer enables the input-netplay start barrier by default. The previous default could deadlock both peers after start-ready exchange while both waited for remote input frame 840.
  - `scripts/run-nsmb-mvl-manual-peer.ps1` also exposes `-RecordInput`, but it is only for an actual paired peer run. Do not use it as the primary one-PC repro recorder because peer/start synchronization can look like a freeze when only one side is running.
  - The recorder runs after `ApplyInputScript`, so the output includes the startup/bootstrap input plus the user's manual controls. The resulting file can be passed back to `-InputScript` for replay.
  - Recording now canonicalizes to the DS button mask's low 12 bits and batches file flushes, avoiding per-frame disk flushes from unrelated key-mask bit changes during manual input.
- Reproduction note: the earlier automated inputs did not reproduce the exact wall-kick-up-then-descend path. The user capture in `logs/pipe-camera-repro-2g` is the current repro source for this bug.
- Failed/invalid check: `logs/codex-pipe-camera-hostonly-extended-20260612` timed out because the host-only input path made the host wait for a remote start-ready peer after the client exited; the leftover melonDS process was stopped. Treat that log as invalid for camera behavior.
- Current blocker: none for the identified rematch camera-init-hold bug. Remaining manual check is to play the pipe course rematch normally and confirm the visible camera now feels correct after wall-kick descent.
- Next action: if manual play still shows a residual offset, capture another `-RecordInput` run after this fix and compare `stageCameraPositionY` against the now-cleared `cameraDbgCA880=0x00` path to separate normal player-height camera offset from a new camera latch.
- Verification: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed after the rematch camera fix; `.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 ... -InputScript logs\pipe-camera-repro-2g\recorded-inputs\combined-replay-bootstrap-host.inputs ... -GameStateTraceExtended -ClientPacketBridgeForceGameLocalPlayerID 0` passed for `logs/codex-pipe-camera-repro2g-after-fix-trace-pass`; earlier verification includes `.\scripts\run-nsmb-mvl-split-local-input-smoke.ps1 ... -MvlStage 3 ... -GameStateTraceInterval 10` for `logs/codex-pipe-camera-y-trace-20260612`, `.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 ... -MvlStage 3 -ClearMvlCameraInitHold -GameStateTraceExtended` for `logs/codex-pipe-camera-clearhold-extended-20260612`, input recording build/replay smokes, JIT-default manual-local smoke, and `logs/codex-manual-local-start-fixed`.

## Current desync diagnostics - 2026-06-10

- User-reported issue: rollback disabled matches can sometimes diverge between host and client.
- GUI-launched melonDS now enables lightweight state hash exchange by default with `MELONDS_NSML_STATE_SYNC=1`, `MELONDS_NSML_STATE_SYNC_INTERVAL=60`, and `MELONDS_NSML_STATE_SYNC_EXTENDED=1`.
  - This does not apply state. It only exchanges periodic state hashes and emits a structured `game_state_mismatch` entry to `melonds-diagnostics.json` when host/client disagree. The existing `NSMB PoC: game state mismatch ...` stdout line remains as a human log, but the GUI no longer parses stdout for this status.
  - The mismatch line reports whether `basic`, `playerGlobal`, `wifiCandidate`, and `renderCandidate` matched, which should narrow the first diverging category without full RAM dumps.
- GUI launch now passes `MELONDS_NSML_DIAGNOSTICS_FILE=<logdir>\melonds-diagnostics.json`. The Tauri backend reads that JSON and surfaces the latest mismatch in the Battle view's connection and log panels.
- `scripts/run-nsmb-mvl-manual-peer.ps1` now has `-DesyncLog`, `-DesyncLogInterval`, and `-DesyncLogExtended`.
  - `-DesyncLog` enables the same state hash exchange and writes per-role `host.game-state.csv` / `client.game-state.csv` at the chosen interval.
  - Start with `-DesyncLogInterval 60`; use `30` or `15` only when the first diverging frame needs tighter localization.
  - `-DesyncLogExtended` adds broader CSV fields/hashes and should be reserved for short repro windows.
- Expected overhead: the default GUI path is low frequency and should be much lighter than full RAM dumps or every-frame CSV tracing. Detailed CSV tracing can cause stutter if interval is too small because it scans game objects and flushes rows to disk.
- Current blocker: the actual first-divergence category/frame still needs a reproduced GUI/manual run with the new diagnostics.
- Next action: on the next GUI reproduction, inspect each peer's GUI mismatch warning and `melonds-diagnostics.json`; if needed, rerun with manual `-DesyncLog` around that reported frame for full CSV context.
- Verification: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel`, `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo clippy-all` in `tools\nsmb-mvl-gui\src-tauri`, `pnpm typecheck`, targeted `pnpm biome check` for changed GUI files, `pnpm test:unit`, and `pnpm test:browser` pass. `pnpm run ci` in `tools\nsmb-mvl-gui` is still blocked after `tsc` by existing Biome formatting failures caused by CRLF line endings across TS/JSON files not touched in this change.

## Current input health diagnostics - 2026-06-10

- GUI-launched melonDS now enables low-overhead input transport diagnostics with `MELONDS_NSML_INPUT_HEALTH_TRACE=1`, `MELONDS_NSML_INPUT_HEALTH_TRACE_INTERVAL=120`, and `MELONDS_NSML_INPUT_HEALTH_TRACE_WAIT_THRESHOLD_MS=16`.
  - The summary path emits one `NSMB InputHealth: event=summary ...` line every 120 logical frames, about once every 2 seconds at 60fps.
  - Event logs are emitted for `remote-wait-resolved`, `throttle-blocked`, `throttle-resolved`, `recv-gap`, and `send-gap`.
  - Each line reports frame/logicalFrame/sendFrame, last sent/received input frame, lead, local/remote/delayed input queue sizes, wait/throttle/network timings, and rollback/prediction flags.
- Expected overhead: the normal path adds only a few integer checks and one low-frequency stdout line. It does not scan RAM, write CSV rows, or log every input frame. Event lines are only printed when a wait, throttle, or frame gap is observed.
- Current blocker: still needs a real host/client reproduction to determine whether the one-sided stop corresponds to a remote wait, frame-lead throttle, receive gap, bridge packet stall, or later state hash mismatch.
- Next action: on the next reproduction, compare both peers' `melonds.stdout.txt` around the first `NSMB InputHealth` wait/throttle/gap line and the first `game state mismatch` line, then cross-check `bridge.stdout.txt` packet counters for the same wall-clock window.
- Verification: `cmake --build build\release-windows-x86_64 --target melonDS --config Release -j 4`, `cargo fmt --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo clippy-all` in `tools\nsmb-mvl-gui\src-tauri`, and `git diff --check` pass. `corepack pnpm run ci` in `tools\nsmb-mvl-gui` remains blocked by existing Biome CRLF formatting failures in TS/JSON files unrelated to this change; its initial `tsc --noEmit` step passed before Biome failed.

## Current stage/RNG plan sync - 2026-06-11

- User request: choose or randomize courses at room creation time, precompute the maximum possible game count from the win target, and vary Big Star/item RNG seeds every game while keeping host/client synchronized.
- GUI room settings now carry `course_stages` and `rng_seeds` arrays with exactly `wins * 2 - 1` entries. `match_seed` remains as a compatibility field and must match `rng_seeds[0]`.
- `Course=random` generates a fresh course sequence and RNG seed sequence when the room is created or a manual random match is started. It no longer derives the course from one match seed for every game.
- `Course=select` shows one course selector per possible game before room creation/start, so the full course order is fixed before either peer launches.
- Matchmaking schema validates and returns the same arrays to joiners. Tauri passes them to melonDS as `MELONDS_NSML_MVL_STAGE_SEQUENCE` and `MELONDS_NSML_MATCH_SEED_SEQUENCE`.
- melonDS now refreshes the active course from the per-game stage sequence every frame before writing the runtime config, so later games do not fall back to the first selected course after `InitFromEnvironment()`.
- Restart behavior:
  - Same-stage restarts keep using the existing in-game checkpoint path.
  - Mixed-stage restarts no longer call result-scene direct `loadLevel`. That path can leave course model/effect resources from the previous stage alive and caused missing Mario/Luigi rendering, white dash/coin effects, and death-time freezes after a selected course change.
  - melonDS now saves the bootstrap checkpoint at the stable pre-MvL transition point where scene 4 is active and already scheduled to enter scene 6. Restoring this point lets the normal VSConnect/generated-ROM startup path load the next requested course, instead of forcing a level load from the result scene or an early boot scene.
  - Restart startup-frame scheduling is rebased from the saved checkpoint frame, so packet-bridge JIT/input netplay starts at the same relative point after restore. This avoids the second match running hundreds of frames with unsynchronized local input before the helper patch is re-applied.
  - The restart path resets per-game Net/Game RNG from `MELONDS_NSML_MATCH_SEED_SEQUENCE`, clears stale sync/sample caches, and filters pre-restart packets so old stage state is not applied to the next game.
- Verification: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passes. `logs/codex-stage-switch-final-main-20260611` passes split host/client smoke for selected sequence `0,1,2`, seed sequence `101,202,303`, required stages `0,1`, required second MvL game, and required player death between frames 4900-8200 with no ARM abort, stall, or remote-input timeout. Earlier `logs/codex-stage-switch-checkpoint-rebased-20260611` passed the same scenario and confirmed the checkpoint-relative rebase fixed the second-match input/JIT timing mismatch. `logs/codex-stage1-initial-regression-3600-20260611` passes initial stage 1 split smoke for 3600 frames; `logs/codex-select-stage1-initial-6500-20260611` remains the longer initial stage 1 stability check.
- movingHazard follow-up: the strict split CSV comparison failure was a transient verification artifact, not a persistent stage-transition desync. `logs/codex-movinghazard-frame1-noapply-20260611` traced frames 2160-2225 at interval 1 and found only one differing row: frame 2190 had `movingHazardX` off by `0x800`, plus local player visible flags swapped for that same row; frames 2189 and 2191 matched again. The split smoke wrapper now defaults to allowing only these transient fields to settle by the next trace sample when all stable fields re-match. `logs/codex-movinghazard-default-settle-20260611` passes the full 6500-frame selected sequence without `-SkipGameStateComparison` or explicit `-RollbackSettleFrames`.
- Test input note: `tests/nsmb_us_direct_mvl_auto_stage_switch_left_two_wins.inputs` is the main mixed-stage regression input. It wins the first game, lets the precomputed stage sequence advance to stage 1, then drives player 0 left to exercise second-stage death handling. `tests/nsmb_us_direct_mvl_star_collect_left_two_wins_select_stage1.inputs` keeps the native course-selection cursor path available for manual/diagnostic stage-select transition checks.

## Current desync risk review - 2026-06-10

- User symptom: one peer's opponent suddenly stops on-screen, while that opponent can still move on their own screen. The stopped peer later receives movement again, but inputs made during the stop do not appear to have affected the stopped peer's simulation, causing a state split.
- Most likely risk area is the input transport/timing layer, not RNG or stage setup:
  - GUI uses `InputDelayFrames=4`, `InputMaxFrameLead=4`, `MELONDS_NSML_INPUT_UNRELIABLE=1`, and input bundle history `8`.
  - The WebRTC bridge DataChannel is also unordered/unreliable with no retransmits. This means normal play currently has two lossy layers: melonDS input packets are ENet unsequenced, then the bridge sends the UDP payload over an unreliable DataChannel.
  - Input bundle history can recover short packet loss, but a burst longer than the bundle window, or a short send/pump stall, can make the peer wait for old input and look like the opponent stopped.
- Non-rollback input path should normally wait for exact remote input before advancing the logical frame. If the exact input never arrives, GUI's default fatal timeout should terminate the process after about 5s. A temporary stop that later recovers likely means the missing frame eventually arrived through a later bundle or delayed packet before the fatal timeout.
- `ThrottleInputNetplayFrameLead()` sends the current future input, then blocks when `sendFrame - LastReceivedInputFrame > InputMaxFrameLead`. While blocked it pumps network but does not send additional newer input bundles. This can amplify a transient receive gap because the peer may not get fresh bundles from this side until the wait clears.
- Previous related evidence: an older manual peer log already showed `input frame throttle timeout ... lead=5 waitedMs=5000`, so this class of one-sided lead/wait problem is known to be possible under bad timing.
- Things that look less likely from code inspection:
  - RNG seed/state setup: a one-sided temporary opponent stop maps more directly to missing input frames than to RNG divergence.
  - Direct MvL stage setup: the reported mid-match stop/recovery does not match an initial setup mismatch.
  - State hash logging itself: it was added after the report and only exchanges low-frequency reliable diagnostic packets without applying state.
- Next actions:
  - Consider increasing `InputBundleHistory` above `8` for WAN, or send input bundles over a reliable/partially reliable channel while keeping low latency measured.
  - Consider changing frame-lead throttle so a blocked peer can still periodically send the latest input bundle/heartbeat while waiting.
  - On the next reproduction, inspect both `melonds.stdout.txt` and `bridge.stdout.txt` for `NSMB InputHealth`, `remote input timeout`, `input frame throttle`, `PacketBridgeScratchSpike`, `game state mismatch`, and bridge `app->rtc` / `rtc->app` packet counter stalls.

## Current GUI netplay controls - 2026-06-07

- GUI対戦設定に `InputDelayFrames`、`InputMaxFrameLead`、ロールバック有効/無効を追加した。
- 既定値は従来の実用設定を維持し、ロールバック無効、`InputDelayFrames=4`、`InputMaxFrameLead=4`。
- GUI上でロールバックを有効にすると `InputDelayFrames=2` / `InputMaxFrameLead=2` に切り替わり、無効に戻すと `4/4` に戻る。
- GUI起動envでは、ロールバック有効時のみ `MELONDS_NSML_ROLLBACK=1`、backend `coredelta`、window `64`、checkpoint interval `8`、resimulate有効、delta keyframe interval `30`、Main RAM page size `256` を渡す。
- マッチメイキングの room settings schema にも同じ3項目を追加し、参加側にも設定が引き継がれるようにした。
- Verification: `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`、`cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml --all-targets -- -D warnings`、`corepack pnpm run ci` in `tools\nsmb-mvl-gui`、`corepack pnpm run ci` in `tools\nsmb-signaling-server` がpass。

## Current RNG variation fix - 2026-06-01

- User-reported issue: Big Star positions and 8-coin item outcomes looked constant across matches, suggesting gameplay RNG was fixed.
- Root cause: the Rust stable ROM generator still applied the old Python-equivalent `rng-constant` patch to both `Net::getRandom()` and `Game::getRandom()`, making both functions return `0x100` regardless of the match seed. The runtime `MELONDS_NSML_MATCH_SEED` / NetRandom injection wrote the RNG state, but gameplay random callers could not observe it because the functions were patched to return the constant.
- Fix: removed the ROM-side `Net::getRandom()` / `Game::getRandom()` constant patch, and pass `0xffffffff` as the direct `Game::loadLevel` rngSeed stack argument so NSMB uses the match-seeded network/game random state.
- Reusable ROM marker bumped to `nsmb-mvl-reusable-runtime-config-v3`, forcing one regeneration so cached v2 ROMs with constant RNG are not reused.
- Verification:
  - `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`: 4 tests pass, including the direct loadLevel rngSeed stack argument check.
  - `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `corepack pnpm run ci`, and `scripts\test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` pass.
  - `logs/codex-rng-vary-seed-12345678-20260601` and `logs/codex-rng-vary-seed-87654321-20260601`: same stage 2, different match seeds. Host/client remain synchronized within each run, while RNG timeline and initial Big Star position differ by seed:
    - seed `0x12345678`: frame 2500 `netRandomValue=0xc5b2d62c`, Big Star X `0x40000`.
    - seed `0x87654321`: frame 2500 `netRandomValue=0x3791bcf0`, Big Star X `0x210000`.

## Previous visual/gameplay anomaly investigation - 2026-06-01

- User-reported issues after the reusable ROM/runtime-config work:
  - stage 2 snow course background flickers.
  - stage 3 pipe course edge pipes no longer connect left-to-right/right-to-left; entering an edge pipe exits from the respawn pipe position.
  - Mario/Luigi character drawing or animation looks wrong around death and triple-jump states.
- Fixes made:
  - Stage 3 edge-pipe regression: always-on runtime entrance normalization was resetting normal in-stage pipe entrance IDs back to `0/1`. It is now gated behind explicit diagnostic env `MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES`; normal GUI/manual runs no longer enable it.
  - Stage 2 upper background corruption and death/triple-jump sprite corruption: these did not happen when each setting change generated a new ROM, and appeared after runtime RAM injection. Runtime config was writing every frame to high Main RAM `0x023C1100`, which can overlap gameplay heap/asset buffers. The block is now at overlay0 padding `0x020C5360`.
  - Reusable ROM marker bumped to `nsmb-mvl-reusable-runtime-config-v2`, forcing one regeneration so old ROMs that read `0x023C1100` are not reused.
  - Release GUI follow-up: `tools\nsmb-mvl-gui\src-tauri\target\release\nsmb-mvl-gui.exe` had been rebuilt, but the copied Tauri sidecar `target\release\melonDS.exe` was still the older 04:00 binary. That old sidecar still ran the broad entrance-normalization path, so GUI runs could show the pipe/death/background regressions even after the main `build\release-windows-x86_64\melonDS.exe` looked fixed. `scripts\test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` now refreshes sidecars before Tauri build and verifies the release sidecar hash matches `build\release-windows-x86_64\melonDS.exe`.
  - GUI-launched melonDS now removes inherited `MELONDS_NSML_*` variables before applying the GUI's own allowlisted environment. This prevents stale diagnostic force/trace flags from leaking into normal release GUI play.
- ROM camera/stage-lock patch removal was tested and rejected because it regressed the existing death/progress probe. These patches existed in the per-setting ROM generation path, so they are preserved.
- GUI log findings:
  - Recent user GUI logs did change seed/stage (`seed=5 -> stage=0`, `seed=4 -> stage=4`, `seed=2 -> stage=2`, `seed=1 -> stage=1`). The latest visible underground run had `matchSeed=0x00000001`, which maps to stage 1 by `seed % 5`.
  - The same logs also contained `NSMB MvL: normalized entrance spawn state...`, proving the release GUI was launching the stale sidecar and not the fixed melonDS binary.
- Verification:
  - `logs/codex-runtime-config-overlay-v2-stage2-20260601`: stage 2 runtime override passed 1800 frames at ~60 FPS.
  - `logs/codex-runtime-config-overlay-v2-stage3-right-20260601`: stage 3 right-movement/pipe-route smoke passed 3200 frames with entrance normalization disabled.
  - `logs/codex-runtime-config-overlay-v2-death-20260601`: Luigi death/Mario continues probe passed 3600 frames, including no player update-lock and moving hazard progress checks.
  - `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`, `corepack pnpm run ci`, and `scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` all pass.
  - After the final Tauri rebuild, `build\release-windows-x86_64\melonDS.exe`, `tools\nsmb-mvl-gui\src-tauri\binaries\melonDS-x86_64-pc-windows-msvc.exe`, and `tools\nsmb-mvl-gui\src-tauri\target\release\melonDS.exe` all have SHA-256 prefix `8BA24DCEE1FE6B15`.

## Current reusable ROM launcher flow - 2026-06-01

- Tauri 対戦開始時の ROM 生成を初回または共通 ROM 形式更新時だけに変更した。生成済み host/client ROM の横に version marker を保存し、現行形式なら base ROM を読み直さず再利用する。
- direct-entry ROM stub は共有 RAM の runtime configuration block に magic がある場合、course stage、scene settings、Big Star selector、lives 初期値/mode selector をそこから読む。magic がない場合は ROM 生成時の baked 値を使うため、CLI ROM generator の従来用途も維持する。
- melonDS frontend は `MELONDS_NSML_MVL_*` env を起動時に読み、runtime configuration block を毎フレーム早期に更新する。
- GUI は通常の設定変更では ROM を再生成しない。開始時に未生成/旧形式だけ自動準備し、`共通ROM再準備` は明示的な recovery 操作として残す。
- `scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` は 12 GUI backend tests、bridge signaling UDP pair smoke、sidecar refresh/hash check、debug/release preflight、MSI/NSIS 再生成まで pass。GUI-subsystem release exe の preflight は `Start-Process -Wait` で終了コードを取る。
- 同じ ROM ペアを fallback `stage=0` / `Big Star=5` / `lives=endless` で一度だけ生成し、再生成なしで以下を確認した:
  - `logs/codex-reusable-rom-runtime-stage4-w3-s10-l5-v2-20260601`: `stage=4`, `Wins=3`, `Big Star=10`, `lives=5`。
  - `logs/codex-reusable-rom-runtime-stage1-s3-result-20260601`: `stage=1`, `Big Star=3`, `lives=endless`、3 stars で result 遷移。
  - `logs/codex-reusable-rom-runtime-stage2-s10-noresult-20260601`: `stage=2`, `Big Star=10`, `lives=3`、3 stars では result 遷移なし。

## Current game settings fix - 2026-06-01

- Previous settings verification was insufficient: it checked RAM traces and actor presence, but did not visually verify each course, the Big Star HUD above 2 stars, or the lives HUD. The GUI-visible failures reported by the user were real.
- Root cause for the course bug: `stageSceneSettings` was incorrectly treated as packed match rules. Native MvL uses flattened course scene settings `0xB4FF00` through `0xB8FF00` for stages `0..4`. Rust ROM generation, runtime fallback composition, and PowerShell helpers now derive scene settings only from the selected stage.
- Root cause for the Big Star HUD bug: the frontend runtime clamp held native counters at 2 until its logical target was reached. This broke the HUD. The clamp was removed. Rust ROM generation now writes the native overlay selector at `0x0215C88C`: `0/1/2` selects the native `3/5/10` target table.
- Root cause for the lives bug: lives were encoded into the unrelated scene setting byte. Rust ROM generation now writes the native player life globals at `0x0208B364/+4` and life mode selector at `0x0215C89C`. Finite `3/5` uses mode `0`; `endless` uses normal visible lives `3` with mode `2`.
- Verification after rebuilding `build/release-windows-x86_64/melonDS.exe`:
  - `logs/codex-rust-settings-matrix-native-20260601/settings-matrix-summary.csv`: all 27 combinations of `Wins=1|2|3`, `Big Star=3|5|10`, and `Lives=3|5|endless` passed initial route verification.
  - `logs/codex-settings-visual-after-stage0-soft`, `logs/codex-settings-visual-native-stage1-soft`, `logs/codex-settings-visual-native-stage2-soft`, `logs/codex-settings-visual-after-stage3-soft`, and `logs/codex-settings-visual-native-stage4-soft`: screenshots visually show five distinct courses: grass, cave, snow, pipe underground, and castle.
  - `logs/codex-settings-visual-native-stars4-soft/hud-top-enlarged.png`: with native `Big Star=5`, the HUD visibly shows 4 collected stars before victory.
  - `logs/codex-settings-native-target3-snap`, `logs/codex-settings-native-target5-snap`, and `logs/codex-settings-native-target10-snap`: native result transition occurs at exactly `3`, `5`, and `10` stars.
  - `logs/codex-settings-visual-native-lives-5-soft` and `logs/codex-settings-visual-native-lives-endless-fixed-soft`: screenshots visibly show 5 lives for finite `5`, and the normal 3-life display for `endless`.
  - `logs/codex-settings-wins2-round2-probe`: `Wins=2` returns to game 2 after one result.
  - `logs/codex-settings-wins3-round3-native-lives3-20260601`: `Wins=3` returns to game 2 and game 3, then stops restarting after the third win.
- Previous course limitation resolved in the GUI/direct-boot path: `Course=random` and `Course=select` now use a precomputed stage sequence instead of `matchSeed % 5`. Same-stage restarts keep using checkpoints; different-stage restarts restore the saved pre-start bootstrap checkpoint and let the normal startup path load the requested stage. See "Current stage/RNG plan sync - 2026-06-11" for the latest mixed-stage verification.

## Current FPS regression triage - 2026-06-01

- 2026-06-10 GUI install check:
  - User reported that launching melonDS from the installed GUI app at `C:\Users\Sugiyama\AppData\Local\NSMB Mario vs Luigi Online` and then manually using `Open ROM` feels lower FPS than the official melonDS 1.1 package at `C:\Users\Sugiyama\Downloads\melonDS-1.1-windows-x86_64`.
  - Corrected launch-path difference: normal direct ROM launch was running with `JIT.Enable = false`, while GUI battle launch explicitly passes `MELONDS_NSML_ALLOW_JIT=1`. The same `build\release-windows-x86_64\melonDS.exe` therefore used the interpreter for manual `Open ROM` but JIT for the battle path.
  - Direct 300-frame NSMB boot check without JIT: `logs/codex-direct-open-300f-20260610\normal-nojit.stdout.txt` reported `NSMB Test: JIT disabled`, total `16.89fps`, and active `17.14fps`.
  - Direct 300-frame NSMB boot check with only `MELONDS_NSML_ALLOW_JIT=1` changed: `logs/codex-direct-open-300f-20260610\allow-jit.stdout.txt` reported `NSMB Test: JIT enabled`, total `59.81fps`, and active `59.90fps`.
  - After setting the existing release TOML to `JIT.Enable = true`, direct release exe launch without the allow-JIT env reached total `59.80fps` and active `59.88fps` in `logs/codex-direct-open-config-jit-300f-20260610`.
  - After rebuilding `build\release-windows-x86_64\melonDS.exe`, direct release exe launch still reported `NSMB Test: JIT enabled` and active `59.90fps` in `logs/codex-direct-open-after-rebuild-300f-20260610`.
  - The installed GUI-side binary at `C:\Users\Sugiyama\AppData\Local\NSMB Mario vs Luigi Online\melonDS.exe` also reached active `59.88fps` after changing its local `melonDS.toml` to `JIT.Enable = true`, without needing the allow-JIT env (`logs/codex-appdata-direct-config-jit-300f-20260610`).
  - Follow-up no-env title sampling corrected the interpretation for the official package: official melonDS reached `[60/60]` with `JIT.Enable = false` after an initial `[52/60]` sample, and also reached `[60/60]` with `JIT.Enable = true`. CPU sampling averaged about `67.6%` with official `JIT.Enable = false` and `58.6%` with official `JIT.Enable = true`, so the false setting appears to be doing more interpreter work rather than secretly behaving identically to JIT. This remains an inference from config/title/CPU because the official binary does not have the fork's `NSMB Test: JIT ...` log.
  - Fork no-env title sampling with `JIT.Enable = false` showed early `[7/60]` and `[13/60]` samples before reaching `[60/60]`; with `JIT.Enable = true` it started around `[48/60]` and then reached `[60/60]`. The earlier `17fps` 300-frame result describes the fork's `MELONDS_NSML_TEST` harness without JIT, not official standalone behavior.
  - Follow-up source comparison against tag `1.1`: ARM interpreter implementation files are almost unchanged except trivial `popcount`/copyright updates, but `src/ARM.cpp` gained roughly 4.8k lines of NSML runtime hooks and touches `ARMv5::Execute` plus `ARMv5::BusWrite*`. Official 1.1 does not have those hooks in the interpreter-adjacent hot path.
  - ARM.cpp hook split: the heavy trace/diagnostic bodies are gated by explicit envs such as `MELONDS_NSML_PACKET_BRIDGE`, `MELONDS_NSML_PACKET_CAPTURE_LOG`, `MELONDS_NSML_PACKET_REPLAY_FILE`, `MELONDS_NSML_RANDOM_TRACE`, `MELONDS_NSML_CALL_TRACE`, `MELONDS_NSML_TRACE_*`, `MELONDS_NSML_SAFE_*`, and `MELONDS_NSML_PACKET_BRIDGE_*`. Current GUI battle launch does not set `MELONDS_NSML_PACKET_BRIDGE`; it uses `MELONDS_NSML_INPUT_NETPLAY_ONLY`, state sync, input health trace, and the JIT helper patch from `NsmbNetplayPoC.cpp`.
  - Open-ROM GUI launch now removes inherited `MELONDS_NSML_*` variables before setting only `MELONDS_NSML_ALLOW_JIT=1`, matching the match-launch sanitization. This prevents stale diagnostic envs from accidentally enabling ARM.cpp runtime hooks during normal manual `Open ROM`.
  - Follow-up JIT-disabled raw-speed check with frame limiting and audio sync disabled still measured about `16.5fps` over the active window, so the slowdown is not a frontend wait/audio-sync artifact. JIT-enabled still reaches about `60fps` because it bypasses most of the interpreter hot path and uses compiled blocks/fast memory.
  - Current interpretation: official 1.1 is not secretly using JIT when `JIT.Enable = false`; its clean interpreter path is fast enough for this ROM. The fork's JIT-disabled path is slower because NSML PoC instrumentation and diagnostics have polluted the ARM execution path. A small attempted guard around disabled write trace/entrance-normalization calls did not restore interpreter speed by itself, so the remaining fix should separate NSML runtime hooks from normal standalone ARM execution more aggressively or rely on JIT for GUI/manual launch.
  - Fix direction: add `JIT.Enable = true` to the TOML defaults, keep the local release/install TOMLs on JIT enabled, and make the GUI's plain `open_melonds` launcher pass `MELONDS_NSML_ALLOW_JIT=1` so "launch melonDS then Open ROM" matches the battle path's performance. Separately, clean up the ARM interpreter hot path so JIT-disabled standalone launch is no longer penalized by NSML-only hooks.
- User reported that `scripts/run-nsmb-mvl-manual-peer.ps1` no longer holds the post-`09db0f1b` 57-60 FPS behavior and can dip to about 50 FPS. Reproduced before the fix with `logs/codex-fps-regression-baseline-20260601`: active FPS was host `37.52` and client `37.38`.
- The release build configuration still matches the optimized path: `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`, `ENABLE_JIT=ON`, `ENABLE_OGLRENDERER=ON`, and `ENABLE_LTO_RELEASE=ON`.
- Root cause: `b5769a84c` made manual peer runs always pass MvL settings envs (`MELONDS_NSML_MVL_WINS`, `MELONDS_NSML_MVL_BIG_STARS`, `MELONDS_NSML_MVL_LIVES`) and also added those passive settings to `NSMLRuntimeHooksMaybeEnabled()`, causing ARM hot-loop runtime hook checks to be enabled during normal manual-peer play.
- Fix: passive MvL settings envs no longer enable `NSMLRuntimeHooksMaybeEnabled()`. Entrance-spawn write normalization is now behind explicit `MELONDS_NSML_NORMALIZE_MVL_ENTRANCE_SPAWN_WRITES` instead of piggybacking on the broad runtime hook flag.
- Verification after rebuilding `build/release-windows-x86_64/melonDS.exe`: `logs/codex-fps-regression-after-hook-gate-20260601` improved active FPS to host `57.41` / client `57.17` over 1800 frames, and `logs/codex-fps-regression-after-hook-gate-long-20260601` held host `58.41` / client `58.34` over 3600 frames.
- Historical settings smoke after the FPS fix: `logs/codex-fps-regression-settings-smoke-20260601` passed 1200 frames, but its `0xb80500` scene-settings assumption was incorrect and its trace-only checks did not catch the GUI-visible settings bugs. Use the verification under `Current game settings fix` instead.

## Current Actions package status - 2026-06-01

- `NSMB MvL Tauri` GitHub Actions run `26714302373` succeeded for commit `b5769a84c6de0ac8b99b4fb896ccaec7f598de05`.
- Artifacts are present and not expired: `nsmb-mvl-tauri-windows-x86_64`, `melonDS-windows-x86_64`, and `nsmb-net-bridge-windows-x86_64`.
- The packaged Tauri app was generated after Windows melonDS/bridge builds, GUI backend tests, Tauri bundle build, and packaged `nsmb-mvl-gui.exe --preflight`.
- `.github/workflows/nsmb-mvl-tauri.yml` now also runs on `push` to `main` / `master` with the same `paths` filter as `pull_request`.
- This does not replace live two-PC GUI testing against the deployed signaling server; that remains separate from the package-generation proof.

## Current GUI runtime note - 2026-05-31

- Tauri GUI frontend was migrated to React + Tailwind CSS. Current checks: `corepack pnpm typecheck` pass, `corepack pnpm vite:build` pass, and Vite browser render smoke shows the launcher controls.
- Tauri GUI frontend now uses Biome 2.3.12 with signaling-server-style `format-and-lint`, `format-and-lint:fix`, and `ci` scripts. Current checks: `corepack pnpm format-and-lint` pass and `corepack pnpm typecheck` pass.
- Default GUI signaling URL is now `wss://nsmb-mvl-signaling-signaling-prod.uniunitaro.workers.dev/session`; `NSMB_MVL_SIGNAL_URL` remains the override.
- Main worktree sidecars are present, and `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` passes locally.
- `corepack pnpm build` passes and regenerated `tools\nsmb-mvl-gui\src-tauri\target\release\nsmb-mvl-gui.exe`, the MSI bundle, and the NSIS setup exe. The rebuilt release exe `--preflight` resolves the bundled melonDS, bridge, bootstrap input, symbols file, and passes bridge signaling smoke.
- Fixed the command prompt appearing when launching Tauri: release `nsmb-mvl-gui.exe` now uses the Windows GUI subsystem, and bridge/melonDS child processes are spawned with `CREATE_NO_WINDOW`. The rebuilt release exe reports PE subsystem `Windows GUI`, and release `--preflight` still passes.
- A real Tauri GUI host run failed with `bridge exited(1)`.
- Logs for each GUI run are under `%APPDATA%\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-*`; inspect `bridge.stderr.txt` first for bridge exits.
- The observed failure was not a melonDS launch failure. `bridge.stdout.txt` showed the host connected and sent offer SDP; `bridge.stderr.txt` showed the deployed signaling server returned `{"error":"peer is not connected","type":"error"}`.
- Local signaling server code now queues early host/client signaling messages until the opposite role joins, and the GUI now shows the latest log directory directly in the launcher.
- Live GUI testing against the default signaling URL still requires redeploying the Worker with the queued-message fix.
- For manual local triage, use `scripts/run-nsmb-mvl-local-triage.ps1`. `-Mode DirectUdp` generates Rust-patched host/client ROMs and launches the old direct UDP pair without WebRTC. `-Mode WebRtc` uses the same Rust-patched ROM/settings but launches `nsmb-net-bridge` WebRTC without the Tauri GUI. This separates Rust ROM/runtime issues from WebRTC/bridge issues.
- 2026-05-31 DirectUdp triage reproduced the client green/bad-control symptom without WebRTC, then Python-generated ROMs passed the same test. Root cause was the Rust ROM generator treating ARM9 as one linear RAM block; NSMB ARM9 uses copy-table sections, so ARM9 patches such as `Wifi::getCommunicatingConsoleCount` and `Net/Game::getRandom` were written to the wrong offsets.
- Fixed in `tools/nsmb-mvl-rom`: ARM9 section parsing now follows the code settings copy table, and the Rust stable generator also applies the Python-equivalent RNG constant patch. Verification: `logs/codex-rust-arm9section-bothdiff-20260531` passed 3600 frames with host/client gameplay sync and movement inputs; `logs/codex-rust-arm9section-png-20260531` produced host/client PNG screenshots with normal MvL rendering at frames 900/1200.

## Current status - 2026-05-31

- MvL 設定外部化は、direct MvL route の共通 ROM baked fallback と runtime env 注入の両方で受け取れる状態。
- ユーザーが触る通常 MvL 設定として、`Wins=1|2|3`、`Big Star=3|5|10`、`Mario's Lives=3|5|endless`、`Course=random` を扱う。通常 MvL の `Choose Each Time` は CourseSelect を復帰させる必要があるため、現 direct route では未対応。
- stable ROM生成は Python script から Rust crate `tools/nsmb-mvl-rom` へ移行済み。`scripts/generate-nsmb-mvl-stable-roms.ps1` と Tauri GUI command `generate_roms` は Rust 実装を呼ぶ。
- 旧Python ROM toolingの既定symbols pathも `tools/nsmb-mvl-rom/resources/symbols9.x` へ寄せ、Git管理外の `external/` がない環境でも既定値で動かしやすくした。
- Tauri GUI から base ROM、host/client 共通ROM出力先、通常MvL相当設定、signaling URL、部屋コードを指定できる。対戦開始時は生成済み共通ROMを再利用し、未生成または旧形式の場合だけ自動準備する。
- Tauri GUI backend の `start_match` は、host/client別の `nsmb-net-bridge` signaling引数と melonDS 起動envを unit test で確認する。さらに fake bridge/fake melonDS を実際にspawnし、session状態、停止、melonDS起動失敗時にsessionを残さないことを確認する。
- Tauri GUI に `起動前チェック` を追加し、melonDS/bridge/input/symbols の解決と、実bridgeの `webrtc-signaling-udp-pair-smoke` を開始前に確認できるようにした。古いbridgeが新しいsignaling smoke subcommandを持たない場合も検出する。
- `nsmb-mvl-gui.exe --preflight` を追加し、GUIを開かずに同梱sidecar/resource解決とbridge signaling smokeを検証できるようにした。
- Tauri GUI の既定ROM出力先とログ保存先は app data 配下に移し、`tools/nsmb-mvl-rom/resources/symbols9.x` と bootstrap input は bundle resource からも解決できるようにした。これでインストール済みアプリが開発ツリーのパスへ書き込む前提を外した。
- Tauri GUI の `Course=random` は表示中のmatch seedから `stage = seed % 5` を計算し、起動時envにstageを渡す。空欄時はGUI側でseedを生成して表示する。
- 有限ライフ設定時に Luigi の初期スポーン土管が Mario 側へ重なる問題を修正。原因は direct route が通常 CourseSelect setup を飛ばすため、`loadMvsLFilesThread` の早い入口選択で lives byte `3/5` が entrance ID として使われていたこと。
- `tools/nsmb_us_rom_patch.py` で `loadMvsLFilesThread` の初期入口 temp 値を player0=0 / player1=1 に寄せ、土管生成前に通常 MvL の左右入口に近い状態へ戻す。
- `scripts/run-nsmb-mvl-lan-route-smoke.ps1 -RequireMvlInitialSpawnState` は player actor だけでなく、初期スポーン土管に対応する `0x10b` object 2個の `x=0x8000/0x58000` も検証する。
- 検証:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4` 成功。
  - `logs/codex-mvl-initial-pipe-verified-v1`: `MvlLives=3`, `Course=random`, `seed=0`, host/client 両方で `playerActor0X/playerActor1X=0x8000/0x58000`、`mvlObject267LeftX/mvlObject267RightX=0x8000/0x58000`。`repaired initial player spawn` は出ていない。
  - 同ログの software renderer screenshot `frame000895` で、初期土管が左右2本に分離していることを目視確認。
  - `logs\codex-rust-settings-matrix-final\settings-matrix-summary.csv`: Rust生成ROMで `Course=random` / `Wins=1,2,3` / `Big Star=3,5,10` / `Lives=3,5,endless` の27通りがpass。
  - `logs\codex-rust-bigstar-thresholds-final\bigstar-threshold-summary.csv`: `Big Star=3/5/10` の結果しきい値6ケースがpass。
  - `logs\codex-rust-auto-restart-wins2-v2` / `logs\codex-rust-auto-restart-wins3-v2`: `Wins=2` は `nextGame=2`、`Wins=3` は `nextGame=2` と `nextGame=3` へのcheckpoint restartを確認。
  - GUI/Actions: `corepack pnpm typecheck`、`corepack pnpm vite:build`、`cargo check --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`、`cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` 11 tests、`corepack pnpm build`、`actionlint .github/workflows/nsmb-mvl-tauri.yml .github/workflows/nsmb-mvl-gui-local.yml`、`act workflow_dispatch -W .github/workflows/nsmb-mvl-tauri.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest`、`act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j gui-check -P ubuntu-latest=catthehacker/ubuntu:act-latest` がpass。両方の `gui-check` は2026-05-31に再確認済み。
  - `scripts/test-nsmb-mvl-gui-launch-smoke.ps1` を追加し、GUI backend の fake process launch tests と実bridgeの `webrtc-signaling-udp-pair-smoke` をまとめて確認できるようにした。`-BuildTauriBundle` 付きでもpass。
  - `scripts/test-nsmb-mvl-gui-launch-smoke.ps1 -BuildTauriBundle` は debug/release `nsmb-mvl-gui.exe --preflight` も実行し、release exe が `target\release` 直下の `melonDS.exe` / `nsmb-net-bridge.exe` / `resources\symbols9.x` / bootstrap input を解決できることを確認済み。
  - `act workflow_dispatch -W .github/workflows/nsmb-mvl-gui-local.yml -j bridge-check -P ubuntu-latest=catthehacker/ubuntu:act-latest` がpass。2026-05-31に現差分で再確認済み。Tango依存を固定commitでcheckoutし、Ubuntu上で `cargo check --features webrtc --manifest-path tools/nsmb-net-bridge/Cargo.toml` と `cargo run --features webrtc --manifest-path tools/nsmb-net-bridge/Cargo.toml -- webrtc-signaling-udp-pair-smoke` を通した。後者はローカルWebSocket signaling server経由で offer/answer SDP 交換、WebRTC DataChannel接続、host/client相当UDP socket間の双方向payload到達を確認する。
  - Windowsローカルでも `LIBCLANG_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin` を指定して `cargo check --features webrtc --manifest-path tools\nsmb-net-bridge\Cargo.toml` と `cargo build --features webrtc --manifest-path tools\nsmb-net-bridge\Cargo.toml` がpass。生成済み debug exe の `webrtc-signaling-udp-pair-smoke` もpass。
  - release `nsmb-net-bridge.exe` を再buildし、`scripts/prepare-nsmb-mvl-tauri-sidecars.ps1` で Tauri sidecar を更新済み。更新後の `tools\nsmb-mvl-gui\src-tauri\binaries\nsmb-net-bridge-x86_64-pc-windows-msvc.exe webrtc-signaling-udp-pair-smoke` もpass。
  - `tools/nsmb-mvl-rom/resources/symbols9.x` をGit管理対象配下へ置き、ROM generator CLIの既定symbols pathとTauri bundle resource参照をこのファイルへ変更。既定symbols pathでの `generate-stable` と、Tauri release resourcesへの配置を確認済み。
  - 旧Python toolingのsymbols既定値変更後、`python -m py_compile tools\nsmb_localplayer_ref_report.py tools\nsmb_us_rom_tool.py tools\nsmb_us_rom_patch.py` がpass。
  - `.github/workflows/nsmb-mvl-tauri.yml` はWindows runnerで melonDS / bridge / Tauri app を分けてbuildし、最終Tauri bundle artifactをuploadする。melonDS build時間対策として `melonDS.exe` のsource-hash cacheを追加済み。`bridge-windows` はrelease exeの `webrtc-signaling-udp-pair-smoke`、`tauri-windows` はGUI backend unit testを走らせてからbundleする。

## 目的

New Super Mario Bros. DS のローカル対戦専用モード `Mario vs Luigi` を、最終的に `melonDS 1インスタンス * 2PC` で WAN 越しに対戦できる形へ持っていく。

現在の本筋は、LocalMPをWANへそのまま流す方式ではなく、US版ROM向けの direct MvL entry patch と、NSMBが試合中に読む入力境界への adapter を組み合わせる方式。

## 現在の方針

- 対象ROMは US版 `roms/nsmb-us.nds`。
- host は `localPlayerID=0`、client は `localPlayerID=1` の true local player 構成を維持する。
- direct MvL entry ROM patch でローカル通信UIを経由せず、Mario vs Luigi ステージへ直接入る。
- 試合中は `Net::getConsoleKeys(u16)` / `Net::getConsoleTouchPad(u16)` を JIT helper patch で差し替え、WAN入力を player0/player1 入力として渡す。
- `getPacketByte/getPacketTick/getPacketAction` まで差し替えるとステージ状態を壊しやすいため、現時点では keys/touch helper 限定。
- RNG はROM側で定数化しない。hostが試合ごとにmatch seedを生成し、clientへ配布して、両側の `Net::random.value` / `Game::random.value` に同じseedを注入する。
- 実用低遅延路線は `InputDelayFrames=4` / `InputMaxFrameLead=4` / unreliable input + bundle history 8。
- 入力netplay開始時は、host/client双方が `PacketBridgeStartFrame` に到達したことを reliable start-ready packet で確認してから試合入力を開始する。
- 手動対戦では、片側が先行しすぎた場合に5秒でタイムアウトせず、相手が追いつくまで待つ。自動テストだけ内部wait timeoutを使う。
- 高遅延向け rollback は別紙 `docs/nsmb-mvl-rollback-design-notes.md` に保留。現時点の本筋ではない。

## MvLゲーム設定の現状

- 通常LocalMPのMvL設定画面でユーザーが触る項目は、US版ROMの実画面で `Wins` / `Big Star` / `Mario's Lives` / `Course` と確認した。
  - 確認時のデフォルト表示は `Wins=2` / `Big Star=5` / `Mario's Lives=Endless` / `Course=Choose Each Time`。
  - `Course` はユーザー指摘どおり、通常画面では固定コースではなく `Choose Each Time` と `Random` の選択肢として扱う。
- direct MvL routeでは通常のCourseSelect/設定画面を飛ばしているため、GUI向けには再利用可能な共通ROMへ起動時設定を注入する経路を使う。
  - `tools/nsmb-mvl-rom` は Rust製のstable ROM generator。direct MvL entry patch、WiFi communicating count patch、scene settings、Luigi初期入口/土管補正、camera fallback、VS mode skipを生成ROMへ反映する。
  - `scripts/generate-nsmb-mvl-stable-roms.ps1` は `-MvlWins` / `-MvlBigStars` / `-MvlLives` / `-MvlCourseMode` と、raw override の `-MvlSceneSettings` を受け取り、Rust generatorでhost/client ROMを設定付き生成する。
  - `scripts/run-nsmb-mvl-lan-route-smoke.ps1` は `-GenerateMvlConfiguredRoms` / `-MvlCourseMode random` / `-MvlMatchSeed` と上記ユーザー向け設定を受け取り、起動前に一時host/client ROMを生成できる。
  - manual local/peer scriptも同じユーザー向け設定を受け取れる。Tauri GUI側は、初回だけ共通ROMを生成し、通常の対戦開始では runtime env だけを渡す。
  - Tauri GUI側も `Course=random` ではmatch seedからstageを算出し、melonDS起動envへ渡す。
- コース設定:
  - `random` は起動時に `matchSeed % 5` でコース0..4を決める。`logs/codex-mvl-settings-random-stage0` から `stage4` まで、5コースすべてで1300 frames smokeが通過し、期待stageID検証も通った。
  - 2ゲーム目以降は、現checkpoint restart方式では1ゲーム目の正常stage checkpointへ戻すため、起動時に選ばれた同じコースへ戻る。ゲームごとにrandomを振り直す処理は未対応。pre-direct checkpointからstageを差し替える実験はtimeout/ARM9 abortにつながったため外した。
  - `select` / `Choose Each Time` は、飛ばしているCourseSelect部分を復活させる必要があるため未対応。ユーザー要件どおり、難しければ未対応でよい枠として扱う。
  - `fixed` は通常MvLのユーザー向け設定ではない。現状はdirect route内部・検証用の表現としてだけ残す。
- `Wins` / `Big Star` / `Mario's Lives`:
  - raw `sceneSettings=0x00B4FF00` が上記デフォルト表示と対応することは確認済み。
  - ユーザー向け項目は `-MvlWins 1|2|3`、`-MvlBigStars 3|5|10`、`-MvlLives 3|5|endless` として外部指定できる。
  - direct routeでは通常の設定画面/結果後管理を飛ばすため、`Wins` はROM内 `sceneSettings` の高位nibbleへ無理に詰めず、runtime側のmatch targetとして扱う。`Big Star` / `Mario's Lives` / `Course=random` は起動時 runtime config へ反映し、ROM 内の値は非 launcher 利用向け fallback として残す。
  - `scripts/test-nsmb-mvl-settings-matrix.ps1` を追加し、`Course=random` で `Wins` 3通り * `Big Star` 3通り * `Mario's Lives` 3通りを自動検証できるようにした。
  - 旧 `logs/codex-rust-settings-matrix-final` はtrace-only smokeであり、GUI表示の破損を見逃していた。修正後は `logs/codex-rust-settings-matrix-native-20260601` で27通りを再検証した。
  - `stageSceneSettings` はmatch rulesではなくコースIDに対応する。stage `0..4` はそれぞれ `0xB4FF00..0xB8FF00` を使う。
  - `Big Star=3/5/10` はoverlay52のnative target tableを使う。Rust生成ROMがselector `0/1/2` を書き込み、frontend側のlogical count clampは削除した。
  - `logs/codex-rust-bigstar-thresholds-final` で、`Big Star=3` は2個では結果なし/3個で結果、`Big Star=5` は3個では結果なし/5個で結果、`Big Star=10` は9個では結果なし/10個で結果を確認した。
  - 以前の `Wins=3` 候補 `0x00F4FF00`、`Wins=3` / `Big Star=10` の `0x09xxxx`、暫定 `0x39xxxx` は、direct routeでの安定性または勝敗遷移の意味が弱いため採用しない。現行mappingは安定した `0xB?xx00` を使い、match winsはruntime側で管理する。
  - raw逃げ道として `-MvlSceneSettings` は引き続き外部指定可能。ユーザー向け項目より raw override を優先する。
- 複数ゲーム:
  - `tests/nsmb_us_direct_mvl_star_collect_left_continue.inputs` を追加し、結果画面後にAを連打する継続probeを作成した。
  - `logs/codex-mvl-settings-result-continue-probe` では、勝敗確定後 `sceneCurrentSceneID=0xa` の結果シーンに入り、その後9000 frameまで2回目の `sceneCurrentSceneID=0x3` MvL stageへ戻らないことを確認した。
  - 結果シーン中に `Game::loadLevel` を直接呼ぶ方式はARM9 abortしたため不採用。scene requestだけを書き換える方式も、scene IDは戻るがステージオブジェクトが死んだままになるため不採用。
  - 現在は1ゲーム目の正常なMvL stageを内部savestate checkpointとして保持し、結果後にcheckpointへ戻す方式。`logs/codex-mvl-auto-restart-wins2-checkpoint` で、`Wins=2` 相当の1勝後に2ゲーム目へ実ステージとして戻ることを確認済み。
  - `run-nsmb-mvl-lan-route-smoke.ps1` に `-RequireMvlGameCount` を追加した。ただしcheckpoint restartは新規stage entryとしては見えにくいため、最終確認はstdoutの `nextGame` restartログも併用する。
  - `logs/codex-mvl-auto-restart-wins1-checkpoint` で、`Wins=1` 相当では結果シーン到達後に自動再開しないことを確認済み。
  - `logs/codex-rust-auto-restart-wins2-v2` で、`Course=random` / `Wins=2` / `Big Star=5` / `Lives=endless` が結果後に `nextGame=2` へ復帰することを確認済み。
  - `logs/codex-rust-auto-restart-wins3-v2` で、2回勝敗を作ったあと `nextGame=2` と `nextGame=3` のcheckpoint restartまで到達することを確認済み。
  - checkpoint復帰のため、2ゲーム目以降のstageは1ゲーム目と同じ。stdout上の `requestedStage` は進むが、実際のcheckpoint stageは保存時のstageへ戻る。

## 完了済み

- US版ROM patch tooling:
  - `tools/nsmb_us_rom_tool.py`
  - `tools/nsmb_us_rom_patch.py`
- Rust stable ROM generator:
  - `tools/nsmb-mvl-rom`
  - `scripts/generate-nsmb-mvl-stable-roms.ps1` はこのRust実装を呼ぶ。
- stable direct MvL entry ROM:
  - host: `roms/nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds`
  - client: `roms/nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds`
- 通常MvL設定画面のユーザー向け項目のうち、`Wins` / `Big Star` / `Mario's Lives` / `Course=random` を外部指定し、生成済み共通ROMへ起動時反映する経路。
- `Big Star=3/5/10` の勝敗しきい値をruntime側で検証済み。
- `Wins=2` の2ゲーム目復帰と `Wins=3` の3ゲーム目到達を、direct route のcheckpoint restart方式で確認。
- client側を Luigi 視点/localPlayerID=1 として動かす基本経路。
- Luigi死亡時にステージ全体が止まる問題は、通常MvLと比較しながら改善済み。
- 手動入力をhost/clientで別々に受け取り、入力パケットとして相手へ送る input netplay mode。
- `InputMaxFrameLead` による先行制限。
- unreliable input packet + 過去入力bundleによる packet drop 耐性。
- `PacketBridgeStartFrame` 到達時の二者 start-ready barrier。古い同一プロセス用の開始バリアは input netplay 時には使わない。
- 手動peer/local起動では `InternalWaitTimeoutMs=0` をデフォルト化。input frame lead が閾値を超えたときは、desync/終了ではなく同期待ちで止める。
- active FPS と input wait/throttle の計測ログ。
- trace/hook無効時にJITを使える経路。
- 2026-05-30 カメラ追従:
  - `--mvl-camera-lead-from-player-velocity` による独自先読み補正は、本来MvsLと違う挙動になるためstable ROMから外した。
  - 通常LocalMPをUS版ROMで実測し、Luigi右移動時に `playerActor1X - stageCameraGlobalX1 ~= 0x60FFF` へ寄ることを確認した。
  - direct entryではステージ初期化中に `0x020CA880` の bit `0x08` が残り、右移動時も `playerActor1X - stageCameraGlobalX1 ~= 0x80FFF` 付近に固定されることをwrite-traceで特定した。
  - `MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD` を追加し、direct entryで残った初期化hold bitだけを試合開始前に一度クリアするようにした。`0x020AD784` の本来の `Stage::cameraX[player]` 書き込みや、通常の `0x10` runtime camera flag は変更しない。
  - hook有効後、direct entryでも右移動時に `playerActor1X - stageCameraGlobalX1 ~= 0x60FFF` へ戻ることを確認した。
- 不要な古い `logs/codex-*` は適宜削除済み。

## 60fps切り分け結果

ユーザー確認:

- 公式配布版melonDS + US版ROM + 通常LocalMPでは60fps張り付き。

こちらの確認:

- 2026-05-30 software renderer追加調査:
  - 根本原因の一つは Windows clang Release build cache の `CMAKE_C_FLAGS_RELEASE` / `CMAKE_CXX_FLAGS_RELEASE` が空で、software renderer が実質的に最適化なしでビルドされていたこと。重いMvL gameplay中に `RunFrame()` が16.67msを超え、50-55fps級の低下につながっていた。
  - `cmake/DefaultBuildFlags.cmake` と `CMakeLists.txt` で Windows clang Release に `-O3 -DNDEBUG` を明示し、既存buildも同フラグで再configure/rebuild済み。
  - 手動scriptは renderer 設定を正しいTOML sectionへ書くよう修正済み: `[Screen]`, `[3D]`, `[3D.Soft]`, `Instance0.Window0`。
  - PoC smoke/manual scriptで起動する melonDS process は既定で `AboveNormal` priority にする。1PC 2プロセス + software renderer のscheduler競合を減らすため。
  - 連続remote input条件の専用ベンチとして `scripts/run-nsmb-mvl-software-fps-benchmark.ps1` を追加。既定入力は `tests/nsmb_us_direct_mvl_both_different.inputs`。
  - 代表値: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -LogDir logs\codex-software-fps-benchmark-noperf` で host active `59.63fps`, client active `59.56fps`。perf breakdownは小さく負荷を足すため、代表FPS測定では付けない。詳細な内訳が必要なときだけ `-PerfBreakdown` を使う。
  - WAN相当の送信遅延stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -InputSendDelayFrames 2 -InputSendJitterFrames 1` で host/client active `59.25fps`。wait/throttleは増えるが、50-55fps級の崩れは再発しない。
  - 1PC 2プロセスでは OS scheduling と input lead制御により 59fps台で揺れる。実運用のLAN/WAN 2PCでは各PCが1プロセスだけなので、このベンチは保守的なstress条件として扱う。
- `C:\Users\Sugiyama\melon-ds-master-perf` に素の master worktree を作成し、release build 済み。
- フォーク側の通常LocalMP routeは、日本版ROM + 既存入力スクリプトで成立。
- フォーク側通常LocalMPの定常区間:
  - フレーム制限なし: active `85.18fps`
  - フレーム制限あり: active `60.02fps`
- US版ROM + 既存 `tests/nsmb_mario_vs_luigi.inputs` は日本版向け入力スクリプトなので、通常LocalMP routeの自動化には使えない。メニュー/接続待ちを測ってしまうため、FPS baselineとして採用しない。
- US PoC経路、同一PC2プロセス、SwapBuffers毎フレーム、input delay 4:
  - フレーム制限なし: host/client active 約`65.4fps`
  - フレーム制限あり: host `59.89fps`、client `59.86fps`
- US PoC経路、送信遅延2F + jitter1F、フレーム制限あり:
  - host/client active `59.86fps`
  - remote waitは小さいが、throttleは発生する。FPS低下ではなく先行制限として機能している。
- remote input wait は直近測定では主因ではない。フレーム制限あり測定では remote wait はごく小さく、60fpsを維持できた。
- ただし、手動peerログ `logs/nsmb-mvl-manual-peer-host-20260530-024135/host.stdout.txt` では `input frame throttle timeout frame=2538 ... lead=5 waitedMs=5000` を確認。これは現行の手動設定で、片側先行時に5秒で同期待ちを打ち切る問題として扱う。
- 2026-05-30のWebRTC 1PC自動smoke FPS低下調査:
  - ユーザーのLAN 2PC WebRTC実行では60fpsを確認済み。
  - 1PC自動smokeでは、WebRTC/UDP sidecar/直接ENetのいずれでも、起動ハーネスや同一PC上のプロセス競合によりactive FPSが大きく揺れた。
  - 以前の27fpsは実運用性能ではなく、1PC自動smoke環境特有の結果として扱う。接続確認には使うが、FPS評価はLAN 2PCまたは専用計測で行う。
  - 詳細は `docs/nsmb-wan-netplay-roadmap.md` の `1PC auto smoke FPS investigation` を参照。

結論:

- melonDS本体、フォーク全体、通常LocalMPが52fps程度に落ちているわけではない。
- 以前の52fps問題は、Release最適化フラグ欠落、手動起動scriptの表示設定、1PC 2プロセスのscheduler競合が主因。
- `scripts/run-nsmb-mvl-manual-peer.ps1` は、デフォルトを滑らかさ優先に変更した:
  - `SwapBuffersInterval=1`
  - frame limit有効
  - `ShowOSD=false`
  - OpenGL表示、VSync off、JIT enabled
  - JITを切って比較する場合は `-NoJit` を付ける
  - software renderer比較用に `-SoftwareRenderer` を追加
- software rendererでFPSを確認する場合:
  - hidden stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600`
  - visible stress: `.\scripts\run-nsmb-mvl-software-fps-benchmark.ps1 -Frames 3600 -Visible`
  - 手動peer: `.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -SoftwareRenderer` / `.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -SoftwareRenderer`

## 現在の確認状態

- 開始同期は固定raw frameではなく、全ステージ共通の `StageScene active + StageController + player actor 2体` ready条件に寄せている。クリボーなどステージ固有enemy/objectはready条件に使わない。
- RNGはROM側定数化ではなく、match seed同期。seed未指定時はhost生成seedをclientへ配布し、host/client内では一致させる。
- 手動起動のデフォルトbootstrapは `tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs`。試合中の手動入力を上書きしないよう、neutral範囲は `668-839` まで。
- stable ROMでは独自カメラ先読みpatchを使わず、NSMB本来の `Stage::cameraX[player]` 更新をそのまま使う。旧 `MELONDS_NSML_DYNAMIC_CAMERA_LEAD` 実行時hookは診断用で、手動peerではデフォルト無効。
- direct entryのカメラは、通常LocalMPとの実測比較に基づいて `0x020CA880` の初期化hold bit `0x08` だけを一度クリアする。手動peer/local起動では `ClearMvlCameraInitHold` をデフォルト有効にしている。
- MvL設定は `Wins` / `Big Star` / `Mario's Lives` / `Course=random` を外部指定できる。`Course=Choose Each Time` はdirect routeがCourseSelectを飛ばすため未対応、`Course=random` の2ゲーム目以降の再抽選も未対応。
- `Big Star=3/5/10` はGUI/CLI設定値として受け付け、起動マトリクスとnative勝敗しきい値probeを通過済み。Rust生成ROMがnative selectorを書き込み、frontend側で星数をclampしない。
- 残りの注意点: 自動smokeの終了条件はraw frame基準なので、動的start後は片側が先に終了してもう片側がthrottle timeoutすることがある。これは手動対戦の同期ズレとは別のテストハーネス問題として扱う。

## 手動起動

host:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host
```

client:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip>
```

デフォルトは `InputDelayFrames=4` / `InputMaxFrameLead=4` / frame limit有効 / `SwapBuffersInterval=1` / start-ready barrier有効 / `InternalWaitTimeoutMs=0` / JIT有効。

カメラ追従は通常の手動起動では追加指定不要。旧実行時hookを診断目的で重ねる場合だけ `-RuntimeDynamicCameraLead` を付ける。

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -RuntimeDynamicCameraLead
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -RuntimeDynamicCameraLead
```
開始バリアを一時的に無効化して比較する場合だけ `-NoStartBarrier` を付ける。
同期待ちの自動テスト用timeoutを明示的に戻す場合は `-InternalWaitTimeoutMs 5000` のように指定する。
JITなしで比較する場合は `-NoJit` を付ける。

JITなしで peer 起動する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -NoJit
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -NoJit
```

WAN相当の遅延・jitterを試す場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -InputSendDelayFrames 2 -InputSendJitterFrames 1
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -InputSendDelayFrames 2 -InputSendJitterFrames 1
```

フレーム制限を切って余力を見る場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -NoFrameLimit
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -NoFrameLimit
```

melonDSデフォルト相当のsoftware rendererで比較する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role host -SoftwareRenderer
.\scripts\run-nsmb-mvl-manual-peer.ps1 -Role client -Peer <host-ip> -SoftwareRenderer
```

同一PCで2窓起動する場合:

```powershell
.\scripts\run-nsmb-mvl-manual-local.ps1 -LowDelayWan -SoftwareRenderer
```

`run-nsmb-mvl-manual-local.ps1` と `run-nsmb-mvl-manual-peer.ps1` は、手動/LAN実用検証の速度優先でデフォルトJIT有効。JITなしで比較する場合だけ `-NoJit` を付ける。

## 検証コマンド

通常LocalMP baseline:

```powershell
.\scripts\run-nsmb-mvl-route-smoke.ps1 -Exe build\release-windows-x86_64\melonDS.exe -Rom roms\nsmb.nds -Frames 3600 -AllowJit -NoScreenshots -NoHashLog -NoRngPatch -QuietLog -ActiveFpsStartFrame 3000
```

US PoC 低遅延入力同期 baseline:

```powershell
$env:MELONDS_NSML_SWAPBUFFERS_INTERVAL='1'
.\scripts\run-nsmb-mvl-lan-route-smoke.ps1 -RunRole both -Exe build\release-windows-x86_64\melonDS.exe -Rom roms\nsmb-us.nds -HostRom roms\nsmb-us-direct-mvl-entry-stable-host-true-local0-wificount2-vslockskip-netaid.tmp.nds -ClientRom roms\nsmb-us-direct-mvl-entry-stable-client-true-local1-wificount2-vslockskip-netaid.tmp.nds -InputScript tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs -Frames 3000 -ScreenshotInterval 0 -NoHashLog -SkipMvlStateCheck -SkipGameplayActorCheck -NoLanMP -InputNetplay -InputDelayFrames 4 -InputMaxFrameLead 4 -PacketBridgeJitHelperPatch -PacketBridgeJitHelperPatchFrame 840 -PacketBridgeStartFrame 840 -WaitForPeerAtNetplayStart -AllowJit -InputUnreliable -InputBundleHistory 8
Remove-Item Env:\MELONDS_NSML_SWAPBUFFERS_INTERVAL -ErrorAction SilentlyContinue
```

直近の検証:

- 通常LocalMP baseline: `logs\codex-camera-original-localmp-us-probe1` / `logs\codex-camera-original-localmp-dbgfields`。Luigi右移動時の `playerActor1X - stageCameraGlobalX1` は約 `0x60FFF`。
- direct entry + camera init hold clear: `logs\codex-camera-clear-init-hold-hook-screens`。Luigi右移動時の `playerActor1X - stageCameraGlobalX1` は約 `0x60FFF` へ戻る。
- `scripts\run-nsmb-mvl-manual-local.ps1 -Frames 1200 -LowDelayWan` で、手動local wrapperから `ClearMvlCameraInitHold` がhost/client両方に渡ることを確認。
- `-CheckHostClientGameplaySync` 失敗調査:
  - 直近の frame 880/1280 付近の失敗は、検証コマンド側で `PacketBridgeStartFrame` / `PacketBridgeJitHelperPatch` などの必須フラグを落としていたために発生していた。正しい低遅延入力同期フラグでは `tests\nsmb_us_direct_mvl_minimal_bootstrap.inputs` / 2200 frames がpass。
  - `tests\nsmb_us_direct_mvl_both_different.inputs` / 3600 frames では、player座標、スター、moving hazard、object数、入力状態などのゲームプレイ項目は全行一致したが、`netPacketTick` だけ frame 2100/2560 で一時的に差が出ていた。
  - `netPacketTick` はNSMBの通信packet作業領域で、反映済みゲーム状態そのものではないため、`-CheckHostClientGameplaySync` の必須比較から外した。必要な場合だけ `-CheckHostClientNetPacketTickSync` で別途検査する。
  - 修正後、`tests\nsmb_us_direct_mvl_both_different.inputs` / 3600 frames / `-CheckHostClientGameplaySync` はpass。`-CheckHostClientNetPacketTickSync` を明示した場合は従来どおり frame 2560 のtick差を検出する。
- MvL設定外部化:
  - `logs\codex-rust-settings-matrix-native-20260601\settings-matrix-summary.csv`: Rust生成ROMで `Course=random` / `Wins=1,2,3` / `Big Star=3,5,10` / `Lives=3,5,endless` の27通りがpass。各caseでstageID、sceneSettings、player actor、Vs star actor、StageScene active、初期livesを確認。
  - `logs\codex-rust-bigstar-thresholds-final\bigstar-threshold-summary.csv`: `Big Star=3/5/10` の結果しきい値6ケースがpass。
  - `logs\codex-rust-auto-restart-wins2-v2`: `Course=random` / `Wins=2` で結果後に `nextGame=2` へ復帰することを確認。checkpoint方式のため、2ゲーム目のcourseは1ゲーム目と同じ。
  - `logs\codex-settings-wins3-round3-native-lives3-20260601`: 修正後のnative lives設定で `Wins=3` の `nextGame=2` と `nextGame=3` へのcheckpoint restart、3勝後にrestartしないことを確認。
  - Rust ROM generator parity: Python生成ROMとRust生成ROMの主要patch領域 (`0x021577EC`, `0x020C5298`, `0x020A06DC`, `0x02013428`, `0x02159348`, `0x0200FAE0`) の一致を確認。

## 注意

- `docs/nsmb-mvl-rollback-design-notes.md` は rollback 議論の保存先。肥大化させず、rollback再開時だけ参照する。
- `logs/` はROMコピーを含むため肥大化しやすい。検証結果はdocsに要約し、古い `logs/codex-*` は削除する。
- final response 前には、このファイルの古い「次にやること」や解決済みblockerが残っていないか確認する。
