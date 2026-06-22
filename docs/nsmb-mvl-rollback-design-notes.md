# NSMB Mario vs Luigi Rollback Design Notes

## 2026-06-22 current status - raw-input resim and strong-jitter rollback

Current rollback direction has moved from repair-only `tinycorepreimage` toward exact-ish `tinycoreramdelta` as the correctness oracle, while the next practical path likely needs a new non-blocking or pointer-safe snapshot architecture. The active goal has a strict latency constraint:

- Promotion candidates must keep the GUI rollback standard `InputDelayFrames=2`.
- Increasing InputDelay above `2` is rejected for this goal, even when it improves correctness or smoothness, because it makes rollback no longer worthwhile for the target play feel.
- Higher-delay profiles such as `delay10` and `delay12/keyInt4` are retained only as diagnostics showing the cost/latency boundary.

Completed in the latest pass:

- Fixed a rollback resimulation input-semantics bug. Normal frames feed the DS hardware input from the current raw local script frame while NSMB's `Net::getConsoleKeys` sees the delayed/effective netplay input. Rollback resimulation was feeding the delayed input to both paths, so resimulated frames did not match normal execution. Resim now uses delayed/effective input for packet scratch and raw `f + InputDelayFrames` local input for DS hardware.
- Strengthened `tinycorepreimage` restore anchoring with lightweight Main RAM snapshots/preimages. The preimage backend now keeps usable future anchors, avoids self-base preimage restores, preserves required preimage chains while pruning, and stores periodic/anchor `MainRAMCopy` snapshots.
- Made the state-sync gameplay hash more role-invariant. Local-only input/role/camera/net fields were removed from the strict hash, while player actor/global, MvL manager/gate, moving hazard, object lifecycle, and active object identity fields were added.
- Added persistent `playerGlobal` mismatch detection. Split smoke now treats a one-sample rollback correction mismatch as transient when it settles within `RollbackSettleFrames`, but still fails repeated `playerGlobal=0` samples that persist beyond the settle window.
- Added practical-suite support for artificial input send delay/jitter and the strong-jitter candidate above.
- Added an explicit `-RollbackPredictOnly` smoke path so rollback prediction can run without silently enabling full resimulation. This is used to measure Plan-D-style repair candidates independently from restore/resim cost.
- Practical-suite summaries now record transient mismatch count, max transient settle frames, and involved fields in addition to avg/max/over33 frame timing, so a run cannot look healthy solely because the final status or average FPS is acceptable.
- Added delay-2 predict/repair suite candidates:
  - `predictrepair-delay2-playerstate`
  - `predictrepair-delay2-player-world-lite`

Verification:

- Latest build passed after the JIT reset experiments: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel 4`.
- Normal jitter suite, `InputSendDelayFrames=3` / `InputSendJitterFrames=3`, `delay6/rbwait3000/maxresim2`, no full game-state trace, passed stocktouch, chaos, contact, and dualstresslong in `logs/codex-goal-practical-rbwait3000-rawinput-jitter3-20260622/20260622-040447`. Averages were about `16.7-17.1ms`; max frames were `37-47ms`.
- The same candidate passed death in `logs/codex-goal-practical-rbwait3000-rawinput-stars-death-jitter3-20260622/20260622-041022`. `luigistar` and `mariostarleft` did not prove star behavior because the route did not collect a star on either peer.
- Strong jitter rejected `delay6/rbwait3000/maxresim2`: `InputSendDelayFrames=6` / `InputSendJitterFrames=6` produced persistent `playerGlobal=0` in chaos/dualstresslong. This is the important failure mode the user asked to catch.
- Strong jitter with uncapped rollback correctness passed a direct chaos run at `delay10/rbwait3000` in `logs/codex-goal-chaos-inputdelay10-send6-jitter6-uncapped-settlecheck-2400-20260622`; the only `playerGlobal=0` sample was a single transient at frame `1140`.
- Strong jitter practical suite passed chaos/contact/dualstresslong with `tinycorepreimage-delay10-lead999-rbwait3000-uncapped-bundle8` in `logs/codex-goal-practical-delay10-uncapped-send6-jitter6-20260622/20260622-044053`:
  - `chaos`: avg `17.664/17.654ms`, max `77.520/77.877ms`, over33 `32/30`; one transient `playerGlobal=0` at frame `1140`.
  - `contact`: avg `17.170/17.172ms`, max `47.603/38.975ms`, over33 `5/7`.
  - `dualstresslong`: avg `17.727/17.734ms`, max `57.159/50.181ms`, over33 `46/45`.
- `delay12/keyInt4` improved the high-jitter spike profile in diagnostics, but is rejected as a promotion path because InputDelay is above `2`.
- Delay-2 full rollback baseline:
  - `delay2/maxlead2` under `sendDelay=6/jitter=6` stalls on peer-lead throttle rather than rollback itself.
  - `delay2/maxlead999` avoids throttle, but deep rollback resimulation still produces about `300ms+` spikes and persistent mismatch in stress routes.
- Delay-2 predict/repair:
  - `predictrepair-delay2-playerstate` on chaos: fast (`16.930/17.084ms`, max `41.643/41.689ms`) but fails with persistent `playerGlobal=0` at frames `1410-1470`.
  - `predictrepair-delay2-player-world-lite` on chaos: passes, avg `18.687/18.683ms`, max `43.353/32.226ms`.
  - `predictrepair-delay2-player-world-lite` suite: stocktouch/contact pass; death failed only because `-NoGameStateTrace` disables the death assertion; dualstresslong fails with persistent `playerGlobal=0` around frames `1860-1920`.
  - Reproducing the dualstress failure with diagnostics showed death/transition state arriving too late for predict-only repair. Same-frame waits (`RollbackInputWaitUs=1000/1500/3000`) can remove `playerGlobal=0`, but active FPS falls to roughly `52/50/47fps`.
  - Budgeted resim (`RollbackMaxResimFrames=2`) plus world repair is rejected for now: it produced `101.592ms` max frame time and about `41.6fps` active in `logs/codex-goal-delay2-budgetresim2-worldlite-dual-failseed-20260622`.
- Delay-2 Plan-D-style actor/global/world snapshot experiments:
  - Added host/global apply modes, player transition-step packing, moving-hazard world-state fill/apply, player-global stale repair experiments, and field-level transient mismatch classification in the split smoke.
  - `predictrepair-delay2-player-world-actorsnap-hostglobals` remains fast under strong jitter (`logs/codex-goal-suite-hostglobals-dualstress-20260622/20260622-071044`: `16.692/16.690ms`, max `40.671/31.881ms`) but still fails persistent `playerGlobal=0` during death/transition.
  - Full `GameState` sync every 10F fixes the early playerGlobal window but is rejected on spikes (`logs/codex-goal-suite-hostglobals-sync10-dualstress-20260622/20260622-071407`: max `102.053/649.777ms`).
  - `MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES=12` reduces persistent global mismatch without large average FPS cost, but dualstresslong still shows player actor respawn lag after death (`playerActor0X/Y` diverges after the player0 death/respawn window).
  - Stale transform repair is rejected: both 12F and 4F variants made player actor Y drift worse. Longer stale global/counter-only repair (`24F`) also caused stale writes or earlier playerGlobal mismatches.
  - Rechecked exact paths under the delay-2 rule: `coredelta-baseline` is still too slow (`22.350/22.349ms` avg, `301.826/341.196ms` max), and `exact-delay2-tinycorepreimage-skiprender` is also too slow (`19.093/19.337ms` avg, `163.197/166.475ms` max).
  - Added transition-transform experiments to allow remote player transform writes during death/respawn transitions. A direct dualstresslong run once passed under strong jitter (`logs/codex-goal-suite-transitiontransform-skipmove-dualstress-20260622/20260622-080101`, `16.762/16.765ms` avg), but multi-route and repeated runs were not stable.
  - Prediction-width sweep under strong jitter (`logs/codex-goal-suite-transition-predict-sweep-20260622/20260622-081153`) showed no promotion candidate: `pred0` passed one dualstresslong run with practical timing but failed stocktouch/chaos, `pred4` produced persistent `playerGlobal`, and `pred8` still had actor/transition mismatches and an occasional large spike.
  - Added `MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET` after finding that transition-transform during the initial pipe/entry transition can create early `playerGlobal`/actor mismatches. Offset `300` prevents applying the transform path during match entry, but it still fails strong-jitter chaos/dualstresslong (`logs/codex-goal-suite-transition-offset300-20260622/20260622-083931`) through persistent playerGlobal, object count drift, or active-frame spikes.

Current conclusion:

- The previous `RollbackMaxResimFrames=2` cap is correctness-unsafe under strong jitter. If a prediction mismatch is older than the cap, capping the rollback start frame leaves already-simulated wrong input in gameplay state and can become a persistent `playerGlobal` mismatch. For correctness-focused runs, use uncapped rollback or a cap high enough to cover the network condition.
- Deep uncapped rollback restores correctness better than predict-only repair, but at `InputDelayFrames=2` it does not remove visible spikes. Higher-delay candidates are useful diagnostics but not acceptable solutions.
- Predict-only Plan-D repair proves that a low-cost path exists, but not yet a complete correctness path: under strong jitter, death/transition globals and player/world basics can be unknown at the current frame unless the emulator waits or resimulates.
- Same-frame waits are not equivalent to increasing InputDelay, but the measured FPS cost is still too high for the target.
- Increasing GUI rollback `InputDelayFrames` above `2` is not a valid escape hatch for this goal; any viable rollback path must keep delay2 and solve correctness/perf there.
- Current repair-only actor/global snapshot direction is likely at its limit for the active goal. It can keep average frame time near 60fps, but without either waiting, increasing delay, or doing some exact re-execution, it cannot know discontinuous current-frame events such as entry transition, death/respawn, contact, object spawn/despawn, and item lifecycle under 6F artificial send delay/jitter.
- Exact rollback pre-pump experiment: `MELONDS_NSML_ROLLBACK_PRE_PUMP_BEFORE_RESIM=1` reduced resim operations/frames slightly in one 1800F dualstress trace, but max/avg spikes did not improve (`~149-158ms` max active frame) and max per-resim-frame cost worsened. This is not a promotion path.
- Plan-D actor lifecycle experiment:
  - `WireWorldActorSnapshotState` now carries 32 actors instead of 16.
  - Optional actor snapshot lifecycle apply can write remote `stateType`/flags onto matched local actors.
  - Optional extra-actor pruning was added, currently restricted to duplicated `0x10C` star-candidate actors because broad pruning removed desyncs but caused large spikes (`330-605ms`) and lower active FPS.
  - The practical comparison now uses significant active-object multiset comparison, excluding known local-role `StageFX(0x12)` and `MvsLObject267(0x10B)`, rather than raw `objectActiveCount`/`objectDeadCount`.
  - `predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-pred0` passed `dualstresslong` with delay2 strong jitter in `logs/codex-goal-actorsnap32-prune10c-transition90-repeat-send6-jitter6-20260622/20260622-094618` (`16.690/16.691ms`, max `60.372/31.202ms`) but `chaos` still failed the spike gate in the same run (`158/117ms` max), and other `chaos` runs still expose moving-hazard or playerGlobal timing windows.
- Latest exact rollback delay2 rechecks:
  - Raising GUI rollback `InputDelayFrames` above `2` is explicitly out of scope for this goal. It can remain a diagnostic, but it is not a valid fix path.
  - `RollbackMaxResimFrames=1` is too aggressive and breaks correctness (`playerActor1X` mismatch at frame `990`).
  - `RollbackMaxResimFrames=2` is the best exact delay2 point measured so far, but it still has visible spikes: `17.497/17.509ms` average, `55.537/58.361ms` max, over33 `82/83` on chaos with `InputSendDelayFrames=6` / `InputSendJitterFrames=6`.
  - `InputMaxFrameLead=2` plus micro-wait stalls under the same strong-jitter condition; `InputMaxFrameLead=999` plus micro-wait avoids stalls but worsens average timing. These do not solve the active goal.
  - Resim profiling with `InputNetplayTrace` shows the main cost is `RunFrame` during re-execution, not snapshot copy: a 2F resim commonly spends restore `~3-5ms`, checkpoint save `~1.5-5ms`, and resimulated `RunFrame` `~20-35ms`.
  - Skipping intermediate resim checkpoints did not reduce spikes, and pre-pumping network packets before resim either broke correctness under a 2F cap or remained too slow uncapped.
  - Direct raw actor memory copy is not safe as-is because actor structs contain pointers/link fields whose addresses can diverge between host and client. The next actor-authority design should be pointer-safe: copy selected non-pointer arena pages, remap actor links, or use a host-authored page-delta with exclusions.
- NSMB process-list selected-range rollback:
  - `nsmbtinycore` plus process-list ranges proves the checkpoint/restore side can be made cheap under delay2: process-list/no-heap saves are around `0.12-0.18ms`, restores around `3ms`, and playerC80 1500F chaos traces can keep active frames at `16.667ms` with max under `27ms`.
  - The raw selected-range approach is still not reliable enough. `player=0xC80` fixes one chaos short trace and passes `dualstresslong`, but contact still diverges in player Y/global/moving-hazard state and chaos can stall in longer practical runs.
  - Broadening raw ranges is dangerous. `actorArena`/`ARM9 stack` can trigger `80-100ms` resim frames or ARM9 aborts, and moving-hazard `0x500` raw tail copying makes contact run at `59/94ms` average. The evidence points to pointer-heavy or scheduler-sensitive actor internals being restored in an inconsistent state.
  - Next architecture should use selected field/page snapshots with exclusions rather than object-tail memcpy: keep non-pointer player/contact/hazard fields, skip or remap links/vtables/process nodes, and treat full exact rollback only as an oracle.
- Tiny core + Main RAM delta rollback:
  - Added `tinycoreramdelta`, a new backend that stores tiny core state plus full/delta Main RAM page ranges. This avoids full core savestate cost while keeping Main RAM exact.
  - It confirms the current tradeoff: exact-ish rollback can remove mismatch under delay2 strong jitter, but same-frame rollback still causes visible spikes. The best measured candidate is `exact-delay2-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender`.
  - Contact with that candidate had no persistent mismatch but still failed the spike gate: `17.407/17.360ms` average, `93.251/92.990ms` max, over33 `17/14`.
  - Dualstresslong and chaos also had no persistent mismatch, but remained above the smoothness target: averages `18.1-18.3ms`, max up to `163ms`, and over33 `78-167`.
  - `tinycoreramdelta` cannot safely use the tinycorepreimage/nsmbtinycore default JIT-reset skip. With `RollbackSkipJitReset`, contact became faster but produced persistent player actor position mismatch. The wrapper now treats `tinycoreramdelta` separately: tiny flags and resim render skip are applied, but JIT reset skip is opt-in only.
- Latest JIT reset boundary tests:
  - The active goal remains delay2-only. Increasing GUI rollback `InputDelayFrames` above `2` is rejected and is not a valid mitigation.
  - Forced JIT invalidation was added for Main RAM delta ranges plus tiny-core-restored Shared WRAM, ARM7 WRAM, and ITCM. It also uses 16-byte invalidation to cover literal dependencies, not only code pages.
  - Even with that invalidation, `RollbackSkipJitReset` still mismatched (`playerActor0X` at frame `2010`) on contact in `logs/nsmb-mvl-rollback-jit-forceinv16-contact/20260622-134436`. `MELONDS_NSML_ROLLBACK_JIT_MEMORY_RESET_ONLY=1` failed the same way in `logs/nsmb-mvl-rollback-jit-memreset16-contact/20260622-134604`.
  - This means the safe restore boundary is not just "invalidate restored memory pages". Current JIT blocks can depend on CPU/JIT state restored by savestate in a way that is not captured by local region invalidation. Normal full JIT block reset is still the correctness boundary for exact Main RAM restore.
  - A diagnostic `MELONDS_NSML_ROLLBACK_JIT_FAST_RESET=1` path detaches all JIT blocks immediately and defers JitBlock metadata deletion. It preserved contact correctness, proving full block detachment is enough for correctness, but performance was worse than normal reset (`611ms` max with default deferred delete budget, `139/142ms` max with delete budget `0`). This suggests recompile/code-cache churn dominates after full detachment.
  - `RollbackInputWaitUs=500` without increasing delay preserved correctness but did not solve spikes (`90/107ms` max). `InputMaxFrameLead=2` under strong jitter stalled. These are not promotion paths.
- JIT configuration boundary tests:
  - Added NSML env overrides for JIT args so rollback can test conservative JIT without changing normal GUI settings.
  - The earlier `MELONDS_NSML_JIT_LITERAL_OPTIMIZATIONS=0` plus `MELONDS_NSML_JIT_FAST_MEMORY=0` skip-reset result did not survive repeat testing. Contact later reproduced persistent `playerActor0X` mismatch at frame `2010`, so the branch is diagnostic-only.
  - After fixing suite env leakage, more conservative skip-reset variants still failed: maxBlock8 with branch off, maxBlock4, maxBlock4 with branch off, and `conservativejit4` all mismatched in `logs/nsmb-mvl-rollback-jitmb4-conservative-contact-envfix/20260622-145515`.
  - Disabling JIT restore candidates did not fix skip-reset correctness. `exact-delay2-jitmb8-noliteral-nofastmem-norestorecand-tinycoreramdelta-key2-page256-cp2-maxresim1-skipjit-skiprender` still mismatched at frame `2010`.
  - Larger Main RAM page sizes are not safe by default on this branch. Page512/page1024 variants mismatched contact, so page256 remains the safer exact-ish delta baseline.
- Fast JIT reset follow-up:
  - Added `MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_INITIAL_DELETE_BUDGET` to separate immediate detached-block deletion from compile-time deferred deletion. The default remains `64`; rollback experiments can set it to `0`.
  - `exact-delay2-fastjitreset-nodelete0-tinycoreramdelta-key2-page256-cp2-maxresim1-skiprender` preserved correctness in contact/chaos/dualstresslong under strong jitter, but still missed the playability target. Contact had max `106/101ms`; chaos/dualstresslong averaged around `18.0-18.15ms` with max `90-100ms` and too many over33 frames.
  - `FpsSpikeTrace` is now plumbed through the practical suite. Trace shows the remaining spike is not checkpoint copy/restore: restore max is about `8.7ms`, checkpoint save about `2-3ms`, but rollback/resim phases hit `40-70ms` after JIT block detachment.
  - `MELONDS_NSML_ROLLBACK_JIT_FAST_RESET_KEEP_CODEMEM=1` is unsafe. Keeping code memory while detaching block maps reduced timing but produced contact mismatch at frame `2820`.
  - Same-frame wait combined with fast reset is not a fix: `RollbackInputWaitUs=1000` worsened active averages to about `18.2ms`, and `1500us` mismatched.
- 2026-06-22 delay2 JIT-boundary retake:
  - The active goal has been restated as delay2-only. Raising GUI rollback `InputDelayFrames` above `2` is rejected even if it hides desync or smooths spikes.
  - Interpreter/no-JIT contact passed with the same delay2 strong-jitter input (`logs/nsmbrb-interp-contact/run1`), so the tiny-core/Main RAM delta state is broadly sufficient for that route. It is not playable because the run took about `228s` for 3600 frames.
  - Normal full `JIT.Reset()` on restore preserved contact correctness but stayed too slow: `17.759/17.752ms` average and `128.823/127.090ms` max in `logs/nsmbrb-normaljitreset-contact/20260622-155447`.
  - Region-masked fast reset was tested with short candidates:
    - `pv-main` mismatched contact.
    - `pv-maincore`, `pv-mainshared`, and `pv-mainshared-arm7` avoided persistent mismatch in one contact sweep but still exceeded the spike gate.
    - `pv-volatile` and `pv-volatile-j8nf` mismatched on repeat, so partial fast reset is not deterministic enough to promote.
  - `RollbackResimulateDelayFrames=2` coalescing on the normal-reset baseline did not solve visible spikes (`host maxFrameMs=114.364`, client `91.620`).
  - Working conclusion: safe exact-ish delay2 rollback currently requires full JIT block reset/detachment. The remaining cost is cold JIT/resim execution. Skipping or partially reusing JIT blocks is fast but still incorrect, so the next architecture should focus on pointer-safe JIT reuse or avoiding same-frame cold-JIT re-execution, not on increasing delay.

Current blocker / next actions:

- Keep delay2 as a hard requirement; increasing GUI rollback InputDelay above `2` is not allowed for this goal.
- Exact same-frame resim is not yet a playable endpoint for delay2. The reliable boundary remains full/fast JIT block detachment, and the remaining spike is cold JIT/resim execution rather than checkpoint copy/restore.
- Continue from the fast-reset nodelete0 correctness baseline, but do not treat it as playable. The next real path needs either pointer-safe rollback JIT reuse or a new rollback architecture that avoids same-frame cold-JIT re-execution while keeping host/client state exact.
- Add stronger event routes for star pickup/drop/recover, block break persistence, and 8-coin item identity. Current star routes are still input coverage failures, not correctness proof.
- Keep testing with artificial send delay/jitter and movement-heavy chaos/contact routes. Average FPS alone is insufficient; `maxFrameMs`, `over33ms`, and persistent mismatch detection must stay in the promotion gate.

## 2026-06-07 GUI rollback settings exposure

Current GUI default remains the non-rollback input-delay path:

- rollback: disabled
- `InputDelayFrames`: `4`
- `InputMaxFrameLead`: `4`

The GUI battle settings now exposes `InputDelayFrames`, `InputMaxFrameLead`, and rollback enable/disable. Toggling rollback on sets `InputDelayFrames=2` and `InputMaxFrameLead=2`; toggling it off restores `4/4`.

When rollback is enabled from the GUI, melonDS is launched with coredelta rollback envs (`MELONDS_NSML_ROLLBACK=1`, backend `coredelta`, window `64`, checkpoint interval `8`, resimulation enabled, delta keyframe interval `30`, Main RAM page size `256`). This is an exposed experimental path for comparing delayed rollback against the current GUI default, not a new playability claim.

Current verification status:

- Passed: `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`.
- Passed: `cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml --all-targets -- -D warnings`.
- Passed: `corepack pnpm run ci` in `tools\nsmb-mvl-gui`.
- Passed: `corepack pnpm run ci` in `tools\nsmb-signaling-server`.
- Pending: manual GUI play comparison of default `delay4/lead4` vs rollback `delay2/lead2`.

## 2026-06-07 current status - practical tinycorepreimage candidate

Current practical rollback candidate is `tinycorepreimage-rbwait1500-window32`:

- backend: `tinycorepreimage`
- input delay: `0`
- input max frame lead: `2`
- rollback same-frame input wait: `1500us`
- rollback window: `32`
- checkpoint interval: `1`
- JIT enabled

Completed in the latest pass:

- Fixed a preimage shadow bug after rollback resimulation. The frame-delta shadow is now refreshed at the current frame after resim, not at the restored checkpoint frame. This removed a class of false preimage chains that later produced `chain missing`.
- Strengthened rollback history pruning so retained checkpoints also keep every base/preimage chain required by currently kept checkpoints and by the active frame-delta shadow. This prevents pruning a base frame that is still needed for restore.
- Added rollback integrity detection to `scripts/run-nsmb-mvl-practical-rollback-suite.ps1`; `cannot resimulate`, missing checkpoint, missing delta chain, restore failure, and rollback failure logs are now classified as `rollback-fail` instead of being hidden behind generic perf/mismatch failures.
- Added the `tinycorepreimage-rbwait1500-window32` and `tinycorepreimage-rbwait1500-bundle8` suite candidates. `bundle8` is not promoted because it improved some transport timing but introduced correctness failures.
- Made FPS spike phase tracing opt-in through `-FpsSpikeTrace` for split smoke and stopped forcing heavy trace/perf breakdown in the manual low-latency wrapper. Normal manual rollback runs are lighter by default, while active frame timing still records avg/max/over33ms.
- Fixed a result/restart bug: the MvL auto-restart checkpoint is saved before the packet-bridge JIT helper patch, so restoring it after result removed the patch from Main RAM while `PacketBridgeJitHelperPatchApplied` still stayed true. Auto restart now clears that applied flag after savestate restore, causing the helper patch to be reapplied on the next frame. This fixed second-game remote input not affecting the peer.
- Updated `secondgame` practical route to use `MvlLives=3`; with endless lives it only accumulated synchronized deaths and never reached result.

Verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- PowerShell parse passed for the touched rollback suite/manual/smoke scripts.
- `logs/codex-practical-suite-window32-sixroute-jitpatchfix-20260607/20260607-021914/summary.csv` passed all six current practical routes:
  - `stocktouch`: avg `18.371/18.370ms`, max `52.904/45.171ms`, over33 `3/7`
  - `chaos`: avg `17.317/17.315ms`, max `40.648/48.909ms`, over33 `3/7`
  - `death`: avg `17.305/17.304ms`, max `44.139/39.782ms`, over33 `4/2`
  - `contact`: avg `17.076/17.075ms`, max `40.459/40.216ms`, over33 `3/3`
  - `dualstresslong`: avg `17.530/17.530ms`, max `44.751/42.764ms`, over33 `9/6`
  - `secondgame`: avg `16.778/16.778ms`, max `46.982/47.397ms`, over33 `4/5`
- The second-game fix was first isolated in `logs/codex-practical-suite-window32-secondgame-jitpatchfix-20260607/20260607-021551`: helper patch logs appear at frame `870` and again at frame `4054` after auto restart, and the route reaches the second result without gameplay mismatch.
- A contaminated full-suite run, `logs/codex-practical-suite-rbwait1500-window32-postprune-20260607/20260607-012841`, showed much worse averages (`24-30ms`). It is not used as the current baseline because later single and full retakes under clean conditions passed with normal frame times. Keep using repeated retakes when a run shows global slowdown across every route.

Current conclusion:

- This candidate is now much closer to actual playability than the previous state: standard movement/item-touch, chaos input, death/respawn, player contact, long dual move/jump/dash, and result/restart into a second game all pass with practical FPS and no detected rollback integrity failure.
- The remaining rollback cost is still dominated by one-frame resimulation when a prediction miss occurs. Checkpoint save/restore is not the main blocker: recent runs show normal checkpoint saves around `1.6-2.4ms`, while one-frame correction can still spend roughly `19-37ms` depending on route and machine load.
- `luigistar` and `mariostarleft` are not current correctness failures; their latest run did not collect a star on either peer and therefore failed the event requirement symmetrically. They need better input coverage before they can prove dropped-star/star-pickup behavior.

Current blocker / caveat:

- Automated coverage still does not fully represent the user's manual reports around dropped stars, block break persistence, 8-coin item identity, and arbitrary complex contact. The six-route suite is a stronger baseline, not a final proof of comfortable human play.
- Manual play should use the low-latency tinycorepreimage/window32 path, but any new manual desync needs a trace route that captures the concrete event class instead of only checking final result.

Next actions:

- Add or repair deterministic routes for star pickup/drop/recover, block break state, and 8-coin item spawn identity. Treat those as gameplay-state coverage gaps, not as isolated one-off examples.
- Keep `tinycorepreimage-rbwait1500-window32` as the current candidate and reject `bundle8` unless a later correctness proof changes the input history semantics.
- Keep result/restart in the practical promotion matrix, but do not make the shorter default suite too slow unless needed; run the six-route matrix before claiming a playable milestone.
- Continue watching both average FPS and sudden frame drops (`maxFrameMs`, `over33ms`, consecutive slow frames). Average-only checks are insufficient.

## 2026-06-06 retained status - tinycorepreimage rollback profiling

At this retained checkpoint, the implementation direction had moved to rollback with lightweight checkpoints, not the earlier Plan-D actor/global/world snapshot path. The candidate under profiling was `tinycorepreimage`: frame-local Main RAM reverse preimages plus `DoRollbackTinyCoreSavestate` with `tinyFlags=0x241`.

Completed in the current pass:

- Added finer FPS-spike instrumentation to `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`. `NSMB BeforeHookPhaseSpike` now splits the pre-frame hook into `probeRestoreMs`, `jitPatchMs`, `rollbackMs`, packet-bridge setup, checkpoint, scratch, network, and wait buckets. `NSMB PacketBridgeScratchSpike` further splits scratch writes into network, throttle, remote wait, and write time. New spike lines are flushed immediately so forced process termination is less likely to lose the cause.
- Updated `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` to suppress CP15 `PU region` debug spam for tiny rollback backends, pass the same tinycorepreimage env as the manual wrapper (`tinyFlags=0x241`, JIT-reset skip, resim render skip), and enable `-RollbackResimulate` by default whenever `-Rollback` is requested. The previous split-smoke runs that omitted `-RollbackResimulate` could detect prediction mismatches without actually correcting them.
- Updated `scripts/run-nsmb-mvl-manual-local.ps1` so `-LowLatencyRollback -RollbackBackend tinycorepreimage` defaults to `InputMaxFrameLead=2`, rollback input wait `1500us`, checkpoint interval `1`, network pump `50us`, `tinyFlags=0x241`, JIT-reset skip, resim render skip, and PU debug suppression.
- Added `scripts/run-nsmb-mvl-practical-rollback-suite.ps1` to run practical rollback gates across stock-touch, chaos, death/respawn, player-contact, and long dual move/jump/dash routes. The suite records avg/max frame time, `over33ms`, consecutive slow frames, stall status, and game-state mismatch status in `summary.csv`.
- Added `GameStateTraceStartFrame` / `GameStateTraceEndFrame` pass-through to `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` so dense diagnostic traces can focus on gameplay windows instead of comparing pre-game uninitialized rows.
- Added an experimental `-RollbackSkipIntermediateResimCheckpoints` switch to the split smoke wrapper and `MELONDS_NSML_ROLLBACK_RESIM_SKIP_INTERMEDIATE_CHECKPOINTS` in the PoC. It is intentionally not enabled by default; forced-prediction tests below show that skipping intermediate re-saves can break correctness.
- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- PowerShell parse passed for the touched manual and split smoke scripts.

Verification:

- `logs/codex-tinycorepreimage-phaseprobe-start50-2300-20260606`: `RollbackInputWaitUs=2500`, `InputMaxFrameLead=1`, game-state comparison enabled, 2300F passed. This avoided frequent rollback but active FPS was only about `46.18/46.19`; high-frequency remote input wait timeouts were the dominant cost. One-frame corrections measured about `29.011ms` host and `26.250ms` client total.
- `logs/codex-tinycorepreimage-phaseprobe-wait0-lead2-2300-20260606`: `RollbackInputWaitUs=0`, `InputMaxFrameLead=2`, game-state comparison enabled, 2300F passed. Active FPS improved to about `55.17/55.21`, throttle was essentially gone, and checkpoint save averaged about `1.37ms`. At frame `2280`, host/client had `19/19` correction ops; restore averaged `3.751/3.952ms`, resimulated `RunFrame` averaged `13.630/15.182ms`, resim checkpoint re-save averaged `1.522/1.560ms`, and total correction averaged `27.236/30.722ms`.
- `logs/codex-tinycorepreimage-suppressedpu-wait0-lead2-2300-20260606`: same route after script-level PU debug suppression passed. `PU region` spam was removed. Active FPS was about `54.78/54.76`; max frame was `78.139/68.791ms`, still caused mostly by rollback/resimulation windows, with one client scratch spike from throttle (`27.558ms`).
- `logs/codex-tinycorepreimage-stocktouch-wait0-lead2-3200-20260606`: stock-touch route with host move/jump/dash and client stock-item touch passed 3200F with game-state comparison. Active FPS was about `49.91/49.87`; max frame was `103.635/71.572ms`, `over33ms=40/48`. This broadens correctness confidence, but it shows complex routes still have visible spikes even when remote waits are gone.
- `logs/codex-baseline-stocktouch-allowjit-delay0-lead2-1400-20260606`: JIT-enabled, rollback-disabled baseline passed the early stock-touch comparison region. Active average was `16.655/16.654ms`, max `25.749/25.249ms`, and `over33ms=0/0`. This confirmed that later 50ms averages were an artifact of testing with JIT disabled, not normal play.
- `logs/codex-tinycorepreimage-stocktouch-allowjit-resimdefault-wait0-lead2-3200-20260606`: after fixing split-smoke resim defaults, JIT-enabled tinycorepreimage passed 3200F with game-state comparison and `RollbackSettleFrames=30`. Active average was `17.028/17.020ms`, max `56.939/56.545ms`, `over33ms=19/16`; rollback spikes were in `rollbackMs`, while checkpoints stayed around `1.2-1.7ms`.
- `logs/codex-tinycorepreimage-chaos-allowjit-resimdefault-wait0-lead2-3200-20260606`: JIT-enabled chaos route passed 3200F with game-state comparison and `RollbackSettleFrames=30`. Active average was `17.000/16.994ms`, max `62.499/68.794ms`, `over33ms=24/20`. The largest after-start spikes were rollback/resimulation (`rollbackMs` up to about `45.4/51.8ms`), not checkpoint save or packet scratch.
- `logs/codex-tinycorepreimage-chaos-allowjit-resimdefault-wait0-lead2-4200-20260606`: the same chaos route passed 4200F. Active average was `17.050/17.056ms`, max `95.879/69.156ms`, `over33ms=39/35`. This keeps correctness confidence up but confirms that rare visible spikes can still reach about `96ms`.
- `logs/codex-tinycorepreimage-death-allowjit-resimdefault-ignoreinput-skipmove-wait0-lead2-3600-20260606`: Luigi death/respawn-oriented route passed 3600F with game-state comparison, death and moving-hazard progress checks, and input-field comparison ignored. Active average was `17.108/17.092ms`, max `50.349/42.040ms`, `over33ms=6/5`. The skipped movement probe is intentional because the client-side death script keeps Luigi mostly stationary.
- `logs/codex-tinycorepreimage-chaos-predprobe10-allowjit-resimdefault-wait0-lead2-2600-20260606`: diagnostic forced-prediction chaos route passed 2600F with `RollbackPredictionProbeModulo=10`, limit `80`. Active average was `18.260/18.286ms`, max `59.209/71.206ms`, `over33ms=82/81`. This is a stress test for rollback frequency, not a normal-play promotion gate.
- Rejected experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-skipresimckpt-allowjit-resimdefault-wait0-lead2-2600-20260606` skipped intermediate checkpoint re-saves during resim and failed at frame `1710` (`playerActor0X` mismatch). The intermediate checkpoints are therefore part of the correctness mechanism under repeated rollback, not just removable overhead.
- Rejected experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-resimdelay2-allowjit-resimdefault-wait0-lead2-2600-20260606` passed but worsened spikes: active max `83.378/82.271ms`, `over33ms=88/92`, with transient position mismatches that only settled later. Delaying correction coalesces work but makes the eventual correction heavier.
- Short same-frame wait experiment: `logs/codex-tinycorepreimage-chaos-predprobe10-rbwait500-allowjit-resimdefault-lead2-2600-20260606` passed the same forced-prediction route. It reduced spike counts to `over33ms=65/62` and capped max around `58.827/57.355ms`, but average rose to `18.458/18.460ms` due to `~1ms` remote waits on many frames. `rbwait250` was not reliable in the same stress and failed at frame `2130` with `playerActor0X` mismatch.
- `logs/codex-tinycorepreimage-resultrestart-allowjit-resimdefault-wait0-lead2-12000-20260606`: existing repeat-result input route did not reach result scene by 12000F, so the result/restart gate failed. Host/client final game-state rows matched exactly (`sceneCurrentSceneID=0x3`, `sceneNextSceneID=0x181`, lives `3/3`, battle stars `0/0`, deaths `0x17/0x5`), so this is currently an input-scenario coverage blocker rather than a tinycorepreimage desync. Performance stayed good for the long run: active average `16.748/16.744ms`, max `67.083/69.989ms`, `over33ms=29/27`.
- Practical-suite precheck rejected the old `coredelta-baseline` as a playability candidate despite its oracle value: `logs/codex-practical-suite-baseline-20260606` failed stock-touch, chaos, and death with max frame spikes around `151-324ms`.
- `tinycorepreimage-rbwait1000` improved over wait0 but was still marginal: `logs/codex-practical-suite-rbwait1000-extended-20260606` passed stock-touch and death, and `logs/codex-practical-suite-rbwait-chaos-20260606` passed chaos at `1000us`, but the added `dualstresslong` route later exposed transient input-state mismatches around rollback correction frames.
- Dense diagnostic run `logs/codex-rbwait1000-dualstress-trace1-1600b-20260606` showed those short mismatches were not missing Main RAM coverage: the mismatching rows had different `inputPlayer*Held` values for one frame and settled on the next frame. Example: at frame `1141`, host had `inputPlayer0Held=0x811` while client still had `0x810`; frame `1142` matched again.
- `tinycorepreimage-rbwait1500` is the current practical candidate. `logs/codex-practical-suite-rbwait1500-20260606/20260606-230223` passed stock-touch (`18.346/18.346ms`, max `59.030/58.929ms`), chaos (`17.997/17.996ms`, max `51.196/41.169ms`), death (`17.220/17.218ms`, max `40.202/40.141ms`), and contact (`17.039/17.039ms`, max `40.674/41.405ms`). One `dualstresslong` suite run had a non-rollback scratch/PacketBridge spike, while a direct repeat `logs/codex-rbwait1500-dualstresslong-7200-20260606` passed 7200F at `17.757/17.757ms`, max `64.419/63.123ms`.
- `tinycorepreimage-rbwait2000` reduced one dualstress run (`logs/codex-rbwait2000-dualstresslong-7200-20260606`, max `57.824/53.977ms`) but was worse on other play routes: `logs/codex-practical-suite-rbwait2000-20260606/20260606-231231` failed chaos/death with `runFrameMs` spikes around `100ms`. It is not the current default.
- Rejected input-lead experiments: `tinycorepreimage-rbwait1500-lead4` averaged around `28-29ms` and failed all five practical routes in `logs/codex-practical-suite-rbwait1500-lead4-20260606`; `tinycorepreimage-rbwait1500-lead8` passed stock-touch but made chaos/death worse and stalled contact/dualstresslong in `logs/codex-practical-suite-rbwait1500-lead8-20260606`. `InputMaxFrameLead=2` remains the practical setting.

Current conclusion:

- The stale 2026-06-02 conclusion that the comparison-enabled `tinycorepreimage` route stopped around frame `1453` no longer represents the current branch. Re-runs now complete 2300F under game-state comparison.
- JIT must be enabled for practical FPS assessment. JIT-disabled split-smoke runs can sit around `50ms` per active frame and should not be used as the normal-play performance baseline.
- Checkpoint storage is light enough for the current candidate: normal checkpoints are roughly `250KB` on the JIT allow route, save is usually about `1.2-1.7ms`, and restore is about `3.4-5.3ms` on these routes.
- The remaining visible frame drops are not dominated by checkpoint bytes. With JIT enabled, average frame time is usually near `17-18ms`, but rare frames can still spike from rollback resimulation, PacketBridge scratch throttle, or emulator `RunFrame`/JIT work.
- Input policy matters more than snapshot bytes now. `lead=2` is still required; widening to `lead4` or `lead8` made practical routes worse or stalled. A bounded same-frame remote-input wait around `1500us` is currently the best tradeoff found: it reduces one-frame speculative input mismatches without becoming input delay.

Current blocker / caveat:

- `tinycorepreimage` is promising but not promoted. With `rbwait1500/lead2`, stock-touch, chaos, death/respawn, contact, and a direct 7200F dual move/jump/dash route pass, but repeatability is not yet good enough for "comfortable real play" because rare PacketBridge throttle and `RunFrame` spikes still occur.
- Existing repeat-result input scripts no longer prove result/restart coverage under the current direct-MvL setup: by 12000F they stay synchronized but never enter result. A new deterministic result/restart route is needed before promoting the backend.
- Full write-barrier coverage is still not proven. The current page-comparison/preimage path is correctness-oriented; replacing it with write tracking should wait until more routes pass.

Next actions:

- Keep `rbwait1500, lead=2` as the current tinycorepreimage manual/practical-suite default and use frame-spike/stall/consecutive-slow gates, not only average FPS.
- Keep `-AllowJit` on for practical automated FPS tests, and keep `-RollbackResimulate` enabled for rollback correctness tests.
- Next implementation focus is PacketBridge/InputNetplay throttle behavior under rollback: avoid long scratch-throttle waits that destroy FPS, while keeping `InputMaxFrameLead=2` semantics stable enough to avoid runaway rollback.
- Run the same backend on block/item, actual result/restart, longer manual-like routes, and repeated practical-suite runs. Treat forced all-frame prediction-probe tests as diagnostic stress, not as a promotion gate.
- Rework the repeat-result input route so it reaches result scene deterministically; the current script proves long synchronized death/life churn but not restart coverage.
- Keep intermediate checkpoint re-saves during resim unless a different correctness proof replaces them; the skip experiment desynced under repeated rollback.
- If spikes remain too visible, prefer reducing long PacketBridge throttle waits and rollback frequency through a small bounded same-frame wait or smarter prediction over deleting checkpoint bytes. Current measurements show checkpoint save/restore is already light enough; resimulation, scratch throttle, and occasional `RunFrame` spikes are the practical blockers.

## 2026-06-02 retained diagnostic status - Plan-D actor/global snapshot path

Current best Plan-D-like direction is no longer a rollback backend. It is a small actor/global/world snapshot path that avoids full NDS rollback restore/resim:

- New wire packet: `WirePlayerState`, 168 bytes. The base actor fields are always present; player global fields are read/applied only when `MELONDS_NSML_PLAYER_STATE_GLOBALS=1`.
- New mostly host-authoritative wire packets: `WireWorldState`, 520 bytes, for the real Big Star actor, item-specific `Item(0x01F settings=0x00080002)` event, a bidirectional `NeutralItem(0x01F settings=0x00080000)` slot, and a separate `DroppedStarItem(0x01F settings=0x00090002)` slot; and `WireMovingHazardState`, 424 bytes, for up to four active moving-hazard actors. The neutral/dropped item slots are intentionally applied both directions because these short-lived item actors can originate from either local player, while Big Star and normal Item remain host-authoritative.
- New host-authoritative effect packet: `WireWorldEffectState`, 760 bytes, for up to four active fixed Effect slots from `0x021C3268 + 0x1D4*i`. This targets visible dropped-star/red-number style effects without scanning all Main RAM.
- New host-authoritative generic actor packet: `WireWorldActorSnapshotState`, 1688 bytes, for up to sixteen non-player, non-manager process-list actors. It carries object ID plus the same compact transform/velocity/common actor fields as the world actor state, but the current receiver intentionally applies transform/velocity only. This is the current root-level Plan-D refinement: it broadens coverage beyond hand-picked enemy/star examples without cloning all Main RAM or blindly spawning every host-only actor.
- `scripts/run-nsmb-mvl-manual-local.ps1 -PlanDActorSnapshot` now defaults to input delay `0`, max frame lead `4`, network pump `50us`, and the generic actor snapshot. Lead `4` replaced the earlier lead `8` default after manual logs showed remote-input-wait spikes during freeze reports.
- Lightweight gameplay liveness is available through `MELONDS_NSML_GAMEPLAY_HEARTBEAT_INTERVAL` / `-GameplayHeartbeatInterval`. It logs low-frequency player transforms and object lifecycle counts so the analyzer can detect in-game plateaus even when the emulator frame heartbeat keeps advancing.
- Gameplay heartbeat now also emits compact active object IDs/settings (`activeIds=`), and the analyzer reports both raw host-only/client-only active object sets and a gameplay-significant set that ignores known local-role objects such as `StageFX(0x012)` and `MvsLObject267(0x10B)`. This is the current main diagnostic for complex manual desyncs where the game keeps running but actors are missing, duplicated, or deactivated differently.
- Env/script switches:
  - `MELONDS_NSML_PLAYER_STATE_SYNC=1`
  - `MELONDS_NSML_PLAYER_STATE_APPLY=1`
  - `MELONDS_NSML_PLAYER_STATE_GLOBALS=1` for the experimental actor+global route
  - `MELONDS_NSML_PLAYER_STATE_SYNC_INTERVAL`
  - `MELONDS_NSML_PLAYER_STATE_MAX_PREDICT_FRAMES`
  - split/lan scripts expose `-PlayerStateSync`, `-PlayerStateApply`, `-PlayerStateGlobals`, `-PlayerStateSyncInterval`, and `-PlayerStateMaxPredictFrames`.
  - `MELONDS_NSML_WORLD_STATE_SYNC=1`, `MELONDS_NSML_WORLD_STATE_APPLY=1`, `MELONDS_NSML_WORLD_STATE_SYNC_INTERVAL`, and `MELONDS_NSML_WORLD_STATE_MAX_PREDICT_FRAMES` enable the standard Big Star world snapshot.
  - `MELONDS_NSML_WORLD_STATE_SPAWN_ITEM=1` enables the item-specific client spawn experiment. `-PlanDActorSnapshot` now enables it by default.
  - `MELONDS_NSML_WORLD_STATE_APPLY_MOVING_HAZARD=1` enables compact multi-instance moving-hazard correction. `-PlanDActorSnapshot` now enables it with a 30-frame process-list rescan interval.
  - `MELONDS_NSML_WORLD_STATE_APPLY_EFFECTS=1` enables the compact Effect slot snapshot. `-PlanDActorSnapshot` now enables it by default; pass `-WorldStateSkipEffects` to the manual wrapper to disable only this new part.
  - `MELONDS_NSML_WORLD_STATE_APPLY_ACTOR_SNAPSHOT=1` enables the generic process-list actor snapshot. `-PlanDActorSnapshot` now enables it by default.
  - `MELONDS_NSML_WORLD_STATE_TRACE_ACTOR_INTERNALS=1` and `MELONDS_NSML_WORLD_STATE_TRACE_EFFECTS=1` are diagnostic-only tracing modes for ROM/memory analysis. They are not part of normal manual play.
- Remote world, moving-hazard, actor-snapshot, and effect samples now reject older packet frames instead of allowing late unreliable packets to overwrite a newer sample. This matters for short-lived item actors where a `Found=0` packet from an older frame can otherwise erase the useful `Found=1` sample before apply.
- After-frame artifact correction now syncs first and applies only fresh same-frame moving-hazard/player samples. This is intentionally narrower than the rejected always-apply after-hook path: stale player samples are ignored after the frame, while the normal before-frame apply still uses the latest eligible sample for gameplay.
- The packet carries actor transform, velocity, action/subaction/physics flags, damage cooldown, transition/collision/environment flags, and compact runtime byte flags.
- The optional global section currently carries per-player life/death/pipe/star counters. It is applied as event-only state, not as a full global overwrite.
- During player actor transition steps other than `1`, the actor+global route skips transform/full runtime writes and applies only minimal visible/defeated bytes. This avoids fighting the game's pipe/death transition code.
- The receiver applies the latest remote player actor snapshot before frame execution and again before game-state trace/sync.
- Existing fixed-size wire packets were also unblocked from the input-bundle branch so `WireNSMLPacket`, `WireGameState`, `WirePlayerState`, `WireWorldState`, and `WireMovingHazardState` can reach their exact handlers.
- Generated direct-MvL ROMs now patch the stage-object activation scan to use player `0` on both peers. The local-player-1 scan could skip early off-camera actors such as the first Goomba, which matches the user-reported "enemy appears on only one side" class of desync better than a runtime clone-all approach.

Verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- Current generic actor snapshot validation:
  - Historical manual freeze logs are no longer misclassified as OK: `logs/nsmb-mvl-manual-local-20260602-175043` and `logs/nsmb-mvl-manual-local-20260602-175135` now analyze as `failed` because the wrapper reported missing host/client frame limits.
  - `logs/codex-pland-actor-snapshot-lead4-stress-3600-20260602`: no-trace move/dash/jump stress passed with actor snapshot and lead `4`; host/client active avg `16.782/16.781ms`, max `29.305/27.061ms`, and `over33ms=0/0`.
  - `logs/codex-pland-actor-snapshot-heartbeat-lead4-stress-4200-20260602`: gameplay-heartbeat stress passed; analyzer status `ok`, gameplay plateau `1/1`, host/client active avg `17.048/17.047ms`, max `39.255/37.939ms`, max consecutive slow frames `1/1`.
  - `logs/codex-pland-actor-snapshot-trace-lead4-stress-1800-20260602`: trace confirmed client-side generic actor application, typically `2-5` actors per sampled row, while staying below `33ms`.
  - `logs/codex-pland-actor-snapshot-lifecycle-lead4-stress-3000-20260602`: lifecycle gate passed with `host=18 client=18 shared=18` and no unexpected actor-count differences.
  - Rejected experiment: generic application of `StateType`, `Flags`, and common motion fields was too aggressive. `logs/codex-pland-actor-common16-heartbeat-lead4-stress-4200-20260602` stalled around frame `2040`, so the generic actor path remains transform-only until object-specific fields are classified.
  - `logs/codex-pland-actor16-heartbeat-lead4-stress-4200-20260602`: 16-slot transform-only actor snapshot passed; analyzer status `ok`, gameplay plateau `1/1`, host/client active avg `17.724/17.724ms`, max `36.728/34.511ms`, and `over33ms=3/3`. This improves coverage but is measurably heavier than the previous 12-slot run, so future work should consider variable-length packets or tighter candidate selection.
  - `logs/codex-pland-actor16-heartbeatids-lead4-stress-1800-20260602`: heartbeat `activeIds=` output and object-diff analyzer path passed a short validation run; host/client active avg `16.667/16.665ms`, max `26.563/27.330ms`, `over33ms=0/0`, and heartbeat object diff stayed `0`.
  - `MvsLObject267(0x10B)` is now excluded from the generic world actor snapshot candidate set. A 6200F result/restart probe, `logs/codex-pland-actor16-heartbeatids-result-restart-6200-20260602`, showed raw heartbeat differences consisting only of local `StageFX(0x012)` settings and transient `0x10B`; significant diff stayed `0`. The run did not reach the second-game gate within 6200F, so it is a diagnostic route result rather than a promotion gate.
  - `logs/codex-pland-actor16-exclude10b-heartbeatids-stage0-2600-20260602`: movement-enabled validation passed after excluding `0x10B`; analyzer status `ok`, host/client active avg `16.849/16.848ms`, max `43.355/41.246ms`, max consecutive slow frames `1/1`. The new significant object-diff summary exposed a recurring `053:00000000` host-only moving-hazard lifecycle gap across `3/14` shared heartbeat rows (`firstSignificantActiveIdDiffFrame=2040`). This is the next concrete non-local actor drift to investigate.
  - Heartbeat now also prints `hazards=` with up to four moving-hazard GUID/position/velocity/state/flags tuples. Re-running the same 2600F route as `logs/codex-pland-hazarddetails-heartbeat-stage0-2600-20260602` passed with object diff `0`, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `17.190/17.192ms`, max `29.897/31.898ms`, and `over33ms=0/0`. The earlier `053` gap is therefore not a stable every-run divergence on this route, but future manual/long-run logs will now identify the missing hazard GUID and coordinates when it occurs.
  - Added asymmetric chaos input routes:
    - `tests/nsmb_us_direct_mvl_chaos_host.inputs`
    - `tests/nsmb_us_direct_mvl_chaos_client.inputs`
    These mix uneven left/right periods, jump, Y/B run buttons, short stops, and touch events to better approximate complex manual input.
  - First chaos stage `0` run, `logs/codex-pland-chaos-stage0-7200-20260602`, passed but exposed a client-only `Item(0x01F settings=0x00080000)` across three heartbeat samples. Adding the bidirectional `NeutralItem` slot alone was insufficient because older unreliable world packets could overwrite the newer `Found=1` sample.
  - After rejecting older world/effect/actor/hazard packets and keeping the bidirectional `NeutralItem` slot, the same chaos stage `0` route passed as `logs/codex-pland-chaos-neutralitem-newest-stage0-7200-20260602`: analyzer status `ok`, `01F:00080000` occurrences `0`, active avg `17.244/17.244ms`, max `39.852/46.189ms`, max consecutive slow frames `1/1`, Big Star drift `0/0`, moving-hazard max drift `2048/0`. Remaining significant object diff was a single `053:00000000` activation-boundary row at frame `2160`.
  - Chaos stage `1` also passed after the same fix: `logs/codex-pland-chaos-neutralitem-newest-stage1-7200-20260602`, analyzer status `ok`, active avg `17.151/17.151ms`, max `42.170/44.802ms`, max consecutive slow frames `1/1`, Big Star drift `0/0`, moving-hazard max drift `2048/0`. `01F` actors appeared with matching settings on both peers; no item-only significant diff remained.
  - Chaos stage `2` passed as `logs/codex-pland-chaos-neutralitem-newest-stage2-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `17.775/17.773ms`, max `45.544/45.773ms`, max consecutive slow frames `1/2`, Big Star drift `0/0`, and no tracked moving hazard on this route. Stage `2` is a bit heavier (`over33ms=14/17`) but still did not show a rollback-style stutter or persistent actor drift.
  - Chaos stage `3` passed as `logs/codex-pland-chaos-neutralitem-newest-stage3-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `17.150/17.150ms`, max `31.742/30.504ms`, max consecutive slow frames `0/0`, Big Star drift `0/0`, and `over33ms=0/0`.
  - Chaos stage `4` passed as `logs/codex-pland-chaos-neutralitem-newest-stage4-7200-20260602`: analyzer status `ok`, significant object diff `0`, active avg `18.719/18.719ms`, max `45.365/49.665ms`, max consecutive slow frames `2/2`, Big Star drift `0/0`, and no tracked moving hazard. This stage is the heaviest chaos route so far (`over33ms=17/27`) and should stay in the performance matrix.
  - A stricter 8400F chaos rerun exposed a real automatic detection failure before the latest fix: `logs/codex-pland-chaos-seed13579-stage0-8400-20260602` failed strict comparison at frame `930` on `movingHazardX`, and `logs/codex-pland-sync1-worldpred2-playerpred1-driftgate-stage0-8400-20260602` still failed the moving-hazard drift gate at frame `2130` (`dx=2048`) before the fresh same-frame hazard apply.
  - Fresh same-frame moving-hazard apply fixed the hazard gate for generated stages `0` and `1`: `logs/codex-pland-freshhazard-sync1-driftgate-stage0-8400-20260602` and `logs/codex-pland-freshhazard-sync1-driftgate-stage1-8400-20260602` both passed with Big Star drift `0/0`, moving-hazard drift `0/0`, and no significant active-object diff. Stage `0` active avg was `16.951ms`; stage `1` was `18.261ms`.
  - Fresh-only after-frame player apply then removed the remaining one-sample player drift on complex input. `logs/codex-pland-freshplayer-stage2-8400-20260602`, `logs/codex-pland-freshplayer-stage3-8400-20260602`, and `logs/codex-pland-freshplayer-stage4-8400-20260602` passed input-delay-`0` 8400F chaos validation with Big Star drift `0/0`, significant active-object diff `0`, and player drift `96/0`, `0/0`, and `0/0`. The stricter final fresh-only hazard/player reruns, `logs/codex-pland-freshonly-stage0-8400-20260602` and `logs/codex-pland-freshonly-stage1-8400-20260602`, also passed with Big Star drift `0/0`, moving-hazard drift `0/0`, significant active-object diff `0`, and player drift `2528/2432` and `0/0`.
  - The strict trace-enabled `freshonly` stage `0` run produced a `perf-fail` caused by the observer, not rollback or actor apply: `TraceGameState` took `215.729ms` at frame `4620`. Re-running the same stage/input without full CSV tracing as `logs/codex-pland-freshonly-stage0-notrace-8400-20260602` passed with analyzer status `ok`, active avg `17.542ms`, max `38.749/39.489ms`, max consecutive slow frames `1/1`, `over33ms=3/4`, and significant active-object diff `0`. Keep using targeted trace runs for drift and no-trace runs for FPS/spike promotion.
  - Latest available manual log before the active-ID heartbeat change, `logs/nsmb-mvl-manual-local-20260602-181856`, is still useful as a drift/stall example: analyzer reports `status=stalled`, host/client heartbeat frame `21960`, and gameplay heartbeat object divergence across `39/177` shared frames with `maxActiveDelta=3`, first at frame `1320` (`host=12/12/0/0 client=11/11/0/0`). New manual logs should now identify the concrete host-only/client-only active IDs at the first divergence.
  - Strict full game-state CSV equality is still too strict for this non-deterministic actor-correction path. `logs/codex-pland-actor-snapshot-gamestate-lead4-stress-2400-20260602` failed on a small moving-hazard X sample delta (`0x800`) at frame `960`; this should be treated as a comparison-timing caveat, not as a full freeze reproduction.
- First naive implementation used `ReadGameStateSample()` every frame and was too heavy: `logs/codex-playerstate-norollback-lead8-stress-seq-1600-20260602` ran only about `37.6fps` active.
- After replacing that with `FindPlayerActors()` plus direct player offset reads:
  - `logs/codex-playerstate-fastsend-lead8-stress-1600-20260602`: 1600F passed, host/client active FPS about `57.9`, max frame about `36ms`, `over33ms=2`.
  - `logs/codex-playerstate-fastsend-lead8-stress-2400-20260602`: 2400F passed, no rollback/resim, host/client active FPS about `51.6`, max frame `31.979/34.490ms`, `over33ms=0/2`, and both player actor X values moved.
  - `logs/codex-playerstate-interval2-predict1-lead8-stress-2400-20260602`: 2400F passed with send interval 2 and prediction 1, host/client active FPS about `54.5`, max frame `33.074/33.406ms`, `over33ms=0/1`.
- Added a dedicated actor-snapshot gate for `-SkipGameStateComparison` runs. It checks both directions of remote actor movement, optional host/client coordinate drift, active max frame, over33ms count, and consecutive slow frames.
- `scripts/analyze-nsmb-mvl-rollback-log.ps1` now parses hex trace fields correctly and only treats actor plateaus as freeze suspects while input is being held. It classifies the user-reported Plan-D-like manual freeze `logs/nsmb-mvl-manual-local-20260601-212956` as `abort/perf-fail`, and the baseline `logs/nsmb-mvl-manual-local-20260601-213213` as `ok`.
- Player actor base/GUID caching was added for the actor snapshot send/apply path so it does not scan all Main RAM every frame unless the cached actor becomes invalid.
- Cached actor snapshot verification:
  - `logs/codex-playerstate-cache-drift-gate-interval2-predict1-2400-20260602`: 2400F passed with movement/drift gate; host/client active FPS about `59.5`.
  - `logs/codex-playerstate-cache-drift-gate-interval2-predict1-4200-20260602`: 4200F passed with movement/drift gate; host/client active FPS about `59.5`, max frame `33.224/32.548ms`, `over33ms=0/0`, max drift X/Y `12352/16768`.
  - `logs/codex-playerstate-cache-luigi-death-3600-20260602`: Luigi death/respawn-style probe passed with player death and pipe visibility checks; analyzer status `ok`, active FPS about `59.6`, max frame `40.399/45.362ms`, `over33ms=4/4`, max consecutive slow frames `1/1`.
- Actor+global snapshot verification:
  - Rejected earlier always-apply global snapshots: `logs/codex-playerglobal-cache-drift-gate-interval2-predict1-4200-20260602` and `logs/codex-playerglobal-directwrite-drift-gate-interval2-predict1-4200-20260602` ran around `48fps` with many `over33ms` frames.
  - Event-only globals with signed drift fixed passed normal movement: `logs/codex-playerglobal-events-optional-on-drift-gate-interval2-predict1-4200-20260602`, active avg about `16.79ms`, max `43.724/46.892ms`, `over33ms=4/5`.
  - A death/pipe route without transition-step minimal apply reproduced a bad stutter: `logs/codex-playerglobal-events-luigi-death-3720-20260602` hit `353.752/354.027ms` max frames around frame `2794` and is now classified as `perf-fail`.
  - With transition-step minimal apply, the same death/pipe route passed: `logs/codex-playerglobal-transition-step-min-luigi-death-3720-20260602`, active avg about `17.10ms`, max `39.388/38.823ms`, `over33ms=9/9`, max consecutive slow frame `1/1`.
  - With transition-step minimal apply, normal movement still passed: `logs/codex-playerglobal-transition-step-min-stress-4200-20260602`, active avg about `17.05ms`, max `44.636/42.051ms`, movement/drift gate passed.
- Result route using `RequireResultScene` passed with actor+global snapshot: `logs/codex-playerglobal-transition-step-min-result-9000-20260602`, active avg about `16.77ms`, max `46.907/45.751ms`, `over33ms=11/11`. The input file name suggested a star route, but trace review showed the result came from repeated fall deaths while star counter fields remained `0/0`. `RequireStarPickup` is not a valid assertion for this route.
- The split-input wrapper now exposes `-RequireSecondMvlGame`, `-RequireMvlGameCount`, and `-RequireMvlGameStages`. A result/restart route using the actor+global snapshot passed a second-game gate: `logs/codex-playerglobal-transition-step-min-secondgame-gate-9000-20260602`.
- Added `tests/nsmb_us_direct_mvl_star_collect_second_game_stress.inputs`: after result/restart it drives both players with simultaneous move, dash, jump, and direction reversals. `logs/codex-playerglobal-sustained-drift-gate-secondgame-stress-10000-20260602` passed 10000F, reached the second MvL game, moved both remote actors for 126 sampled rows, and passed movement/drift validation. Active avg was about `17.40ms`, max `56.405/54.940ms`, `over33ms=44/47`, and max consecutive slow frames `2/1`.
- The actor drift gate ignores dead/transition rows because transition-step minimal apply intentionally does not overwrite transform there. It also supports `-ActorSnapshotMaxConsecutiveDriftRows`; the 10000F route allows one transient sampled row but still fails sustained drift. The passing route observed max drift X/Y `24192/17024` and no sustained over-limit row.
- `scripts/run-nsmb-mvl-manual-local.ps1 -PlanDActorSnapshot -AllowJit` is the lightweight manual-play command. It enables input delay `0`, frame lead `4`, network pump `50us`, player actor/global, Big Star, item-specific client spawn, compact multi-instance moving-hazard/effect snapshots, generic process-list actor snapshots, and gameplay heartbeat. Plan-D now defaults to world snapshot interval `1`, world prediction `2`, player prediction `1`, a 30-frame process-list rescan interval, fresh same-frame after-hook moving-hazard apply, and fresh-only after-hook player apply. It intentionally leaves detailed game-state/perf breakdown traces off unless explicitly requested. The old lead-`8` launch `logs/codex-manual-pland-runtimelean-launch-1800-20260602` remains a historical propagation check, not the current manual default.
- Global writes are now applied once per newly received player-state packet while actor transform prediction still runs each frame. This avoids repeatedly writing the same delayed lives/deaths/transition snapshot into the game between packet arrivals.
- Post-change normal movement validation passed: `logs/codex-playerglobal-globals-once-normal-stress-4200-20260602`, active avg `17.608/17.605ms`, max `43.060/46.158ms`, max consecutive slow frames `1/1`, and movement/drift validation passed with max drift X/Y `12352/16768`.
- Post-change visible-window manual launch passed: `logs/codex-manual-pland-globals-once-launch-1800-20260602`, active avg `17.372/17.370ms`, max `28.831/31.461ms`, `over33ms=0/0`.
- Added a host-authoritative Big Star world snapshot and `-RequireWorldSnapshotSync` gate. The gate fails on Big Star actor drift and reports moving-hazard drift separately.
- Standard Big Star-only world snapshot validation:
  - `logs/codex-worldstate-star-gate-normal-stress-rerun-4200-20260602`: normal move/dash/jump stress passed, Big Star drift `0/0`, active avg `17.083/17.083ms`, max `43.936/40.535ms`, `over33ms=6/7`.
  - `logs/codex-worldstate-star-gate-luigi-death-3720-20260602`: death/respawn route passed, Big Star drift `0/0`, active avg `17.192/17.193ms`, max `41.988/42.378ms`.
  - `logs/codex-worldstate-star-gate-result-restart-6200-20260602`: result/restart into a second MvL game passed, Big Star drift `0/0`, active avg `17.402/17.403ms`, max `67.511/50.303ms`.
- Rejected naive moving-hazard correction as a standard-path feature:
  - `logs/codex-worldstate-hazard-only-normal-stress-4200-20260602` showed that transform-only apply was insufficient.
  - Adding hazard physics fields and active GUID cache refresh fixed one respawn boundary, but `logs/codex-worldstate-newest-rescan-combined-normal-stress-4200-20260602` showed that selecting one newest `0x0053` actor can still target the wrong simultaneous instance.
  - A low-frequency diagnostic scan showed that active hazard count is normally `1`, briefly `2` during replacement, and GUIDs can acquire a stable host/client offset even when motion matches.
- Added compact multi-instance moving-hazard correction:
  - Host sends up to four active `0x0053 settings=0` actors ordered by creation GUID. Client only applies when host/client active counts match.
  - Client now keeps a persistent `remote GUID -> local GUID` map across frames. When a lifecycle boundary introduces a new remote actor, it pairs the remaining local actor by nearest current position. This preserves identity across actor crossings and no longer assumes that host/client creation order stays identical.
  - Active-list refresh now traverses the NSMB process lists instead of scanning all 4MB Main RAM. The initial full-RAM scanner raised normal stress average frame time to `18.26ms`; process-list traversal reduced it to `16.93ms`.
  - `logs/codex-worldhazards-processlist-normal-stress-4200-20260602`: normal stress passed, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.928/16.929ms`, max `39.932/39.572ms`.
  - `logs/codex-worldhazards-processlist-luigi-death-3720-20260602`: death/respawn passed, moving-hazard max drift `2048/3072`, active avg `16.898/16.899ms`, max `42.804/48.631ms`.
  - `logs/codex-worldhazards-processlist-result-restart-6200-20260602`: result/restart into a second MvL game passed, moving-hazard max drift `4096/0`, active avg `16.908/16.908ms`, max `49.980/49.207ms`.
  - `logs/codex-pland-hazard-guidmap-stage0-stress-3100-20260602`: the new GUID map exercised lifecycle churn and stable host/client GUID offsets such as `43/44`, `44/45`, and `48/49`; moving-hazard max drift stayed `2048/0`, with max consecutive drift rows `0`.
  - `logs/codex-pland-hazard-guidmap-result-restart-6200-20260602`: result/restart passed after the GUID-map change, with Big Star drift `0/0`, moving-hazard max drift `4096/0`, manager/global agreement across `177` rows, active avg `16.829/16.829ms`, max `52.843/51.141ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-pland-hazard-guidmap-luigi-death-3720-20260602`: death/pipe-respawn passed with hazard-progress and pipe-visibility gates enabled. Active-count lifecycle boundaries converged without sustained drift; moving-hazard max drift was `4096/0`, active avg `16.746/16.747ms`, max `39.574/39.730ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-manual-pland-worldhazard-launch-1800-20260602`: visible-window `-PlanDActorSnapshot` launch completed with active avg `17.462/17.461ms`, max `31.970/30.764ms`, and `over33ms=0/0`.
- Added `-RequireMvlManagerGlobalSync`, an observation-only gate for selected MvL manager/global/stage-scene fields. It intentionally does not add runtime writes when the trace already agrees:
  - `logs/codex-mvlmanager-gate-result-restart-6200-20260602`: result/restart passed with `177` compared rows, Big Star drift `0/0`, moving-hazard max drift `4096/0`, active avg `16.800/16.801ms`, max `62.290/63.555ms`.
  - `logs/codex-mvlmanager-gate-repeat-result-threegame-12000-20260602`: three-game repeated result/restart passed with `371` compared rows, Big Star drift `0/0`, moving-hazard max drift `4096/0`, active avg `16.773/16.773ms`, max `47.360/45.596ms`.
- Real Big Star acquisition is now covered by a deterministic probe. `tests/nsmb_us_direct_mvl_luigi_star_right.inputs` depends on initial star placement, so the reliable condition is `-MvlMatchSeed 0x19FE5603`:
  - `logs/codex-pland-luigi-star-right-seed19fe5603-2600-20260602`: Luigi collected the real star with Plan-D snapshots enabled, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.867/16.869ms`, max `39.714/41.264ms`.
  - `logs/codex-pland-luigi-star-right-settle-seed19fe5603-3200-20260602`: the post-collection star counter and respawned star converged on both sides. This rejects the earlier apparent failure from a run whose random initial star was at `0x3c0000` instead of the probe-compatible `0x90000`.
  - `logs/codex-pland-hazard-guidmap-luigi-star-settle-3200-20260602`: the current moving-hazard GUID-map path also passed real Luigi star pickup and settle. Big Star drift stayed `0/0`, moving-hazard max drift was `2048/0`, manager/global agreement covered `77` rows, active avg was `17.346/17.346ms`, max `54.719/53.511ms`, and max consecutive slow frames `1/1`.
- The split wrapper now forwards `-MvlStage`, `-MvlSceneSettings`, `-MvlBigStars`, `-MvlLives`, `-MvlCourseMode`, and `-GenerateMvlConfiguredRoms`. Configured-ROM generation uses the unpatched default source `roms/nsmb-us.nds` through `-GenerateMvlSourceRom`.
- Plan-D stage variation matrix passed for all five courses with generated ROMs, move/dash/jump stress, player actor movement gate, Big Star drift gate, moving-hazard drift gate, manager/global gate, and frame-spike gate:
  - `logs/codex-pland-world-stage0-generated-stress-2400-20260602` through `logs/codex-pland-world-stage4-generated-stress-2400-20260602`.
  - Host/client active averages ranged from `16.918ms` to `17.676ms`; maxima ranged from `35.407ms` to `63.668ms`.
  - Courses `0` and `1` exercised the tracked `0x0053` hazard with max drift `2048/0`. Courses `2`, `3`, and `4` had no tracked `0x0053` hazard in the sampled route. All courses kept Big Star drift at `0/0`.
- The course `1` matrix trace exposed a persistent coin-global gap: host had `player0Coins=1 / vsCoinCount=1` while client stayed at `0 / 0`. `WirePlayerState` already carried `Coins`, but the event-only apply path did not use it. Remote-player coin writes are now applied only when changed; `vsCoinCount` is the same `0x0208B37C` address as `player0Coins`.
  - `-RequireMvlManagerGlobalSync` now includes `player0Coins`, `player1Coins`, and `vsCoinCount`, for `34` observed fields total.
  - `logs/codex-pland-coinsync-stage1-generated-stress-2400-20260602`: the reproducing course `1` stress route passed with fully converged coin fields, Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `17.172/17.171ms`, max `42.176/42.178ms`.
  - `logs/codex-pland-coinsync-luigi-star-right-settle-3200-20260602`: real Luigi star acquisition still passed with the 34-field gate.
  - `logs/codex-pland-coinsync-result-restart-rerun-6200-20260602`: result/restart still passed with the 34-field gate, active avg `17.488/17.488ms`, max `57.761/52.862ms`.
- Camera/layout broad-diff classification is complete for the currently suspicious fields. `0x02092B4` tracks a coarse/quantized camera coordinate, `0x020CAF20` tracks local camera X at finer resolution, and `0x020CAF40` is a stage bound or brief local layout-transition value. They differ because each process has a local viewpoint and must not be synchronized.
- Added diagnostic-only actor lifecycle tracing:
  - `MELONDS_NSML_WORLD_STATE_TRACE_OBJECT_LIFECYCLES=1` prints GUID, object ID, settings, lifecycle state, type, flags, vtable, base, and `PosX/Y/Z` for actors reached through the ROM-analyzed NSMB process lists.
  - The LAN and split smoke scripts expose `-WorldStateTraceObjectLifecycles`, interval, start-frame, and end-frame parameters.
  - The original diagnostic used a full Main RAM scan and was intentionally not enabled by `-PlanDActorSnapshot`; dense 2-frame scans raised active averages to about `18-19ms`. It now reuses the process-list walker, so targeted lifecycle observation no longer requires a 4MB sweep.
  - `scripts/analyze-nsmb-mvl-object-lifecycle-diff.ps1` compares host/client lifecycle logs by sampled frame and aggregates actor-count differences by `objectID/settings`. It supports allowlisted local-only actors and a fail mode.
  - The split wrapper exposes `-RequireNoUnexpectedWorldLifecycleDiff`. With lifecycle tracing enabled, it allows local-only `StageFX(0x012)` and the classified `0x0F0 settings=0x01080002` transient, then fails on any other actor-count difference, including a single sampled row.
  - `logs/codex-pland-processlist-lifecycle-stage1-2400-20260602`: a 10-frame lifecycle trace window on generated course `1` passed Item spawn and all world/global gates. Active avg was `17.635/17.640ms`, max `45.892/42.345ms`, and max consecutive slow frames `1/1`. Same-frame `objectID/settings` comparison found no new persistent world gap after Item repair: remaining differences were the local-role `0x012` settings variant and one sampled row of the already-known host-only `0x0F0 settings=0x01080002` transient.
  - `logs/codex-pland-processlist-lifecycle-result-restart-6200-20260602`: a 30-frame lifecycle trace across result/restart passed the second-game, Big Star, moving-hazard, and 34-field manager/global gates. Active avg was `17.280/17.279ms`, max `49.521/47.596ms`, and max consecutive slow frames `1/2`. Aggregated differences were only local-role `StageFX(0x012)` variants: the regular `settings=0x00008000/0x00008010` split and a host-only `settings=0x00000005` effect while Luigi was dead before result transition. ROM resource symbols map logged vtable `0x02127840` to `_ZTV7StageFX`, so these visual actors stay local.
  - `logs/codex-pland-processlist-lifecycle-threegame-12000-20260602`: a 60-frame lifecycle trace passed three MvL games, two result/restart boundaries, Big Star drift `0/0`, moving-hazard max drift `4096/0` with no sustained row, and the 34-field manager/global gate across `371` rows. Active avg was `17.424/17.424ms`, max `48.162/49.904ms`, and max consecutive slow frames `1/2`. Aggregated differences were still only `StageFX(0x012)`: the local-role split and winner/loser result effects `settings=0x00000005/0x00000016` sharing vtable `0x02127840`.
  - `logs/codex-pland-processlist-lifecycle-luigi-star-3200-20260602`: a 30-frame lifecycle trace passed deterministic real Luigi Big Star pickup, Big Star drift `0/0`, moving-hazard max drift `2048/0` with no sustained row, and the 34-field manager/global gate across `77` rows. Active avg was `17.214/17.216ms`, max `52.192/51.038ms`, and max consecutive slow frames `2/1`. The only aggregated differences were the regular local-role `StageFX(0x012 settings=0x00008000/0x00008010)` pair.
  - `logs/codex-pland-processlist-lifecycle-luigi-death-3720-20260602`: a 30-frame lifecycle trace passed Luigi death, pipe-respawn visibility, moving-hazard progress, Big Star drift `0/0`, and the 34-field manager/global gate across `95` rows. Moving-hazard max drift briefly reached `2048/8192` but had no sustained row. The Y drift was one sampled row at frame `2760` on the same host/client GUID `0x1B`, then converged, so it was not a GUID-map mismatch. Active avg was `17.302/17.303ms`, max `46.113/56.172ms`, and max consecutive slow frames `1/1`. The only aggregated differences were the regular local-role `StageFX(0x012 settings=0x00008000/0x00008010)` pair.
  - `logs/codex-pland-processlist-lifecycle-stage0-2400-20260602` and stages `2-4`: a 30-frame lifecycle matrix passed player movement, Big Star, hazard, and manager/global gates. Stages `2-4` exposed no non-`StageFX` actor-count differences. Course `0` exposed a single sampled row where client had two active `Item(0x01F settings=0x00080002)` actors while host had one, leading to the natural-spawn grace fix below.
- Dense lifecycle diagnosis found a concrete host-only world actor gap on generated course `1` stress:
  - `logs/codex-pland-objectlifecycle-pos-stage1-stress-2120-20260602`: host creates active `Item` actor `0x01F settings=0x00080002` around frame `2046` at fixed-point world position `X=0x00048000`, with associated `0x0F0 settings=0x01080002`; client creates neither.
  - `logs/codex-stocktouch-stress-objectlifecycle-pos-2120-20260602`: forcing both stock inventories to `2` and adding a client lower-screen stock touch still leaves the same `0x01F/0x0F0` creation only on host. This rejects the lower-screen stock-animation interpretation and classifies the gap as a host-side world interaction.
  - ROM symbols provide `Actor::spawnActor` at `0x020A0B64`, and the frontend already has an ARM trampoline pattern. The implementation now uses a narrow `Item` event packet plus client spawn, not a generic actor clone.
  - Same-frame lifecycle re-aggregation found no additional persistent active world gap. The remaining associated `0x0F0 settings=0x01080002` actor is active for about `14` frames before becoming dead, while the `Item(0x01F)` remains active for about `30` frames. Keep `0x0F0` unsynchronized unless a concrete visible or gameplay mismatch appears.
- Added item-specific lightweight world replication:
  - The host sends the newest active `Item(0x01F settings=0x00080002)` from the process list. The client calls `Actor::spawnActor` once through the ARM trampoline when that host item is missing locally, then applies the host transform while the item remains active.
  - Normal runtime does not enable the diagnostic lifecycle trace. The split wrapper exposes `-WorldStateSpawnItem` and the lightweight `-RequireWorldItemSpawn` gate.
  - `logs/codex-pland-itemspawn-gate-stage1-normal-2400-20260602`: course `1` reproduced and repaired the gap; client spawn/active gates, player movement, Big Star, hazard, and 34-field manager/global gates passed. Active avg `17.229/17.227ms`, max `47.205/38.108ms`, max consecutive slow frames `1/1`, rollback restore/resim `0/0`.
  - `logs/codex-pland-hazard-guidmap-itemspawn-stage1-2400-20260602`: the same generated course `1` route still performed exactly one compensating client spawn and active confirmation after the moving-hazard GUID-map change. Coin/global and world gates passed; active avg `16.949/16.946ms`, max `41.249/41.352ms`, and max consecutive slow frames `1/1`.
  - `logs/codex-pland-itemspawn-luigi-star-settle-fixed-3200-20260602`: real Luigi Big Star acquisition still passed with active avg `16.887/16.887ms`, max `48.209/49.942ms`.
  - `logs/codex-pland-itemspawn-luigi-death-3720-20260602`: death/respawn and pipe visibility still passed with active avg `17.036/17.035ms`, max `43.122/44.764ms`. The hazard had one replacement-boundary sampled row over the drift threshold, then converged.
  - `logs/codex-pland-itemspawn-result-restart-rerun2-6200-20260602`: result/restart into the second game still passed. An earlier identical attempt detected an existing paired-process stall before any Item spawn, with rollback restore/resim still `0/0`; the immediate rerun passed.
  - `logs/codex-manual-pland-itemspawn-default-launch-1800-20260602`: the manual `-PlanDActorSnapshot` launcher propagated `worldStateSpawnItem=1` to both peers and completed with active avg `17.367/17.366ms`, max `31.630/32.403ms`, and `over33ms=0/0`.
  - A generated course `0` route exposed a duplicate-event edge case: both peers created the Item naturally, then the client could spawn the same host GUID again after its local copy disappeared slightly earlier. Active confirmation now marks that remote GUID as already handled.
  - Post-dedup stage matrix passed for generated courses `0` through `4`: `logs/codex-pland-itemspawn-stage0-dedup-generated-stress-2400-20260602` through `logs/codex-pland-itemspawn-stage4-dedup-generated-stress-2400-20260602`. Course `0` observed the naturally created Item without a client spawn, course `1` performed exactly one compensating spawn plus active confirmation, and courses `2-4` emitted no Item event. Active averages ranged `17.079-17.839ms`; maxima ranged `37.185-49.650ms`; max consecutive slow frames stayed `1`.
  - A later 30-frame lifecycle trace caught a narrower course `0` overlap window: client natural Item GUID `38` was not yet discoverable when the host packet arrived, so immediate compensation spawned GUID `39`; both remained active in the frame `1590` sample. New remote Item GUIDs now receive a 4-frame natural-spawn grace period before compensation.
  - Post-grace validation passed: `logs/codex-pland-item-grace-stage0-2400-20260602` confirmed the natural client Item as `remoteGuid=38 localGuid=38` without compensation or lifecycle-count drift. `logs/codex-pland-item-grace-stage1-2400-20260602` still performed exactly one required compensating spawn at frame `2051`, confirmed it active at `2052`, and had no non-`StageFX` lifecycle-count drift.
  - Automated lifecycle gate validation passed: `logs/codex-pland-lifecycle-gate-stage1-2400-20260602` performed exactly one required course `1` compensation and reported no unexpected actor-count difference. Running the same gate against pre-fix `logs/codex-pland-processlist-lifecycle-stage0-2400-20260602` fails on `01F/00080002 host=1 client=2` at frame `1590`, so the gate catches the repaired overlap.
- `scripts/analyze-nsmb-mvl-rollback-log.ps1` now also classifies single-frame or short-run spikes over `MaxSingleFrameMs` as `perf-fail`, so a run like the old 353ms death/pipe case cannot be reported as `ok` just because the average FPS is acceptable. It also avoids marking a completed result-scene trace as a freeze solely because player actors are stationary during the result transition.
- Added `MELONDS_NSML_PERF_SPIKE_PHASE_TRACE=1`. On a slow frame, `NSMB PerfPhaseSpike` reports `mpMs`, `inputMs`, `beforeHookMs`, `runFrameMs`, `afterHookMs`, `drawMs`, `audioMs`, `limitMs`, and `unaccountedMs`. Strict split-input smoke runs and manual `-PlanDActorSnapshot` enable it by default.
  - `scripts/analyze-nsmb-mvl-rollback-log.ps1` summarizes the largest gameplay phase spike from frame `900` onward and its dominant phase, ignoring startup peer-wait noise.
  - `logs/codex-pland-perfphase-v2-stage0-1800-20260602`: strict 1800F route passed with active avg `17.222/17.223ms`, max `44.937/43.706ms`, max consecutive slow frames `1/1`; analyzer selected gameplay phase spikes dominated by `runFrame`.
  - `logs/codex-manual-pland-perfphase-launch-1800-20260602`: manual Plan-D propagation and overhead check passed with `perfSpikePhaseTrace=1`, active avg `17.450/17.448ms`, max `32.223/32.240ms`, and `over33ms=0/0`. Compared with the previous equivalent manual launch at `17.367/17.366ms`, phase timing adds about `0.08ms`.
- Reduced performance-observer interference in strict automation:
  - `ReadGameStateSample()` used to repeat full Main RAM actor scans for each queried actor. An intermediate cache removed the repeated scans, but still swept Main RAM once per diagnostic sample. The diagnostic cache now walks the ROM-analyzed NSMB process execute/delete/render/create and ID lookup lists, deduplicates actor bases, and reuses the resulting live-actor cache for player, star, stage, MvL object, and lifecycle queries. The cache is active only while producing the diagnostic CSV; the normal Plan-D actor/world snapshot path is unchanged.
  - Added `NSMB BeforeHookPhaseSpike` and `NSMB RemoteInputWaitSpike` for strict runs. They split setup, actor-state application, barrier, checkpoint, packet-bridge scratch, outbound network, gate, and final remote-wait costs, and log the remote target frame and wait-loop count.
  - Expanded `NSMB AfterHookPhaseSpike` to split heartbeat, barrier, bridge, lifecycle trace, rollback trace, runtime-force, artifact trace, actor apply, game-state trace, and actor sync costs.
  - `InitFromEnvironment()` now has an atomic initialized fast path, and FPS accounting uses a dedicated mutex instead of contending with the network-pump mutex every frame.
  - Performance-spike trace lines are buffered instead of flushing stdout on the emulation thread. Stall liveness is observed separately through the dedicated heartbeat file.
  - Stall detection now publishes the latest frame to an atomic value every `120` frames by default. A background heartbeat writer thread writes and flushes the tiny dedicated file, so filesystem flush latency no longer pauses emulation.
  - Rejected intermediate measurements exposed observer costs correctly. `logs/codex-pland-beforehook-breakdown-3600-20260602` showed stdout-flush feedback reaching `833.280/1384.609ms`. After buffering the spike traces, `logs/codex-pland-buffered-spiketrace-existingrom-result-restart-6200-20260602` isolated a diagnostic `TraceGameState=789.044ms` Main RAM scan. After switching to the process-list walker, `logs/codex-pland-afterhook-presnapshot-breakdown-4200-20260602` isolated an `heartbeatMs=82.432` synchronous file flush. The async writer removed that emulation-thread cost.
  - Final repeated result/restart validation passed: `logs/codex-pland-async-heartbeat-result-restart-6200-20260602` reached the second game, kept Big Star drift `0/0`, moving-hazard max drift `2048/0`, and passed the 34-field manager/global gate across `177` rows. Active avg was `17.510/17.510ms`, max `56.524/60.689ms`, and max consecutive slow frames `1/1`. Runtime Plan-D `actorStateMs` remained approximately `0.01-0.1ms`; the remaining isolated spikes were packet-bridge peer waits, emulator `runFrame`, or unaccounted scheduling-like time outside the instrumented phases.
  - The longer three-game route also passed with the async observer path: `logs/codex-pland-async-heartbeat-threegame-wins3-12000-20260602` entered game 3 at frame `9961`, kept Big Star drift `0/0`, moving-hazard max drift `4096/0`, and passed the manager/global gate across `371` rows. Active avg was `17.148/17.148ms`, max `52.201/50.323ms`, and max consecutive slow frames `1/1`. Runtime `actorStateMs` stayed approximately `0.01-0.12ms`.
- Added role-specific split inputs for repeated result/restart stress:
  - `tests/nsmb_us_direct_mvl_repeat_result_stress_host.inputs`
  - `tests/nsmb_us_direct_mvl_repeat_result_stress_client.inputs`
- Added role-specific split inputs for the Luigi death/pipe-respawn stress:
  - `tests/nsmb_us_direct_mvl_luigi_death_mario_continues_host.inputs`
  - `tests/nsmb_us_direct_mvl_luigi_death_mario_continues_client.inputs`
- Added role-specific split inputs for the deterministic Luigi Big Star pickup:
  - `tests/nsmb_us_direct_mvl_luigi_star_right_host.inputs`
  - `tests/nsmb_us_direct_mvl_luigi_star_right_client.inputs`
- Configured-ROM generation now normalizes direct-route `fixed` mode to generator `random` while preserving the explicitly selected stage. `logs/codex-pland-singlescan-filtered-stage1-defaultfixed-2400-20260602` verified the default wrapper path, one Item compensating spawn, Big Star/hazard/manager gates, and `courseMode=fixed generatorCourseMode=random`.
- Star/result-continuation route is not a useful actor-snapshot correctness failure yet: `logs/codex-playerstate-cache-star-result-continue-9000-20260602` reached result/restart and held about `59.6fps`, but `RequireStarPickup` failed because star counters stayed `0/0`. Existing baseline `logs/codex-rollback-baseline-starcollect-6200-skipmove-20260601` shows the same `result ... stars=0/0 collected=0/0`, so this route/check needs cleanup before being used as a blocker for actor snapshot.
- The previous full/core rollback issue is still reproduced in logs: rollback/resim paths can spike into hundreds of ms when many inputs arrive or forced delay causes repeated rollback. The actor snapshot path avoids that mechanism entirely.
- Current dropped-star/effect experiment:
  - `logs/codex-pland-effect-sync-stage0-2400-20260602`: Effect slot tracing found active slots at the fixed Effect table, and enabling `WorldStateApplyEffects` kept the normal stage `0` stress route light enough: host/client active avg about `16.89ms`, max `29.835/34.965ms`, max consecutive slow frames `0/1`.
  - `logs/codex-pland-effect-sync-luigi-death-notrace-3720-20260602`: the Luigi death/star-loss route passed without game-state CSV tracing, avg `16.750/16.751ms`, max `39.082/41.908ms`, max consecutive slow frames `1/1`.
  - Full game-state CSV tracing is currently too intrusive for spike decisions on this route: `logs/codex-pland-effect-sync-luigi-death-skipcmp-3720-20260602` hit a `traceMs=180ms` observer spike at frame `2610` even though the no-trace route was light. Use no-trace active timing plus targeted gates/traces for performance decisions.
- Current stage-object activation / dropped-star refinement:
  - ROM generation now forces stage-object activation to player `0` before the vsmode stage-lock stubs. This was added after lifecycle traces showed direct local-player differences can prevent an actor from being activated before the runtime snapshot path sees it.
  - Activation validation passed with generated ROMs: `logs/codex-pland-activation-player-stage0-lifecycle-2400-20260602` had `host=52 client=52 shared=52` lifecycle samples, no unexpected actor-count diffs, and active avg `16.788/16.791ms`. The default regenerated stable ROM route also passed: `logs/codex-pland-defaultrom-activation-stage0-2400-20260602`.
  - A short generated stage matrix passed courses `0-4`: `logs/codex-pland-activation-matrix-stage0-1800-20260602` through `logs/codex-pland-activation-matrix-stage4-1800-20260602`.
  - `logs/codex-pland-activation-luigi-death-notrace-3720-20260602` passed the death route after the ROM activation patch, active avg `16.699/16.700ms`, max `37.745/37.963ms`, max consecutive slow frames `1/1`.
  - Dropped-star item sync now tracks `Item(0x01F settings=0x00090002)` separately from the normal world item and accepts item actor Type `2`; the previous item finder only accepted Type `1`, so short-lived dropped-star items appeared in lifecycle logs but were often missed by the packet sampler.
  - `WireWorldState` is now sent both directions, but host-side application only uses `DroppedStarItem`; Big Star and normal Item are still applied only on the client from host samples.
  - `logs/codex-pland-droppeditem-type2-bidir-stage0-trace-2100-20260602` confirmed the dropped-star slot is applied around frame `1876` while staying light: active avg `16.702/16.702ms`, max `26.964/28.009ms`, `over33ms=0/0`.
  - `logs/codex-pland-droppeditem-type2-bidir-luigi-death-notrace-3720-20260602` passed the longer death route with no trace CSV: active avg `16.855/16.855ms`, max `43.114/40.792ms`, max consecutive slow frames `1/1`.
  - `logs/codex-pland-type2-bidir-stage0-lifecycle-only-2400-20260602` passed the lifecycle gate after the Type `2` change: `host=52 client=52 shared=52`, no unexpected actor-count diffs, active avg `16.814/16.815ms`, and `over33ms=0/0`.
- Current enemy/stomp diagnosis:
  - Diagnostic actor-internal tracing now prints object words through `0x1FC` instead of stopping at `0x10C`, so enemy-specific state after the common actor base can be inspected without adding normal runtime cost.
  - `logs/codex-pland-enemy-lifecycle-internals-stage0-2200-20260602` ran a 5-frame lifecycle/internal trace for the existing move/jump/dash stage `0` stress route. It did not reproduce a host/client enemy-count mismatch: the only aggregated lifecycle differences remained local-role `StageFX(0x012)`.
  - `logs/codex-pland-enemy-internals-wide-stage0-1250-20260602` focused on the `0x0053` actor around frame `1155`, where the total actor count drops from `13` to `11` on both peers. The `0x0053` actor and the extended `0x110-0x1FC` region stayed aligned enough that this route is not a useful stomp-desync reproducer. A dedicated enemy-stomp route is still needed.
- Current moving-hazard refinement:
  - Applying the already-sent `StateType` and `Flags` fields to matched moving hazards passed `logs/codex-pland-hazard-stateflags-stage0-2400-20260602`: Big Star drift `0/0`, moving-hazard max drift `2048/0`, active avg `16.916/16.914ms`, max `28.863/31.990ms`, `over33ms=0/0`.
  - `logs/codex-pland-stateflags-effect-luigi-death-notrace-rerun-3720-20260602` passed the death route with Effect sync and hazard state/flags apply: avg `17.053/17.052ms`, max `42.598/43.390ms`, max consecutive slow frames `1/1`.
  - Rejected experiment: host-authoritative deactivation of extra local moving hazards made replacement-boundary matching worse. `logs/codex-pland-hazard-deactivate-stage0-2400-20260602` failed at frame `1530` with moving-hazard X drift `317440`, so extra local actors must not be blindly killed during normal lifecycle churn.

Current blocker / caveat:

- New post-fix manual feedback shows that the current Plan-D correction path is still not a production correctness route: Mario/Luigi contact can freeze the session; broken-block state can differ; Luigi-side minimap Big Star markers can differ while the in-stage Big Star position matches; and 8-coin item rewards can differ between peers. These are not one actor-class bug. They show divergence across collision/event processing, destructible stage state, UI-facing derived/global state, and item/RNG event selection.
- Do not keep expanding per-object transform snapshots or per-symptom host-authoritative events as the primary architecture. Preserve the current lightweight snapshots as diagnostics and narrow recovery experiments, but pivot the primary implementation back to a systematic page-delta rollback checkpoint derived from the working correctness baseline. Capture pre-write Main RAM pages plus the smallest fixed CPU/timer/device core required by restore/resim, and use automatic full/coredelta comparison to discover any missing memory domain. This should cover collision, blocks, UI markers, item rewards, actor-private state, and RNG state without adding one visible symptom at a time.
- Latest automated complex-input matrix no longer reproduces active-object drift, moving-hazard drift, or sustained player drift with input delay `0`; however, user manual feedback still reported complex-play desyncs before this fresh-only after-hook fix. Treat freezes/desyncs as improved but not closed until a new manual log or a stronger automatic route covers enemy contact/stomp, star loss, fall death, and continued chaotic input.
- The generic actor snapshot is intentionally transform-only on apply. It is a broader root-level correction than enemy/star-specific patches, but it still does not spawn arbitrary missing actors or synchronize object-specific private state. Use active-ID heartbeat and lifecycle gates to find persistent classes of drift before adding any narrower ROM/memory field.
- Strict full game-state comparison is still too strict as a promotion gate because it assumes deterministic same-frame equality across many unrelated fields. The current useful gates are targeted: player drift, Big Star, moving hazard, active-ID object diff, lifecycle diff, gameplay heartbeat, and FPS spike/consecutive-slow-frame checks.
- The 12000F three-game route no longer freezes under automated result/restart stress, but ordinary non-rollback frame spikes still exist (`50-52ms` max in that run). Manual play remains required before promotion.
- Added `tests/nsmb_us_direct_mvl_repeat_result_stress.inputs` and exposed `-MvlWins` in the split wrapper. A 12000F repeated death/result/checkpoint-restart route reached MvL stage entries at frames `870`, `5790`, and `9990`.
- Earlier strict runs exposed occasional paired-process stalls in the `278-825ms` range across three-game, Big Star, coin-sync, and item-sync routes. At least the newly reproduced large stalls were observer interference rather than Plan-D snapshot cost: stdout trace flush, diagnostic full-Main-RAM actor scans, and synchronous heartbeat file flush were each isolated and removed from the emulation thread. Keep the older logs as historical caveats because they predate the finer phase traces, but use the current async-heartbeat route for new performance decisions.
- Moving hazards now use a compact multi-instance snapshot with persistent GUID mapping and nearest-position fallback, but application still waits for equal host/client active counts. Automated lifecycle churn and stage variation passed; longer manual play is still required before treating it as complete.
- Effect slots are synchronized only for fixed active Effect slots and without clearing local-only effects. Dropped-star item actors now have a separate bidirectional item slot, but other host/client-only actors still require case-by-case ROM/memory classification rather than generic actor cloning.
- Course `1` host-only `Item` creation is now covered by an item-specific client spawn. The associated `0x0F0 settings=0x01080002` transient is not replicated yet; add it only if a concrete visible or gameplay mismatch appears. Blindly spawning every host-only actor would also replicate local-only effects.
- Result/restart lifecycle traces show local-role `StageFX(0x012)` differences, including a host-only lose/result lead-in effect. Keep these local unless a concrete visible defect appears; do not clone them as gameplay world actors.
- The selected MvL manager/global/stage-scene fields stayed equal during real star acquisition and repeated result/restart. Do not add blind runtime writes for them unless the new observation gate finds a concrete divergent route.
- This means the actor/global snapshot path is now a much more practical Plan-D-like route for "does not freeze / does not rollback-spike / remote actor moves / pipe death visibility survives", but it is still not a correctness replacement for deterministic rollback.

Next actions:

- Build a Main RAM dirty-page/preimage checkpoint ring and pair it with the smallest fixed core snapshot that still restores/resims correctly. Start with automatic page comparison if write tracking misses JIT/direct writes, then optimize the proven page set and write paths. Measure checkpoint bytes, save/restore p50/p95/max, resim burst cost, active FPS, and consecutive slow frames.
- Use the existing working `coredelta`/full snapshot route as an oracle: after restore/resim, compare hashes and targeted state against the baseline under chaos, contact, death, block-break, and item-reward routes. Add memory domains by automatic mismatch classification rather than by visible symptom.
- Add a low-overhead manual-diagnosis trace mode for mismatches that remain after page-delta coverage. Keep event records in bounded in-memory rings and flush asynchronously so tracing does not create FPS failures. Capture player contact/collision callbacks, actor spawn/destroy events, block/tile writes, Big Star world/minimap setter values, 8-coin reward actor/settings, RNG value/call-count/branch provenance, and the existing heartbeat/perf phase data.
- Use that trace mode with a manual reproduction pass only where the systematic rollback candidate still diverges. Treat the user's visible symptoms as markers, then correlate the first divergent event rather than patching only the visible object that ended up wrong.
- In parallel, build focused automatic routes for player-player contact and block break/item reward. The goal is to make the first collision/event/RNG divergence reproducible without requiring full CSV tracing.
- Use the new heartbeat `activeIds=` output on the next manual or long automated desync to identify concrete host-only/client-only ObjectID/Settings pairs. Prioritize recurring non-`StageFX` / non-`0x10B` differences and decide whether each needs a ROM activation change, a spawn/confirm event, or a narrow object-specific field sync.
- Investigate any recurring `053:00000000` host-only/client-only moving-hazard lifecycle gap using the new heartbeat `hazards=` GUID/position details. Avoid the already rejected "kill extra local hazard" approach; prefer classifying why a peer lacks the hazard at the activation boundary and whether a narrow stage-object activation or host-authoritative spawn/confirm path is justified.
- Extend the chaos automation beyond the current stage0-4 movement matrix to include fall death, item/star loss, enemy contact/stomp, and continued inputs after respawn/result. It should keep using frame heartbeat, gameplay heartbeat, active-ID object diff, targeted world/player drift gates, and FPS-spike gates rather than full CSV equality.
- Keep the three-game stress route in repeated performance sweeps so occasional paired-process stalls remain visible instead of being hidden by average FPS.
- Keep the phase traces and async dedicated-heartbeat stall detector enabled in automation, but do not use full game-state CSV traces as the primary FPS-spike signal on long routes; they can create observer spikes. Prefer no-trace active timing plus targeted world/effect/lifecycle traces.
- Continue ROM/memory analysis for enemy stomp correctness. The unsafe "deactivate extra local hazards" path is rejected; after the activation-player patch, the next useful direction is building a deterministic enemy-stomp reproducer, then identifying narrow enemy damage/death/stomp state fields or a proper StageEntity/Actor destroy event rather than blindly killing extras.
- Use the MvL manager/global observation gate on new routes and add only fields that show a concrete persistent mismatch, without falling back to full savestate or full CPU rollback.
- Reuse `scripts/analyze-nsmb-mvl-object-lifecycle-diff.ps1` on new lifecycle traces and investigate only persistent non-`StageFX` gaps.
- Tighten drift thresholds after more route coverage; the current sustained-drift gate is meant to catch gross desync/freeze without rejecting a transient one-row correction.

## 2026-06-02 current status - real rollback gate and Plan-D-like retest

Current working candidate is still experimental:

- `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan + tinyCoreFlags=0x241`.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now defaults to checkpoint interval 1, input max frame lead 1, `RollbackInputWaitUs=2500`, `RollbackMaxResimFrames=1`, network pump 50us, JIT reset skip, render skip during resim, and CP15 PU debug suppression.
- `coredelta` remains the correctness/perf baseline. The lightweight path is still not promoted.

Validation changes:

- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` now has `-MinRollbackResims`, so a bounded-input-wait run cannot pass as a rollback test when it avoided rollback entirely.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1` passes the minimum-resim gate, scans candidate logs recursively, records `rollbackResims` summary lines, exposes `-RollbackMaxResimFrames`, and can force remote input delay with `-InputSendDelayFrames`.
- `nsmbranges-proclist-arena-gpu2d-noheap` was added as a RAM-only Plan-D extreme candidate; it is useful as a negative control.

Implementation changes:

- NSMB range restore no longer invalidates all Main RAM JIT pages. It invalidates only restored ranges, and honors `MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET=1`.
- CP15 PU-region debug logging can be suppressed with `MELONDS_NSML_SUPPRESS_PU_DEBUG=1`, removing large rollback-time stdout bursts.

Latest measurements:

- The old manual comparison still matches the user report: `logs/nsmb-mvl-manual-local-20260601-212956` is `nsmbtinycore` abort/low-FPS failure, while `logs/nsmb-mvl-manual-local-20260601-213213` is a non-frozen `coredelta` baseline.
- `RollbackInputWaitUs=8000 + netpump 50us` is no longer treated as a valid rollback pass by itself. `logs/codex-sweep-tinycore-rbwait8000-minresim1-1600-20260602/20260601-234507` fails correctly with `resims=0`.
- Current best real-rollback natural route: `logs/codex-sweep-tinycore-suppresspu-natural-wait2500-compare-2400-20260602/20260602-000856`. It passed state comparison but still failed strict rollback spike gate by a small margin: host max `32.935ms`, client rollback spike `33.894ms`.
- Relaxed correctness proof for the same settings passed 2400F: `logs/codex-sweep-tinycore-maxresim1-wait2500-netpump-correctness-2400-20260602/20260601-235854`.
- Forced one-frame send delay remains unacceptable: `logs/codex-sweep-tinycore-suppresspu-forced-delay1-wait2500-compare-2400-20260602/20260602-000614` saw repeated rollback frames, active max `102.108ms`, and `over33ms=505`.
- RAM-only `nsmbranges` is rejected: `logs/codex-sweep-nsmbranges-forced-delay1-wait2500-compare-1600-20260602/20260602-000804` data-aborted around frame 961 despite very low restore cost.

Current conclusion:

- The lightweight snapshot size is not the main blocker anymore. CPU/core restore plus full-frame `nds->RunFrame()` resim is the remaining cost.
- CPU core restore is required for correctness; RAM-only actor/global restore is too unstable.
- The best tinycore path is close under natural localhost timing but not robust under forced delay. Next useful direction is a narrower CPU/timer/core subset or a game-level actor/global apply that avoids full NDS resim, not wider Main RAM snapshots.

## 2026-06-01 prior status - spike gate and Plan-D-like experiments

Current working candidate is still experimental:

- `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan`.
- Best current no-freeze/no-rollback-spike automation setting adds bounded same-frame input wait: `MELONDS_NSML_ROLLBACK_INPUT_WAIT_US=8000` plus `MELONDS_NSML_NET_PUMP_THREAD=1` / `MELONDS_NSML_NET_PUMP_SLEEP_US=50`.
- Manual/log comparison confirmed the user's report: `logs/nsmb-mvl-manual-local-20260601-212956` is an `nsmbtinycore` abort/low-FPS failure, while `logs/nsmb-mvl-manual-local-20260601-213213` is a non-frozen `coredelta` baseline.
- The aborting manual log predates the ARM9 stack addition. With the current stack range, `logs/codex-currentframefix-tinycore-long-4200-20260601/20260601-225143/nsmbtinycore-proclist-arena-noheap` passes 4200F without abort/stall, checkpoint size `398,399` bytes, save avg about `0.18ms`, restore avg about `3.6ms`.

Spike-aware validation added:

- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` now has `-MaxRollbackFrameMs`; it fails specifically when a frame containing `rollbackResimDelta > 0` exceeds the limit, so average FPS can no longer hide rollback stutter.
- `NSMB PerfSpike` now prints `rollbackRestoreDelta` and `rollbackResimDelta`, which lets the harness distinguish rollback spikes from ordinary slow frames.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1` passes the new rollback-frame gate and classifies it as `perf-fail`. It also has `-MaxActiveFrameMs` so non-rollback frame spikes can be gated separately.

Performance experiments:

- Skipping JIT reset on rollback restore (`MELONDS_NSML_ROLLBACK_SKIP_JIT_RESET=1`) reduced tinycore restore avg from roughly `9.5ms` to roughly `3.5-3.8ms`.
- Skipping render during rollback resim (`MELONDS_NSML_ROLLBACK_RESIM_SKIP_RENDER=1`) did not remove the visible spikes; the remaining cost is mainly the full NDS `RunFrame()` resimulation.
- With checkpoint interval 8 / frame lead 8, strict rollback spike gate still sees about `150-180ms` max rollback frames.
- With checkpoint interval 1 / frame lead 1, the same route improves to about `54-76ms` max rollback frames, but still fails a 33ms no-stutter gate.
- An experimental `MELONDS_NSML_ROLLBACK_MAX_RESIM_FRAMES=1` cap reduced max rollback frames to about `38-40ms` in 1600F, but the 4200F correctness run failed at frame 1110 (`playerActor0Y` mismatch). This is not promotable.
- A Plan-D-like remote-player actor snapshot mode (`MELONDS_NSML_STATE_APPLY_MODE=remote-player`) was added for experiments, but no-rollback state-apply testing ran at only about `38fps` and failed the movement-probe harness. It is not a replacement for rollback yet.
- Bounded same-frame remote input wait is not an input-delay scheme and does not change local frame delay, but it is also not the final Plan-D snapshot answer. It prevents prediction in the common localhost case and therefore avoids full rollback resim spikes.
- `RollbackInputWaitUs=8000` without network pump passed the 4200F rollback-spike gate but still showed occasional non-rollback active-frame spikes around `39ms`.
- `RollbackInputWaitUs=8000` plus network pump 50us passed 4200F move+jump+dash stress with game-state comparison, `-MaxRollbackFrameMs 33`, `-MaxActiveFrameMs 50`, and `-MaxConsecutiveSlowFrames 120`: `logs/codex-sweep-tinycore-rbwait8000-netpump-param-rb33-active50-4200-20260601/20260601-233536`. Active timing was about `avgFrameMs=17.15`, `maxFrameMs=44.51`, active FPS about `58.3`, and `rollbackResims=0` in the sampled active window.

Current conclusion:

- The old "案D寄りが固まる" report was real. The current stack-augmented tinycore candidate no longer reproduces that abort in the 4200F automated route, but rollback resimulation still causes noticeable spikes.
- Lightweight snapshot size is no longer the dominant cost. The blocker is full-frame resimulation: even a 398KB restore is followed by 1-2 full `nds->RunFrame()` calls.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now defaults to checkpoint interval 1, input max frame lead 1, rollback input wait 8000us, and network pump 50us because that is the best measured tinycore setting so far. It is still experimental, not the final no-stutter answer.
- Next direction: keep `coredelta` as correctness baseline, keep tinycore as the light snapshot candidate, and continue ROM/memory-level work to avoid full NDS resim rather than relying only on bounded same-frame waiting.

## 2026-06-01 prior lightweight direction - actor arena + ARM9 stack snapshot

Current Plan-D-like candidate: `nsmbtinycore + delta-discovered globals + process-list object ranges + actorArena + ARM9 stack + no heap scan`.

Implementation:

- Added `MELONDS_NSML_ROLLBACK_NSMB_ACTOR_ARENA_RANGES=1`.
- Added `MELONDS_NSML_ROLLBACK_NSMB_ARM9_STACK_RANGE=1`.
- Actor arena range currently adds `0x021B2600+0x5000` and `0x02088B00+0x200`.
- ARM9 stack/scratch range currently adds `0x023E0000+0x20000`. This comes from the failed manual `nsmbtinycore` run where ARM9 aborted with `sp=0x027E38F4`, which mirrors into Main RAM near `0x023E38F4`.
- This is intentionally not a full savestate and not full Main RAM. It is a small static actor/global/stack snapshot plus process-list-derived live objects and tiny core state.

Manual-log classification:

- `logs/nsmb-mvl-manual-local-20260601-212956`: user-reported frozen Plan-D-like run. New analyzer classifies it as `abort`; client has `ARM9: prefetch abort (frame=2077 pc=00000004 ...)`, host `maxConsecutiveSlowFrames=409`, client `maxConsecutiveSlowFrames=251`.
- `logs/nsmb-mvl-manual-local-20260601-213213`: user-reported non-frozen baseline run. Analyzer classifies it as `ok`; host/client `maxConsecutiveSlowFrames=4/4`.
- Added `scripts/analyze-nsmb-mvl-rollback-log.ps1` so forced-close manual logs can be classified after the fact instead of being dismissed as only `missing frame limit`.

Verification:

- `logs/codex-nsmbcoreranges-proclist-arena-noheap-compare-6000-20260601`: same NSMB Main RAM ranges with full non-MainRAM core state passed 6000F. This suggests the old failure was not just actor range coverage; missing rollback state around stack/core interaction was plausible.
- `logs/codex-tinycore-flag-probe-pred1-2400-20260601` and `logs/codex-tinycore-fullflag-probe-pred1-2400-20260601`: very aggressive prediction-probe-every-frame stress fails even for `coredelta`, so it is retained as an overload diagnostic, not a promotion gate.
- `logs/codex-sweep-pred10-limit100-3600-20260601`: moderate prediction probe passed both `coredelta` and pre-stack actorArena candidate. `logs/codex-sweep-pred5-limit200-3600-20260601` and `logs/codex-sweep-pred8-limit120-3600-20260601` fail even baseline, so they are too severe for current correctness gating.
- `logs/codex-sweep-tinycore-arena-stack-6000-20260601`: stack-augmented candidate passed 6000F move+jump+dash stress with slow-run gate.
- `logs/codex-sweep-tinycore-arena-stack-trace-2600-20260601`: stack-augmented candidate passed with trace. Checkpoint `bytesLast=398,399`, `saveAvgUs=192`, `restoreAvgUs=9,519`, `tinyFlags=0x200`, `actorArena=1`, `arm9Stack=1`, `heapScan=0`.
- `logs/codex-sweep-tinycore-arena-stack-pred10-limit100-3600-20260601`: stack-augmented candidate passed moderate prediction-probe stress.
- `logs/codex-sweep-tinycore-arena-stack-death-skipprobe-4200-20260601`: stack-augmented candidate passed the Luigi death/Mario continues route with game-state comparison.

Current conclusion:

- The old 270KB actorArena/noHeap candidate is not trusted for manual play because the user reproduced a freeze and the log shows ARM9 abort plus long slow-run.
- The current candidate is about 398KB, still much lighter than the 2.5MB `coredelta` baseline, and now includes the stack range implicated by the abort. It is an experimental manual candidate only, not final.
- Manual explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now uses actorArena/processList/ARM9-stack/noHeap by default, with checkpoint interval 1 and input max frame lead 1 from the later spike tests.

## 2026-06-01 prior automation state - slow-run detection

User clarification: the star pickup/fall-death freeze was only an example from an automated run. The real bug was that automation treated runs as passed even when melonDS had become effectively stuck at very low FPS. The harness must detect both hard frame-progress stalls and long consecutive slow-frame runs.

Implemented detection:

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`: keeps the frame-progress watchdog through `-StallTimeoutMs`, `-StallStartFrame`, `-StallPollMs`, and stdout heartbeat parsing.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: now supports `-SlowFrameThresholdMs` and `-MaxConsecutiveSlowFrames`. It parses `NSMB PerfSpike` frame numbers and fails if frames over the threshold are consecutive for too long.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: enabling `-MaxConsecutiveSlowFrames` now forces `MELONDS_NSML_FPS_SPIKE_TRACE=1` and ensures the spike threshold is not higher than the slow-frame threshold, so the check cannot silently no-op.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1`: candidate sweep now passes the same consecutive-slow-frame gate and classifies it as `perf-fail`.
- `scripts/run-nsmb-mvl-manual-local.ps1`: `-LowLatencyRollback` keeps heartbeat logging and now enables game-state/life/defeated traces by default so forced-close manual failures leave more useful logs.

Latest verification:

- PowerShell parse passed for the touched smoke/manual scripts.
- The previous `nsmbtinycore` 6000F result is no longer valid as a pass. Re-run with `-MaxConsecutiveSlowFrames 120 -SlowFrameThresholdMs 33` failed correctly: `logs/codex-detect-slowrun-nsmbtinycore-dual-stress-6000-20260601`, host `maxConsecutiveOver33=2519`, client `maxConsecutiveOver33=708`, active FPS around 19.
- `nsmbtinycore` with `RollbackCheckpointInterval=16` still failed: `logs/codex-detect-slowrun-nsmbtinycore-ckpt16-dual-stress-6000-20260601`, host `maxConsecutiveOver33=708`, active FPS around 19. Checkpoint frequency affects the symptom but does not fix it.
- `coredelta-page256-k30` under the same 6000F move+jump+dash stress and the same slow-run gate passed: `logs/codex-detect-slowrun-coredelta-page256-k30-dual-stress-6000-20260601`, host/client `maxConsecutiveOver33=2/3`, active FPS around 51.

Current conclusion:

- The old automation was wrong: average FPS and frame-limit completion were insufficient. Consecutive slow-frame detection is now part of the pass/fail gate.
- `coredelta-page256-k30` remains the practical correctness/perf baseline for zero-delay rollback. It is still heavy, but it does not show the long solid low-FPS failure in the current 6000F stress.
- `nsmbtinycore + delta-discovered + light GPU3D` is not usable as current best despite being much lighter. It can degrade into long low-FPS runs without a hard process stall.
- Next ROM/memory-analysis direction: keep `coredelta` as the baseline, use its page-delta coverage and the NSMB process/object list code to find a smaller actor/global range set, and do not promote a lightweight backend unless it passes the consecutive-slow-frame gate.

## 2026-06-01 superseded automation note - stall watchdog and old sweep

User requirement clarified: the next rollback work must not rely on manual observation of "melonDS froze". The test harness now has a frame-progress watchdog. When `-StallTimeoutMs` is set, melonDS emits `NSMB Heartbeat: inst=... frame=...` every 30 active frames, and the wrapper kills the child process if the latest observed frame stops advancing after `-StallStartFrame`. This catches the manual-style hard freeze even when the user has to force-close melonDS and the normal end-of-run logs are incomplete.

Implemented scripts:

- `scripts/run-nsmb-mvl-lan-route-smoke.ps1`: added `-StallTimeoutMs`, `-StallStartFrame`, `-StallPollMs`, heartbeat env setup, and child-process stall detection from stdout progress lines.
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1`: passes stall watchdog parameters through to host/client runs.
- `scripts/run-nsmb-mvl-manual-local.ps1`: `-LowLatencyRollback` now defaults to `-StallTimeoutMs 5000`, so manual low-latency runs also leave progress heartbeats for diagnosing freezes.
- `scripts/run-nsmb-mvl-manual-local.ps1`: explicit `-RollbackBackend nsmbtinycore` under `-LowLatencyRollback` now configures `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1`, scan interval 30, and `tinyCoreFlags=0x200` by default for the current lightweight candidate.
- `scripts/run-nsmb-mvl-rollback-candidate-sweep.ps1`: runs rollback backend candidates under the same move+jump+dash stress input and writes `summary.csv` with `passed`, `mismatch`, `stalled`, `abort`, `timeout`, or `perf-fail`.

Latest verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
- Watchdog normal path passed: `logs/codex-stall-watchdog-coredelta-smoke-1600-20260601`.
- Watchdog intentional trip passed: `logs/codex-stall-watchdog-intentional-trip-20260601` failed with `melonDS process stalled... latestFrame=1020`, confirming the wrapper can detect frame-progress stalls automatically.
- Important fix: `EnvInt` now uses `strtol(..., base 0)`, so hex env values such as `MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200` are parsed correctly. Earlier tiny-core runs that printed `tinyFlags=0x0` were not valid flag tests.
- Candidate sweep after the hex fix: `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190040/summary.csv`.
- Long validation for the current lightweight candidate: `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190537/summary.csv` and no-trace run `logs/nsmb-mvl-rollback-candidate-sweep/20260601-190741/summary.csv`.

Current candidate results from the post-fix sweep:

- `coredelta-page256-k30`: `passed`. Still heavy: final stats around `bytesLast=2,465,673`, `bytesAvg=2,742,996`, `saveAvgUs=3914`, `restoreAvgUs=22297`; timing `avgFrameMs=20.012`, `maxFrameMs=301.520`, `over25ms=114`, `over33ms=35`.
- `nsmbtinycore-expanded`: `passed` at 2600F with comparison and trace. This is the current best lightweight candidate: `bytesLast=269,175`, `bytesAvg=268,552`, `saveAvgUs=2393`, `restoreAvgUs=12310`, `tinyFlags=0x200`, `deltaDiscovered=1`. Timing was `avgFrameMs=20.979`, `maxFrameMs=316.254`, `over25ms=132`, `over33ms=40`.
- `nsmbtinycore-proclist-heap900`: `mismatch` at `frame=1590 movingHazardX`. ProcessList+low-frequency heap scan lowers save cost (`saveAvgUs=247`) but currently drops a required moving-hazard range.
- `nsmbcoreranges-proclist-heap900`: `mismatch` at `frame=1590 movingHazardX`; around `2.56MB`, `saveAvgUs=3906`, `restoreAvgUs=14599`, and still not correct.

Long validation for `nsmbtinycore-expanded`:

- 6000F comparison+trace passed: `bytesLast=269,175`, `bytesAvg=268,963`, `saveAvgUs=2366`, `restoreAvgUs=11411`, `maxFrameMs=298.646`, `over25ms=281`, `over33ms=34`.
- 6000F no-trace playlike run passed: `avgFrameMs=17.403`, `maxFrameMs=285.308`, `over25ms=123`, `over33ms=20`.

Current conclusion:

- `nsmbtinycore-expanded` is now the most promising rollback backend for the user-requested "light checkpoint /案D寄り" direction. It is roughly 269KB per checkpoint in this stress route, versus roughly 2.46MB for `coredelta`.
- The current blocker has shifted from correctness to spike reduction and broader validation. Even the lightweight candidate still has large single-frame spikes around 285-316ms in these local two-instance stress runs.
- Next search direction: keep `nsmbtinycore-expanded` as the candidate baseline, test longer/manual-like sessions with the watchdog enabled, and then try to replace scan30 heap discovery with a ROM/memory-derived static actor/global range set so `saveAvgUs` and spike counts drop without losing correctness.

## 2026-06-01 current direction - ROM/memory analysis and spike-aware validation

Update after manual play: `nsmbcoreranges` is still not acceptable as the default manual path. User manual run `logs/nsmb-mvl-manual-local-20260601-182701` used `rollbackBackend=nsmbcoreranges` and froze during play after many rollback resimulations and repeated `NSMB PerfSpike` lines around frame 1900-2633. The immediately following user run `logs/nsmb-mvl-manual-local-20260601-182807` used `rollbackBackend=coredelta` and reached result/restart logging around frame 3313 without the same freeze. Therefore `scripts/run-nsmb-mvl-manual-local.ps1 -LowLatencyRollback` default is back to `coredelta`; `nsmbcoreranges` remains an explicit experimental analysis backend only.

方針を `coredelta` 固定ではなく、案D寄りのROM/メモリ解析で正しい軽量snapshotを作る方向へ戻した。`coredelta` は引き続き安全基準として残すが、軽量化候補の検証は `coredelta` の `MELONDS_NSML_ROLLBACK_DELTA_PAGE_TRACE=1` で実際に変化したMain RAM pageを取り、既存NSMB range候補で未カバーのpageを集計して進める。

追加した検証/解析:

- `NSMB Test: active frame timing ... avgFrameMs/maxFrameMs/over16ms/over25ms/over33ms` を終了時に出すようにした。平均FPSだけでは見えないガクッとした落ち込みを検出するため。`MELONDS_NSML_FPS_SPIKE_TRACE=1` と `MELONDS_NSML_FPS_SPIKE_THRESHOLD_MS` で逐次 `NSMB PerfSpike` も出せる。
- `scripts/run-nsmb-mvl-split-local-input-smoke.ps1` に `-MaxActiveFrameMs` / `-MaxActiveFrameOver25ms` / `-MaxActiveFrameOver33ms` を追加し、FPS平均だけでなく瞬間dropをgateできるようにした。
- `scripts/analyze-nsmb-rollback-delta-pages.ps1` を追加。`NSMB RollbackDeltaPagesUncovered` を集計し、未カバーrangeを頻度順に出す。

delta-page解析結果:

- `logs/codex-rollback-delta-page-trace-knownranges-990-2600-20260601`: 既知range入りでも `uncoveredFrames=163`, `ranges=64`。上位は `0x02190400`, `0x021B4F00`, `0x02095600`, `0x0208FA00`, `0x0229BD00`, `0x02350E00` など。
- 上位rangeを `AddNSMBRollbackDeltaDiscoveredRanges` に追加後、`logs/codex-rollback-delta-page-trace-expandedranges-v2-990-2600-20260601` は `summaries=402`, `uncoveredFrames=0`, `ranges=0`。少なくとも990-2600Fの移動+ジャンプ+ダッシュstressでは、既知range候補が `coredelta` 変更pageを覆うところまで来た。

拡張rangeでの `nsmbcoreranges` 再検証:

- `logs/codex-nsmbcoreranges-expandedranges-stress-compare-2600-20260601`: `nsmbcoreranges` / `InputDelayFrames=0` / move+jump+dash stress / game-state比較ありで2600F通過。checkpointは約 `2,559,101` bytes、saveAvgUsは約 `5.57ms`、restoreAvgUsは約 `12.4-12.8ms`、active fpsは約 `53fps`。
- `logs/codex-nsmbcoreranges-expandedranges-stress-playlike-2600-20260601`: trace/game-stateなし寄りで2600F通過。active fpsは host/client `53.09/53.37`、throttleは0。ただしactive frame timingは `maxFrameMs=213-252ms`, `over25ms=84-87` で、ガクッとしたdropは残る。
- 旧候補が停止した条件に近い `InputSendDelayFrames=6` / 6000F stress は `logs/codex-nsmbcoreranges-expandedranges-delay6-6000-20260601` で完走。以前の `arm9PC=0xffff0104` / `arm9SP=0x0` 停止はこのrange拡張では再発していない。active fpsは約 `45.5fps`、restoreOpsは host/client `116/111`、restoreAvgUsは約 `11.5-12.4ms`。重い遅延stressなので通常性能とは分ける。
- 手動向け `-LowLatencyRollback` の既定は `coredelta`。`nsmbcoreranges` は `-RollbackBackend nsmbcoreranges` で明示した解析用に限定する。

Current blocker: 拡張 `nsmbcoreranges` は停止耐性は改善したが、checkpoint sizeが約2.56MBのままで、案Dの軽量actor/global snapshotとはまだ言えない。次は、今回追加したpageのうち本当に必要なactor/global/stack/scratchだけをROM/メモリ構造で分類し、ProcessList/global由来の小さいrangeへ置き換える。

## 2026-06-01 rollback stress update - authoritative current note

現在の手動向け `-LowLatencyRollback` は、`nsmbcoreranges` から `coredelta` へ戻した。理由は、移動 + ジャンプ + ダッシュ同時入力の長時間stressで `nsmbcoreranges` が停止/timeoutし、client側 game-state trace では `arm9PC=0xffff0104` / `arm9SP=0x0` になったため。`nsmbcoreranges` はcore stateを過去へ戻す一方で Main RAM はNSMB推定rangeだけを戻すため、range外のスタック/一時領域/周辺Main RAMが現在フレームのまま残り、resimulate時にCPU状態とRAM状態が噛み合わなくなる可能性が高い。

現行候補は `RollbackBackend=coredelta` / `RollbackWindow=64` / `RollbackCheckpointInterval=8` / `RollbackResimulate` / `InputDelayFrames=0` / `InputMaxFrameLead=8` / `MELONDS_NSML_ROLLBACK_DELTA_KEYFRAME_INTERVAL=30` / `MELONDS_NSML_ROLLBACK_MAIN_RAM_PAGE_SIZE=256`。これは「page-delta Main RAM + core state」寄りで、軽量な案D actor/global snapshotではない。

移動 + ジャンプ + ダッシュstressの結果:

- `nsmbcoreranges`, `InputSendDelayFrames=6`, 6000F: wrapperは成功扱いしていたが、実際はhost/client childがtimeout。game-stateは3960Fで止まり、client ARM9は異常PC/SPになった。テストハーネス側も、child stderr timeoutを見逃す経路があったため修正した。
- `coredelta`, page 256, keyframe 30, `InputSendDelayFrames=6`, 6000F: 完走。active fps は host/client とも約 `43.1fps`。重い遅延stressなので通常プレイFPSとは分けて扱う。
- `coredelta`, page 256, keyframe 30, `InputSendDelayFrames=6`, 2600F, game-state comparisonあり: 完走。座標/global hash系の比較は通過。
- `coredelta`, page 256, keyframe 30, no artificial send delay, 2600F, game-state comparisonあり: 完走。active fps は約 `52.1fps`。
- no artificial send delay, trace/game-stateなしのプレイ寄り2600F: active fps は約 `53.5fps`。`InputMaxFrameLead=16` ではthrottle 0件になったがfpsは約 `53.0fps`で、主な残りコストはthrottleではなくcheckpoint保存/resimulate。`RollbackCheckpointInterval=16` は約 `50.6fps`まで悪化したため、現時点ではinterval 8を維持する。

フレーム落ちの主因は、rollback発生フレームで `restore + 過去checkpointから現在フレームまでのRunFrame再実行 + checkpoint再保存` を同じ表示フレーム内で行うこと。現行 `coredelta` は通常delta checkpointでも約 `2.46-2.49MB`、平均約 `2.7MB`、keyframeは約 `6.6MB`。save平均はおおむね `3.5-4.1ms`、restore平均は `17-20ms` 程度まで出る。したがって、さらに軽い actor/global snapshot を正しく作れれば改善余地はある。ただし `nsmbcoreranges` の失敗から、Main RAMを推定rangeだけに削るとCPU stateとの整合性が壊れやすい。軽量化はROM/メモリ解析で「戻すべきゲーム状態」と「戻してはいけないinput/net volatile領域」を確定してから進める。

## 2026-06-01 older manual rollback status - superseded

Superseded by the current automation note above: `-LowLatencyRollback` manual default is now `coredelta`, not `nsmbcoreranges`. The older text below is retained only as historical context for the manual-run debugging path.

手動プレイ用の現行コマンドは `scripts/run-nsmb-mvl-manual-local.ps1 -LowLatencyRollback -AllowJit`。
`-LowLatencyRollback` は `InputDelayFrames=0` / `InputMaxFrameLead=8` / `RollbackBackend=nsmbcoreranges` / `RollbackWindow=64` / `RollbackCheckpointInterval=8` / `RollbackResimulate` / `PacketBridgeStartFrame=870` を設定し、必要な `MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` と `MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30` もスクリプト内で設定する。
同時に `MELONDS_NSML_FIXED_FRAME_SLEEP=1` と `MELONDS_NSML_PERF_BREAKDOWN=1` を有効にし、2プロセス起動時のbusy-yieldによるCPU消費を抑えつつ、手動runでも `NSMB Perf` 行でフレーム進行と処理内訳を確認できるようにした。
手動ログは既定で `logs\nsmb-mvl-manual-local-yyyyMMdd-HHmmss` に分けて保存するように変更したため、失敗回のログが上書きされにくい。

直近の手動失敗ログは `logs\nsmb-mvl-manual-local` に残っていたが、固定ディレクトリのため2回分が上書きされていた。残っていたログでは host/client とも `localFrame=860` の start-ready 受理直後に終了し、wrapper は `missing frame limit` 扱いになっていた。
修正後の有限検証では、`logs/codex-manual-local-lowlatrollback-script-1200-20260601` が1200Fまで通過し host/client active fps は `58.72/58.63`、`logs/codex-manual-local-lowlatrollback-script-2600-20260601` が2600Fまで通過し active fps は `57.95/58.00`、throttle は両側0だった。
追加で `-SoftwareRenderer` を外し、OpenGL compute + fixed sleep + perf breakdown の `logs/codex-manual-lowlat-fixedsleep-opengl-2600-20260601` は2600Fまで通過し、host/client active fps は `59.55/59.65`、throttle は両側0だった。無期限runの `logs/codex-manual-lowlat-fixedsleep-perf-opengl-20260601` でも870以降に `NSMB Perf` が出ており、ログが870で止まるように見えていた主因は通常ログ不足だった。
座標同期の再確認として `logs/codex-split-lowlat-nsmbcoreranges-ckpt8-start870-predprobe-2600-20260601` で prediction probe ありの2600F game-state比較を通過した。active fps はtraceと比較込みで `52.65/52.64`。

Current blocker: 実手動入力でユーザー環境の停止をまだ直接再現できていない。次に停止した場合は、新しいタイムスタンプ付きログディレクトリの `host.stdout.txt` / `client.stdout.txt` / `wrapper/*.err.txt` を読み、`NSMB Perf` が870以降も進んでいるか、`runFrameMs` / `beforeHookMs` / `limitMs` のどれが増えているかで切り分ける。

## 2026-06-01 zero-delay manual candidate

`InputDelayFrames=4` で通った結果は、rollback成立確認として扱わない。`InputDelayFrames=0` / `InputMaxFrameLead=8` で再検証したところ、旧 `nsmbtinycore + TINY_CORE_FLAGS=0x200` 候補は frame 1950 付近で `netPacketTick` がhost側だけ `0x65c -> 0x437` に巻き戻り、座標不一致を起こした。

現時点で手動プレイに出せるゼロ遅延候補は `nsmbcoreranges + delta-discovered ranges + checkpointInterval=8 + rollbackWindow=64`。`logs/codex-delay0-nsmbcoreranges-resim-patches-ckpt8-rendered-lead8-gamestate-2600-20260601` はgame-state比較ありで2600F通過し、host/client active fps は `56.51/56.77`。game-state traceなしの同条件 `logs/codex-delay0-nsmbcoreranges-resim-patches-ckpt8-rendered-lead8-nogamestate-2600-20260601` はhost/client active fps `57.40/57.62`。

正しさの主因は、tiny coreではなく通常core stateを保存する必要があること。`nsmbtinycore` は軽いが、ゼロ遅延rollbackで実際にresimが走るとNSMB packet/global状態が一致しない。`nsmbcoreranges` はcheckpoint sizeが約 `2.54MB` と重い一方、現在のゼロ遅延候補としては座標一致を維持している。

## 2026-06-01 manual play correction

手動プレイ向けに一度案内した `InputDelayFrames=0 / InputMaxFrameLead=8` は未検証で、2600Fの自動入力・game-state比較でframe 1950に `playerActor0X` 不一致を起こした。手動プレイ推奨から外す。

現時点で手動プレイに使う設定は、旧候補rollbackに `InputDelayFrames=4 / InputMaxFrameLead=4 / InputUnreliable / InputBundleHistory=8` を組み合わせる。描画あり・自動入力・game-state比較ありの2600Fは `logs/codex-manual-safer-oldcandidate-rendered-delay4-lead4-gamestate-2600-20260601` で通過し、host/client active fps は `59.54/59.48`。

手動起動パスでも `-SoftwareRenderer` 付きの1800F run `logs/codex-manual-local-software-oldcandidate-delay4-lead4-1800-20260601` がhost/client active fps `57.80/57.56` で完走した。手動時に10fps級まで落ちる場合は、まずOpenGL compute rendererではなく `-SoftwareRenderer` を使う。

## 2026-06-01 ProcessList-centered rollback snapshot

現在の最有力候補は `nsmbtinycore + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1` を維持しつつ、NSMB Code Reference の `ProcessManager` 構造を使って actor/object range を作る方式。

新しい実験フラグ:

- `MELONDS_NSML_ROLLBACK_NSMB_PROCESS_LIST_RANGES=1`: `Game::executeProcess/deleteProcess/renderProcess/createProcess/idLookupProcesses` をたどって実在objectをsnapshot対象にする。
- `MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_RANGES=1`: 従来のMain RAM object風heap scanをfallbackとして使う。互換性のためdefaultは有効。
- `MELONDS_NSML_ROLLBACK_NSMB_HEAP_SCAN_INTERVAL=900`: fallback heap scanだけを低頻度化する。`MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30` のProcessList更新とは分離した。

結論:

- ProcessList-only (`heapScan=0`) は通常FPSと短距離probeでは良い。1500F traceで `saveAvgUs=157-159us`、1800F no-trace FPSでhost/client `59.93/59.97fps`、2600F game-state + predprobe10 limit6も通過した。
- ただし ProcessList-only は6000Fの後着入力stressでframe 4350に `playerActor1Y` 差分を起こしたため、現時点では単独採用しない。
- `ProcessList + heapScanInterval=900` は6000F後着入力stressを通過した。最終hostは `bytesLast=254,219`, `bytesMax=254,219`, `saveAvgUs=169`, `restoreOps=2`, `restoreAvgUs=11,942`, active fps `59.06`。clientは `saveAvgUs=168`, active fps `59.06`。
- 従来のscan30候補は同stressで `saveAvgUs=431-433us` 程度だったため、正しさを維持しつつ保存コストを約0.17msまで落とせた。

FPS方針:

- 15fps級の遅さは通常性能として扱わない。JITなし、restore diff、scan1、game-state CSV大量出力、`InputNetplayTrace` の長時間runは診断条件。
- 通常性能gateは `-AllowJit` を必須にし、traceなし/軽traceと分けて `active fps` を見る。
- 今回の6000F stressはtrace付きでもhost/client active fps `59.06` なので、現候補はFPS面では実用候補に残す。

次の確認:

- 別input routeとstock touch系で `ProcessList + heapScanInterval=900` を再確認する。
- さらに軽くするなら `heapScanInterval=1800` や、heap scan対象をCode Reference由来のmanager/globalへ寄せる。ただしProcessList-onlyの4350F不一致を踏まえ、fallbackを完全に消すのはまだ早い。

## 2026-06-01 FPS-aware rollback validation

現時点の性能判断では、15fps級の遅さはrollback本体ではなく、主に検証ハーネスを `-AllowJit` なしで回していたことが原因。通常性能を見るrunでは必ず `-AllowJit` を付け、`MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1`、`scanInterval=1`、game-state CSV大量出力などの診断条件とは分けて扱う。

Current candidate:

- `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30`。
- checkpointは約 `253,427` bytes、通常保存平均はおおむね `0.43ms` 前後。
- JIT有効・no draw・no audio sync・game-state traceなしの1800F FPS計測では、baselineがhost/client `59.88/59.96fps`、rollback候補が `59.38/59.40fps`。現候補の通常時overheadはこの条件では約0.5fpsで、15fps級ではない。
- `InputNetplayTrace` 有効やgame-state trace有効のrunは診断用。FPS結論には使わない。

Completed:

- prediction probeをフレーム範囲で絞れる `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_START_FRAME` / `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_END_FRAME` を追加した。遅い序盤を変えず、4200F以降などにrollback負荷を寄せられる。
- 入力送信遅延も `MELONDS_NSML_INPUT_SEND_DELAY_START_FRAME` / `MELONDS_NSML_INPUT_SEND_DELAY_END_FRAME` で範囲指定できるようにした。end frameは `0` を「上限なし」として扱う。
- 4200F以降だけ `InputSendDelayFrames=8`、`InputMaxFrameLead=4` の実用寄り条件では、throttleが待って吸収し、5100Fを通過した。ただしactive fpsは約 `49.9fps` まで落ちるため、送信遅延を常時待つ条件は体感評価では別扱い。
- 同じ4200F以降遅延で `InputMaxFrameLead=-1` にしてthrottleを外すと、`RollbackWindow=20` では実input到着が31F後になり `checkpoint missing`。このstressにはwindow 20が不足だった。
- `RollbackWindow=60` に広げた同条件は4500Fを通過した。hostは4200F以降のprobeを含め `restoreOps=3`, `resims=3`, `restoreAvgUs=10,976`, `saveAvgUs=433`, active fps `58.63`。clientも `restoreOps=3`, `resims=3`, `restoreAvgUs=10,475`, active fps `58.87`。
- 同じ `RollbackWindow=60` 条件で5400Fまで延長し、前回問題になった4950F付近もhost/client一致で通過した。最終hostは `bytesLast=253,939`, `saveAvgUs=422`, `restoreOps=2`, `restoreAvgUs=11,286`, `resims=2`, active fps `58.89`。clientは `saveAvgUs=427`, `restoreOps=3`, `restoreAvgUs=10,652`, `resims=3`, active fps `59.06`。
- さらに6000Fまで延長し、結果後の再開周辺もhost/client一致で通過した。最終hostは `bytesLast=253,427`, `bytesMax=254,087`, `saveAvgUs=433`, `restoreOps=2`, `restoreAvgUs=13,236`, active fps `58.91`。clientは `saveAvgUs=431`, `restoreOps=3`, `restoreAvgUs=10,574`, active fps `59.03`。

Current blocker:

- rollback windowは「最大到着遅延 + resim余裕」より短いと、状態の軽さ以前にcheckpointが捨てられて復元できない。診断stressではwindow 60が必要だった。
- 以前の5400F manual seed `0x7A7950E5` では、scan1でも直らないlate object divergenceが出た。今回のwindow 60 + targeted delay stressでは5400F通過したため、現時点ではcheckpoint範囲漏れより「window不足や診断条件差」の可能性が上がった。
- 実用FPSのgateは `-AllowJit`、no restore diff、scan30で見る。restore diff、scan1、InputNetplayTraceつき長時間runは診断stressとして分離する。

Next actions:

- `RollbackWindow=60` + targeted late delay/probeを別input routeへ広げ、ルート依存のspawn/despawnで状態が崩れないか確認する。
- 再発した場合だけ `MELONDS_NSML_ROLLBACK_NSMB_RESTORE_DIFF_TRACE=1` を短い範囲で入れ、未復元Main RAM pageか、Main RAM外のcore進行状態かを切り分ける。
- FPS測定はbaseline/rollbackを同じ `-AllowJit` 条件で並べ、診断traceの遅さを通常性能として扱わない。

## 2026-06-01 previous practical validation

現候補は引き続き `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200 + MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL=30`。軽量checkpointは約253KB、保存平均は0.39-0.46ms程度、1フレームresim付き復元は7.5-8.7ms程度で推移している。

Completed:

- 手動host/clientルートを5400フレームまで延長し、prediction probe modulo 10、stable-field比較、settle window 60で通過した。host最終は `bytesLast=253,427`, `saveAvgUs=394`, `restoreOps=3`, `restoreAvgUs=7,535`。client最終は `bytesLast=253,427`, `saveAvgUs=396`, `restoreOps=1`, `restoreAvgUs=7,657`。
- `nsmb_us_direct_mvl_both_different.inputs` をhost/client両方に使う別ルートで4200フレームを通過した。途中の `movingHazardX` 差分はsettle window内で収束した。hostは `restoreOps=4`, `restoreAvgUs=8,136`, `saveAvgUs=400`。
- 固定の1770->2220移動確認が別ルート検証の邪魔になるため、`scripts/run-nsmb-mvl-split-local-input-smoke.ps1` に `-SkipMovementProbe` を追加した。game-state同期比較とルート固有の移動probeを分離できる。
- star collectルートはrollbackあり/なしの両方でframe 5880に `playerActor0X` 差分が出たため、現時点ではrollback候補の復元漏れではなくルートまたは比較条件側のbaseline差分として扱う。
- stock touchルートはrollbackなしbaseline 2800フレーム、rollbackあり自然jitter 3200フレームを通過した。自然jitterでは `bytesLast=252,691`, `saveAvgUs=390-399`, `restoreOps=0`。
- stock touch + prediction probe modulo 10はframe 2610付近でmoving hazard差分を起こした。ログ上は強制probeが連続し、通常WANより厳しいstressになっている。診断用に `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_LIMIT` を追加し、強制prediction mismatch注入回数を上限付きにできるようにした。
- stock touch + prediction probe limit 1は3200フレームを通過した。hostは `restoreOps=2`, `restoreAvgUs=7,470`, `saveAvgUs=395`, `predProbe=1`。
- stock touch + seed固定 + prediction probe limit 6 のrestore diffで、frame 2220/2320復元時に `0x02095400` / `0x02095500` の未復元ページを確認した。`0x02095300` rangeを `0x300` に広げた後、同じseed固定2800フレームと非固定seed 3200フレームのstock touch + limit 6が通過した。非固定seed 3200ではhost側 `bytesLast=253,203`, `bytesMax=253,427`, `saveAvgUs=449`, `restoreOps=1`, `restoreAvgUs=7,573`。

Current blocker:

- 実用候補としてはかなり軽く、複数ルートの自然jitterとstock touchのlimit 6強制probeまで通るようになった。まだ全予測外れstressやより長いルートは未解決なので、実用gateと診断stressを分けて継続確認する。
- star collectルートはbaseline自体が同期比較に合っていないため、rollback検証用ルートとして使うには比較フィールドまたはルート期待値の再設計が必要。

Next actions:

- 追加した `0x02095300+0x300` が他ルートでも安定するか、5400フレーム級の既存ルートと別ルートで再確認する。
- 強制probe limitを段階的に上げ、次に未復元ページが出るか、Main RAM外の進行状態が問題になるかを切り分ける。
- 自然jitterでの長時間検証を増やしつつ、全予測外れstressは実用gateではなく診断stressとして扱う。

## 2026-06-01 latest rollback snapshot focus

ユーザー指示により、delay方式とのhybrid検討はいったん外し、軽いcheckpoint/snapshotとして現実的なrollback方式を実験している。

現在の最有力候補は `nsmbtinycore + MELONDS_NSML_ROLLBACK_NSMB_DELTA_DISCOVERED_RANGES=1 + MELONDS_NSML_ROLLBACK_TINY_CORE_FLAGS=0x200`。これはMain RAM全体や通常savestateではなく、NSMB向けMain RAM range snapshotに、CPU/timer/scheduler/DMA/IRQ/IPC/WRAMなどの小さいcore進行状態と、GPU3DのFIFO/matrix/pipeline/register系だけを足す方式。checkpoint sizeは最新range補強後で `252,915` bytesまでに収まり、`savestate` や `corelite` よりかなり実用寄り。

ただし、まだ完全な「ROM解析でactor/global構造を静的に確定した案D」ではない。現在のrange setは、coredelta/restore diffと実行時Main RAM観測で見つけたNSMB global/actor/heap周辺を使っている。ROMの関数・構造体・actor tableを本格的に逆引きして、必要状態を名前付き構造として確定する作業はまだ途中ではなく、これからの段階。

今回の追加実験では、`MELONDS_NSML_ROLLBACK_NSMB_SCAN_INTERVAL` を追加し、動的object/range探索結果を数十フレーム単位でキャッシュできるようにした。`scanInterval=30` では、`0x200` 候補のcheckpoint bytesは約 `253KB`、保存平均時間は約 `7.9ms` から約 `0.39-0.41ms` まで下がった。

実rollback検証用に `MELONDS_NSML_ROLLBACK_PREDICTION_PROBE_MODULO` も追加した。これはテスト時だけ予測remote inputを周期的に1bit外し、通常のprediction mismatch/resimulate経路を強制する。restore diffで見つけた未復元ページを追加し、game/global周辺とheap/object周辺を数KB補強した。

`scripts/run-nsmb-mvl-split-local-input-smoke.ps1` には `-IgnoreSpeculativeInputFields` とsettle window検索を追加した。rollback中のgame-state traceは、次フレームでresimulateされるspeculative input状態を含むことがあるため、入力保持/pressedフィールドとsettle後のactor/object/score比較を分けて評価できるようにした。

Verification:

- Build: `cmake --build --preset release-windows-x86_64 --parallel` passed.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-2600-20260601`: 2600-frame split local-input smoke passed. Client frame 2520: `bytesLast=247,355`, `saveAvgUs=391`, `scanInt=30`, `scanRefresh=55`, `scanCacheHits=1595`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-delayjitter-3600-20260601`: 3600-frame split local-input smoke passed. Client frame 3600: `bytesLast=247,355`, `saveAvgUs=393`, `scanRefresh=91`, `scanCacheHits=2639`, `mismatches=0`.
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-restoreprobe-2600-20260601`: 2600-frame smoke passed, but restore probe did not force restoreOps. その後、prediction probeで通常のmismatch/resimulate経路を強制できるようにした。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe30-extra-gameglobals-2600-20260601`: prediction probe modulo 30で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=248,287`, `saveAvgUs=395`, `restoreAvgUs=7,512`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe10-more-diff-ranges-2600-20260601`: prediction probe modulo 10で2600-frame game-state comparison passed。Host側 `restoreOps=2`, `resims=2`, `bytesLast=251,095`, `saveAvgUs=391`, `restoreAvgUs=7,786`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe10-stablefields-extra4-3600-20260601`: prediction probe modulo 10 + stable-field comparison + settle window 60で3600-frame game-state comparison passed。Host側 `restoreOps=3`, `resims=3`, `bytesLast=252,915`, `saveAvgUs=398`, `restoreAvgUs=7,474`。Client側 `restoreOps=1`, `resims=1`, `saveAvgUs=398`, `restoreAvgUs=7,518`。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe-all-more-diff-ranges-2600-20260601`: prediction probe modulo 1はframe 1890で `inputPlayer0Held` のspeculative差分によりwrapper比較が停止した。全予測外れstressは現実的WANより過剰だが、追加range探索用の負荷として残す。
- `logs/codex-rollback-nsmbtinycore-gpu3dlight-scan30-predprobe-all-stablefields-settlewindow-extra3-2600-20260601`: prediction probe modulo 1 + stable-field comparisonでもframe 1950でplayer位置差が残った。全予測外れはまだ未達。

Current blocker:

- 実用候補としてはかなり近づいたが、まだ「実行時delta-discovered range + 小さいemulator進行状態」。ROM解析でNSMB actor/global構造を確定している段階ではない。
- `scanInterval=30` は現ルートで成功しているが、spawn/despawnや別ルートでrangeが変わる場面の安全余裕は追加検証が必要。
- 全予測外れstressでは、入力保持フィールドなどspeculative状態の比較で止まる。実用評価では、通常WAN相当のprediction頻度、settle後のactor/object/score収束、体感カクつきの確認を分けて測る必要がある。

Next actions:

- prediction probe modulo 10程度を継続stressとして使い、別input routeやさらに長時間で `253KB / saveAvg 0.4ms / restoreAvg 7.5-8ms前後` が維持できるか測る。
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
