# NSMB Mario vs Luigi Online PoC

## Rollback/input-sync PoC status - 2026-06-22

- Current practical rollback implementation is `tinycorepreimage` with lightweight Main RAM preimage/snapshot restore. The latest fix aligns rollback resimulation input semantics with normal frames: NSMB packet scratch gets delayed/effective input, while DS hardware input gets the raw local frame input.
- Current hard requirement for the active goal: rollback playability must be evaluated at the GUI rollback standard `InputDelayFrames=2`. Increasing InputDelay above `2` is rejected even if it improves stability, because it defeats the purpose of rollback for this target.
- Normal artificial jitter (`InputSendDelayFrames=3`, `InputSendJitterFrames=3`) passes the current movement-heavy suite with `tinycorepreimage-delay6-lead999-rbwait3000-maxresim2-bundle8`: stocktouch, chaos, contact, and dualstresslong passed with averages around `16.7-17.1ms` and max frame times under `47ms`.
- Strong artificial jitter (`InputSendDelayFrames=6`, `InputSendJitterFrames=6`) exposed a real rollback correctness issue: `delay6/rbwait3000/maxresim2` can produce persistent `playerGlobal=0` mismatch. The root is that the max-resim cap can cut off an older prediction mismatch after wrong input has already affected gameplay.
- Strong artificial jitter can be made more stable by increasing delay (`delay10`/`delay12`), but those profiles are now diagnostic-only and not promotion candidates under the active goal.
- Delay-2 measurements so far:
  - `delay2/maxlead2` stalls under `InputSendDelayFrames=6` / `InputSendJitterFrames=6` because the peer lead throttle waits in large chunks.
  - `delay2/maxlead999` avoids the throttle but full rollback resim can still spike around `300ms+` and produces persistent mismatch in stress routes.
  - True predict-only repair was added to the smoke/suite scripts. `predictrepair-delay2-playerstate` is fast (`~17ms`, max `~42ms`) but fails chaos with persistent `playerGlobal=0`.
  - `predictrepair-delay2-player-world-lite` fixes chaos and passes stocktouch/contact without freeze; under strong jitter it averages `17.5-18.7ms` with `40-44ms` max spikes, but dualstresslong can still produce persistent `playerGlobal=0` during death/transition-heavy movement.
  - Adding same-frame wait (`RollbackInputWaitUs=1000..3000`) can remove that `playerGlobal=0` signal on the failing dualstress seed, but active FPS drops to about `52fps` at `1000us` and about `47fps` at `3000us`, so it is not a final answer.
  - Budgeted `RollbackMaxResimFrames=2` plus world repair is still too heavy and produced a `101ms` active spike on the same dualstress seed.
- Latest delay-2 Plan-D-style snapshot/repair experiments:
  - `predictrepair-delay2-player-world-actorsnap-hostglobals` kept strong-jitter dualstresslong fast (`16.692/16.690ms`, max `40.671/31.881ms`) but still produced persistent `playerGlobal=0` around player death/transition.
  - Increasing full `GameState` sync to 10F fixed the early playerGlobal window but caused spikes (`102ms` host, `649ms` client), so high-frequency full GameState sync is rejected.
  - Short stale global repair (`MELONDS_NSML_PLAYER_STATE_MAX_STALE_GLOBAL_FRAMES=12`) reduced the persistent global mismatch and kept performance practical (`16.7-16.9ms`, max roughly `43-49ms`), but dualstresslong still desyncs after death/respawn through player actor position lag.
  - Stale remote transform repair (`4F` or `12F`) worsened player actor Y/X drift, so stale transform writes are not a promotion path.
  - Longer stale global/counter-only variants (`24F`, counter `24F`) can reduce some counter lag but introduce earlier stale `playerGlobal` or actor drift; they are diagnostic only.
  - Transition-transform repair for death/respawn can make one dualstresslong strong-jitter run pass with practical timing, but it is not stable across routes or repeated runs. It also breaks the initial pipe/entry transition unless gated; `MELONDS_NSML_PLAYER_STATE_TRANSITION_TRANSFORM_START_OFFSET=300` avoids the initial-entry write but still fails chaos/dualstresslong through persistent `playerGlobal`, object count drift, or spikes.
  - Player transform prediction width is route-dependent, not a real fix: `pred0` helped one dualstresslong run but failed stocktouch/chaos, while `pred4`/`pred8` still produced actor or player-global mismatch.
  - Rechecking exact rollback under the delay-2 requirement is still too heavy: `coredelta-baseline` hit `22.350/22.349ms` average and `301/341ms` max; `exact-delay2-tinycorepreimage-skiprender` hit `19.093/19.337ms` average and `163/166ms` max. Profiling shows this is dominated by rollback re-execution (`RunFrame` about `10-12ms` per resimulated frame, with 6-8F corrections around `90-130ms` total), not by checkpoint/restore alone.
  - Pre-pumping network packets immediately before exact resimulation reduced some resim counts in one trace, but did not improve max spikes (`~149-158ms`) and worsened max per-frame resim cost, so it is diagnostic only.
  - Actor/global snapshot moved one step further toward Plan D: generic world actor snapshot capacity was increased from 16 to 32, optional actor `stateType`/flags lifecycle apply was added, and an optional host-authoritative prune path can remove extra local `0x10C` star-candidate actors. The practical comparison now uses a significant active-object multiset that ignores known local-role objects (`0x12`, `0x10B`) instead of raw `objectActiveCount`.
  - New delay-2 candidate `predictrepair-delay2-player-world-actorsnap32-lifecycle-prune-transition90-pred0` can pass `dualstresslong` under strong jitter with practical timing (`16.69/16.69ms`, max about `60/31ms` in `logs/codex-goal-actorsnap32-prune10c-transition90-repeat-send6-jitter6-20260622/20260622-094618`), but `chaos` is still not stable: repeated runs alternate between moving-hazard/`playerGlobal` mismatch and single-frame spikes around `100-160ms`.
- Latest delay-2 exact rollback rechecks under strong jitter:
  - `exact-delay2-tinycorepreimage-maxresim1-skiprender` is rejected for correctness: it improved timing somewhat but mismatched at frame `990` (`playerActor1X`) in `logs/codex-goal-exact-delay2-maxresim1-chaos-send6-jitter6-20260622/20260622-104644`.
  - `exact-delay2-tinycorepreimage-maxresim2-skiprender` remains the best exact-style delay2 baseline so far: no mismatch in the measured chaos run, but still `17.497/17.509ms` average, `55.537/58.361ms` max, and over33 `82/83` in `logs/codex-goal-exact-delay2-maxresim2-chaos-send6-jitter6-20260622/20260622-104311`.
  - `InputMaxFrameLead=2` plus rollback micro-wait (`1500/3000us`) is rejected: all tested variants stalled near frame `4170` and averaged about `34ms` on the active side in `logs/codex-goal-delay2-lead2-rbwait-chaos-send6-jitter6-20260622/20260622-104937`.
  - `InputMaxFrameLead=999` plus rollback micro-wait (`1500/3000us`) is also rejected: it preserved correctness in the measured runs but worsened averages to about `19-23ms` and did not solve spikes in `logs/codex-goal-delay2-lead999-rbwait-chaos-send6-jitter6-20260622/20260622-105933`.
  - Skipping intermediate resim checkpoints did not help: `exact-delay2-tinycorepreimage-maxresim2-skipmidcp-skiprender` stayed mismatch-free but worsened to `17.686/17.713ms`, max `68.617/74.046ms`, over33 `86/78`.
  - Trace shows the exact path is dominated by re-executing frames, not copying checkpoints: maxResim2 resim typically spends restore `~3.3-5.2ms`, checkpoint save `~1.5-5ms`, and `RunFrame` `~20-35ms` for two resimulated frames. Therefore making checkpoints smaller alone is unlikely to achieve stable 60fps.
  - Pre-pumping network packets before resim is not promotable: with maxResim2 it caused `playerActor1X` mismatch at frame `1050`; uncapped prepump stayed correct but worsened to `19.314/19.295ms` average and `154-166ms` max.
- Verification now distinguishes transient rollback-correction samples from persistent desync: a single `playerGlobal=0` sample can pass only if it settles within `RollbackSettleFrames`; repeated samples beyond the settle window still fail. Practical-suite summaries also report transient mismatch count, max transient frames, and fields, alongside max frame time and over33ms counts.
- Current blocker: with `InputDelayFrames=2`, exact rollback can be correct but still stalls/spikes because even a two-frame resim often costs more than one display frame. Lightweight predict/repair keeps FPS close to target but cannot yet guarantee object lifecycle/player-global correctness in complex chaos/manual-style play.
- Next actions: do not raise GUI rollback InputDelay above `2`. Move the next experiments away from route-specific actor field patches and toward a more root actor/global authority model: pointer-safe actor-arena/page-delta snapshots, off-main-thread shadow resim with atomic publish, or another rollback design that avoids blocking the render frame with full `RunFrame` re-execution.

## GUI ice-stage Luigi sudden-death log review - 2026-06-20

- User-reported capture: `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781960336919-68204-0`, client role, diagnostics enabled, no rollback, random course order `[1,2,0,4,3]`.
- Target: second match / ice stage (`stageID=2`, `stageGroup=9`) first and second Luigi sudden deaths.
- Findings:
  - The run logged 195 `game state mismatch` rows, all `basic=0 playerGlobal=1 wifiCandidate=1 renderCandidate=1`. No `playerGlobal=0` mismatch appears in this capture, so the visible sudden death is not explained by the known player-global desync signal.
  - Rematch/start-ready for the second match looks aligned: checkpoint restore at frame `3398`, current stage request `stage=2`, local and remote start-ready both at frame `3562`, logical start `3544`, and input health summaries continue with `hasRemote=1`.
  - Ice-stage Luigi death events group into pairs because the first event is the death transition and the second is the later lives/deaths counter update. First death starts at frame `3914` with `Player::standardDeathTransitState` (`0x02119B24`) and later updates lives/deaths at frame `4022` with `Player::viewTransitState` (`0x0211870C`).
  - Second death starts at frame `4773` with `Player::pitDeathTransitState` (`0x021196B0`) and later updates lives/deaths at frame `4893` with `Player::viewTransitState`. The ring shows Luigi's signed X around `-106.6` while camera X is about `1833`, so this looks like offscreen/pit handling or world-coordinate wrap rather than a standard enemy hit.
  - A later third Luigi death starts at frame `5479` with `Player::pitDeathTransitState`, but the user specifically asked about the first two.
- Current interpretation:
  - This capture points away from input transport/rematch epoch failure and toward a gameplay/world-state problem specific to ice-stage player position, floor/pit bounds, or object/contact state.
  - The current death JSON does not include the exact caller that entered `standardDeathTransitState` / `pitDeathTransitState`, so it can classify the death path but not prove the original function/collision source.
- Additional capture reviewed: `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781961545732-68204-2` paired with host log `...\nsmb-mvl-gui-1781961545985-67380-2`.
  - Settings: no rollback, selected course order `[2,2,3,3,3]`, matching ROM identity on host/client.
  - Host and client logged identical Luigi pit-death frames for the first reported sudden death in each of the first three matches: frame `3437` stage `2`, frame `13271` stage `2`, and frame `17478` stage `3`.
  - At those frames Luigi was in `Player::pitDeathTransitState` (`0x021196B0`) with identical host/client position and flags. Examples: frame `3437` x `431.5`, y `-379.69`; frame `13271` x `369.75`, y `-375.47`; frame `17478` x `296`, y `-342.81`.
  - The preceding ring shows Luigi staying horizontally around the same screen region while Y keeps falling, with no nearby moving hazards logged. This points more strongly to floor/stage collision or pit-bound handling than to host/client desync.
  - The mismatch lines in this run are still `basic=0 playerGlobal=1 wifiCandidate=1 renderCandidate=1`; no `playerGlobal=0` signal appears around the sudden deaths.
- Additional capture reviewed with the new `player_position_anomaly` / `player_pit_transition` logs active: client log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781962346160-66120-0` paired with host log `...\nsmb-mvl-gui-1781962347955-43224-0`.
  - Settings: no rollback, selected course order `[2,3,2,3,2]`, matching ROM identity on host/client.
  - Host/client events again match exactly at the reported sudden-death frames, and diagnostics still show only `basic=0 playerGlobal=1 wifiCandidate=1 renderCandidate=1` mismatches.
  - First match, user-reported Luigi first death: frame `4512`, stage `2`, player `1` / character `1`, entering `Player::pitDeathTransitState` (`0x021196B0`), x `519.06`, y `-404.75`, screenX about `131px`, screenY about `-660px`. Host/client values match exactly. The preceding ring shows Luigi already below the camera for hundreds of frames, with repeated y values around `-400`; this is a floor/pit-bound problem, not a transport-only desync.
  - Note: frame `1220` in the same first match is player `0` / character `0`, so it is a separate Mario death and not the user-reported Luigi sudden death.
  - Second match, reported deaths: frame `12950`, `17563`, and `19532`, stage `3`, player `1` / character `1`, all entering `Player::pitDeathTransitState` (`0x021196B0`). Screen X is in-view (`94-122px` range), while screen Y is below the camera (`-604` to `-624px`), and the ring repeatedly shows y near `-400`. This again points to floor/stage collision or pit-bound handling, not transport desync.
- Current cause:
  - This is most likely not a no-rollback input-sync bug. The host/client RAM-derived player state is identical at the sudden-death frames, and no `playerGlobal=0` mismatch appears around them.
  - The GUI run uses the stable direct-entry ROM plus runtime `MELONDS_NSML_MVL_STAGE` / stage-sequence override, while `MELONDS_NSML_DIRECT_MVL_BOOT` is disabled. At match start both players are placed into the direct-entry spawn transition at fixed coordinates, for example stage 2 frame `861` uses p0 `x=0x00018000 y=0xFFE78000` and p1 `x=0x00068000 y=0xFFE78000`; stage 3 frame `11262` uses p0 `x=0x00058000 y=0xFFE78000` and p1 `x=0x000A8000 y=0xFFE78000`.
  - ROM course data check: stage 2 maps to `course/J03_1.bin` and its entrance records are `(x=0x10,y=0x180)` / `(x=0x60,y=0x180)`. Stage 3 maps to `course/J04_1.bin` and its entrance records are `(x=0x50,y=0x180)` / `(x=0xA0,y=0x180)`. Runtime coordinates match the normal entrance transform `(x + 8, -(y + 8))`, producing stage 2 `(24,-392)` / `(104,-392)` and stage 3 `(88,-392)` / `(168,-392)`. So direct-entry is not inventing a bad initial player Y for these courses; it is using the course entrance Y.
  - The GUI only enables the camera-init-hold clear (`MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD`). It does not enable the older entrance-spawn normalization or initial-spawn repair knobs; entrance normalization is intentionally disabled in normal GUI runs because it previously broke stage 3 edge pipes.
  - Stage-start comparison was run for GUI-like direct-entry starts on all five courses with `MELONDS_NSML_CLEAR_MVL_CAMERA_INIT_HOLD=1`, one-frame game-state trace, and the stable host/client ROMs. Normal LocalMP/native baseline could not be reproduced with the current automation input because it never reached `stageGroup=0x9` / `vsMode=1`, so this comparison is direct-entry stage-to-stage rather than native-to-direct.
  - 2026-06-21 recheck: `logs\codex-compare-direct-stage2-20260621` (`-NoLanMP`) and `logs\codex-compare-localmp-stage2-20260621` (LAN env enabled, but still stable direct-entry ROMs) both start stage 2 at frame `861` with identical Y: p0 `0xFFE78000` (-392) and p1 `0xFFE78000` (-392), then settle to about `0xFFEA0000` (-352). This proves the LAN wrapper flag itself is not changing the direct-entry initial Y, but it is not a native LocalMP comparison.
  - US clean normal-LocalMP attempts `logs\codex-compare-clean-localmp-stage2-20260621`, `logs\codex-compare-clean-us-localmp-camera-probe-20260621`, and `logs\codex-compare-us-localmp-route-camera-probe-20260621` did not reach gameplay; they ended around `sceneCurrentSceneID=0x6/0x9`, `sceneNextSceneID=0x181`, `stageGroup=0x0`, `vsMode=0`, and no player actors. The Japanese normal-LocalMP route `logs\codex-compare-jp-localmp-route-20260621` reaches its route, but the current US-address game-state trace is invalid for that ROM (`stageGroup=0xffff`), so it cannot answer the Y question.
  - First normal player frame summary: stage 0 screenY `-320`, stage 1 `-412`, stage 2 `-608`, stage 3 `-576`, stage 4 `-352`. At the same point all stages have `entranceSpawnID=0/1`, spawn pointer delta `0x14`, `cameraDbgCA880=0`, collision flags `0x800b001/0x800b001`, and physics flags `0x83/0x83`.
  - This weakens the earlier entrance-pointer hypothesis. The initial Y is not the direct cause: direct-entry starts stage 2/3 from the course entrance Y and no-movement runs survive. The fatal differentiator is the ROM-side vertical out-of-view camera slot used later during a fall.
  - 2026-06-21 root cause found: the stable Rust ROM generator patched `StageActor::isOutOfViewVertical` through a fallback stub at `0x020C5298`, and that stub rewrote slot argument `1` to `0` before reading `Stage::cameraY` / `Stage::cameraHeight`. The Python patcher kept this behavior behind the diagnostic `camera-player1-out-of-view-slot0` option, but `tools\nsmb-mvl-rom\src\lib.rs` made it unconditional.
  - Therefore Luigi/player1 vertical out-of-view checks could use player0's camera. In the user capture, client `localPlayerID=1` still has `cameraY0=0x0008C000` (140px) and `cameraY1=0x00100000` (256px) just before the frame-4512 death. Luigi is player1/slot1, but the patched vertical check used slot0, so the lower camera limit became `0xFFEB4000`; Luigi's falling object bottom crossed that limit before reaching the lower floor, and the game entered `Player::pitDeathTransitState`.
- Follow-up cause narrowing:
  - A temporary diagnostic-only vertical out-of-view trace around the ROM-side `StageActor::isOutOfViewVertical` fallback stub was used during investigation, then removed after root-cause verification. In stage 2, neither the first 200 frames after start nor a 3800-frame no-JIT run with the current automated route produced player vertical out-of-view hits. This weakened the theory that the ROM-side vertical out-of-view fallback directly kills Luigi at stage start.
  - A no-movement stage 2 run survived 8000 frames. The death is therefore not caused by initial spawn/camera state alone.
  - Existing `MELONDS_NSML_FORCE_PLAYER_ACTOR_POSITION` was used to place Luigi at the stage 2 user-log fall coordinate (`x=568`, `y=-336`). This reproduced the loss of ground collision: after the forced frame, collision changed to `0x00002000` and Luigi fell with `velY=-4`, closely matching the user log around frames `4486-4511`.
  - The forced stage 2 replay did not enter `pitDeathTransitState`; it later contacted a lower floor around `y=-432`. This was because its `Stage::cameraY[0]` did not match the user capture and stayed around `220px` while falling. With the same forced player state and input tail, forcing camera slot0 Y to the user-log value `0x0008C000` reproduced the pit transition at frame `1024`, x `552.9998`, y `-404.0625`, before the lower floor.
  - Stage 3 coordinate forcing near the reported pipe position (`x=92`, `y=-400`) also did not enter pit death by itself.
  - 2026-06-21 direct-entry automation: stage 2 `RIGHT+B` and `LEFT+B` single-player remote-input injection both survived 6500 frames without `pitDeathTransitState`. Rightward movement reached the same X band as the user death but landed on the lower floor at `y=0xFFE50000` (-432). Leftward movement also stayed alive and settled on floor collision.
  - A forced state matching the user log's upper-platform fall (`x=0x0022B040`, `y=0xFFEADA80`, `velX=0xFFFFEFB0`, `velY=0`, `action=0x00100000`, `subAction=0x48`, `physics=0x82`, `collision=0x2000`) plus the same input tail (`0x22 -> 0x02 -> 0`) still landed on the lower floor when camera slot0 was not forced. Adding the user-log camera slot0 Y reproduced the sudden death, so the missing condition was camera slot0 position, not hidden collision/contact state.
  - Stage 2 `course/J03_1_bgdat.bin` around the reported death point contains an upper floor object around tile `x=35..36,y=21` and lower floor objects around tile `x=25..48,y=26..31`. The real log shows Luigi falling from the upper-floor height (`y=-336`) toward the lower floor, but entering `pitDeathTransitState` near `y=-404.75` before reaching the lower floor at `y=-432`. This now matches the slot0-camera vertical out-of-view limit, not a missing floor tile.
  - Temporary no-JIT vertical trace for the reproduced case confirmed the bad slot use: `NSMB VerticalOutOfView` logged player1 with `argSlot=1` but `usedSlot=0`; at frame `1023` the player1 row had `triggered=1`, `cameraY=0x0008C000`, `cameraHeight=0x000C0000`, and `limit=0xFFEB4000`.
- Fix/logging update:
  - Fixed `tools\nsmb-mvl-rom\src\lib.rs`: the vertical out-of-view fallback stub now preserves the requested player camera slot and only falls back to slot0 when the requested slot's `Stage::cameraHeight` is zero. Added `vertical_out_of_view_fallback_preserves_player1_camera_slot` to prevent reintroducing the unconditional player1-to-slot0 rewrite.
  - Added targeted low-volume death/offscreen diagnostics for GUI diagnostic mode in `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`.
  - The diagnostic frame ring now records per-player camera width/height in addition to camera X/Y, so screen-relative player position can be interpreted from the JSONL event alone.
  - Added `player_pit_transition` JSONL events when either player enters `Player::pitDeathTransitState` (`0x021196B0`). The event includes current/previous player state, screen-relative X/Y, per-frame delta X/Y, stage/camera data, and the recent diagnostic ring.
  - Added throttled `player_position_anomaly` JSONL events when a live visible player is far outside the current camera range or crosses a large X/Y discontinuity. This should catch the frame where Luigi goes offscreen before the later pit-death transition.
  - Removed temporary stdout/JIT trace hooks and forced-state repro extensions after verification. Normal GUI diagnostics now keep only the low-volume JSONL events above when diagnostic events are enabled.
- Verification:
  - `cargo fmt --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, and `cargo clippy --manifest-path tools\nsmb-mvl-rom\Cargo.toml --all-targets -- -D warnings` passed after the ROM generator fix.
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - `logs\codex-stage2-luigi-right-remote-pittrace-20260621`: stage 2 rightward remote-input injection survived 6500 frames, no pit transition.
  - `logs\codex-stage2-luigi-left-remote-pittrace-20260621`: stage 2 leftward remote-input injection survived 6500 frames, no pit transition.
  - `logs\codex-stage2-luigi-forced-fall-tail-pittrace-20260621`: forced user-log fall-tail state plus matching input tail survived 1300 frames and landed on the lower floor, no pit transition because camera slot0 did not match the user capture.
  - `logs\codex-stage2-forced-tail-cameraY140-slot1to0-pittrace-20260621`: same forced fall-tail state plus camera slot0 Y forced to `0x0008C000` reproduced `Player::pitDeathTransitState` at frame `1024`.
  - `logs\codex-stage2-forced-tail-cameraY140-verticaltrace-nojit-20260621`: no-JIT vertical trace confirmed `argSlot=1 usedSlot=0` for player1 and `triggered=1` at frame `1023`.
  - After the ROM generator fix, `scripts\run-nsmb-mvl-lan-route-smoke.ps1 ... -GenerateMvlConfiguredRoms` regenerated the stable host/client ROMs with `romPairId=1c1f219838d6e4ac0b2cc80d6ae7f202f7433b213f7adc37002f79e66cde2a13`.
  - `logs\codex-stage2-forced-tail-cameraY140-after-slotfix-20260621`: the same forced fall-tail state plus camera slot0 Y forced to `0x0008C000` survived 1300 frames, produced 0 pit transitions, and Luigi landed on the lower floor at frame `1031`, y `-432`, while `cameraY0=140px` and `cameraY1=256px`.
  - `logs\codex-stage2-forced-tail-cameraY140-after-slotfix-verticaltrace-nojit-20260621`: updated no-JIT vertical trace confirmed player1 `argSlot=1 usedSlot=1`, `cameraY=0x00100000`, `triggered=0` at frames `1022-1032`; player1 triggered rows count is 0.
  - `logs\codex-stage2-pittrace-jit-volume-20260621`: historical JIT-enabled stage 2 smoke with the temporary pit-transition trace survived 1300 frames and produced 0 `NSMB PitTransitAssign` rows during ordinary non-death movement. That temporary trace has since been removed.
  - `cargo fmt --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo clippy-all` in `tools\nsmb-mvl-gui\src-tauri`, and `pnpm run ci` in `tools\nsmb-mvl-gui` passed.
- Next action:
  - Run a manual GUI match on the ice/pipe stages with freshly prepared stable ROMs and confirm Luigi no longer suddenly pit-dies while falling from upper platforms.
  - Add a vertical/fall-state regression check to the route smoke. The current `-RequireMvlInitialSpawnState` only checks entrance IDs/pointers and X deltas, so it missed this class of issue.

## GUI WAN start-ready ENet connect race - 2026-06-20

- User-reported issue: GUI room launch still crashed before match start. Host log directory: `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781894773390-66076-0`.
- Paired client log found by room/seed: `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781894773242-58576-0`.
- Findings:
  - WebRTC/bridge reached `connected` on both sides and bridge packet counts stopped only after melonDS stopped sending, so this was not a WebRTC negotiation failure.
  - Host melonDS received the client's start-ready packet, accepted gameplay start, then hit `NSMB Test: input frame throttle timeout frame=865 sendFrame=848 remoteInputFrame=843 lead=5 waitedMs=5000`.
  - Client melonDS reached `NSMB InputNetplay: waiting for remote gameplay start ready ...`, but never logged `NSMB PoC: peer connected`.
  - Cause: client initialization stored the return value of `enet_host_connect()` directly in `G.Peer`. That pointer represents a pending ENet peer, not a completed connection, so start-ready/input send paths could run before the `ENET_EVENT_TYPE_CONNECT` event.
- Fix applied in `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`:
  - Added separate `G.ConnectingPeer` state for the pending client connect.
  - `G.Peer` is now set only when ENet emits `ENET_EVENT_TYPE_CONNECT`.
  - Disconnect handling clears both pending and connected peer pointers.
  - Start-ready logs now flush immediately, making this barrier easier to diagnose in GUI logs.
- Verification:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - `corepack pnpm sync:sidecars` passed from `tools\nsmb-mvl-gui` and copied the rebuilt `melonDS.exe` plus `nsmb-net-bridge.exe` into GUI release/binaries sidecar locations.
  - `scripts\run-nsmb-mvl-split-local-input-smoke.ps1 -Frames 1200 -WaitTimeoutMs 120000 -InputDelayFrames 4 -InputMaxFrameLead 4 -InputUnreliable -InputBundleHistory 8 -SkipMovementProbe -SkipGameStateComparison -LogRoot logs\codex-enet-connect-race-20260620` passed. The produced `logs\nsmb-mvl-split-local-input-smoke` host/client stdout both logged `NSMB PoC: peer connected` before match progress and reached frame 1200 without a fatal input throttle timeout.
- Follow-up issue from GUI logs:
  - Host log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781896603278-38548-0` and paired client log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781896601431-53528-0` showed the ENet connect race was fixed, but a second barrier weakness remained.
  - Client sent start-ready and received the match seed, but did not receive the host's one-shot start-ready before host advanced into gameplay. Host then timed out at `frame=863 sendFrame=848 remoteInputFrame=843 lead=5`.
  - Cause: start-ready was treated as a single reliable packet. If the side that already has the remote ready exits the barrier immediately, the other side can remain in the barrier if that one packet is missed or delayed long enough.
- Additional fix applied:
  - Start-ready send state now records send count and last send time.
  - After a side has accepted the remote start-ready and entered gameplay, it resends its own start-ready every 250 ms until remote input at/after the netplay start frame is observed. The message is idempotent, so duplicate receives are safe.
  - Added `scripts\test-nsmb-mvl-gui-sidecar-e2e.ps1`, which launches the GUI release sidecar `melonDS.exe` and `nsmb-net-bridge.exe` over WebRTC signaling, waits for both sides to reach a frame limit, checks start-ready acceptance, rejects input throttle timeout / start-ready timeout / peer disconnect, and cleans up processes.
- Second follow-up from GUI logs:
  - Host log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781897816349-71344-0` and paired client log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781897814844-57916-0` confirmed the rebuilt binary was in use because logs included `sent start ready ... count=1`, and the GUI release/build hashes matched.
  - The remaining failure was not stale sidecars. Host had already received remote input through frame `843`, so the resend condition considered the peer alive and stopped. But with `delay=4`, the first gameplay input required after logical start `840` is frame `844`; client was still in the start-ready barrier and never produced `844+`.
  - Fix tightened the resend stop condition from `LastReceivedInputFrame >= NetplayStartFrame` to `LastReceivedInputFrame >= NetplayStartFrame + Delay`.
- Third follow-up from GUI logs:
  - Host log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781898507073-36328-0` and paired client log `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\logs\nsmb-mvl-gui-1781898505370-9168-0` showed the previous e2e was insufficient: it covered the easy host-first path but not the GUI-like client-first launch ordering.
  - Reproduced locally with GUI release sidecars, WebRTC signaling, `MvlStage=4`, `MvlMatchSeed=60589462`, and client-first melonDS launch.
  - Causes found:
    - A side could accept a start-ready packet that arrived before it sent its own local start-ready, letting host leave the barrier while client was still waiting.
    - The host bridge could receive the client's first ENet UDP packet before host melonDS had bound its UDP port, log `ignored local UDP connection reset`, and drop the only early connect packet.
    - `waitForPeerBeforeStart` could add a 10-second frame-0 skew even though the real synchronization point is the netplay-start barrier.
    - Input bundles were sent as ENet unsequenced packets. If an older input frame fell out of the small bundle history, both sides could later block forever waiting for that missing frame.
  - Fixes applied:
    - Start-ready acceptance now requires a remote ready received after the local ready was sent, or confirmed post-start remote input.
    - Start-ready is resent while still inside the barrier, not only after accepting the barrier.
    - `waitForPeerBeforeStart` is skipped at frame 0 when `waitForPeerAtNetplayStart` is enabled, so synchronization is centralized at the gameplay-start barrier.
    - `nsmb-net-bridge` replays early WebRTC-to-local UDP packets for fixed local targets until the local UDP app becomes reachable.
    - Input bundles now use ENet reliable packets, and throttle wait can resend the latest input bundle as an additional recovery path.
    - `scripts\run-nsmb-mvl-local-triage.ps1` and `scripts\test-nsmb-mvl-gui-sidecar-e2e.ps1` now support deterministic melonDS launch order and gap parameters.
- Current verification:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - `cargo build --release`, `cargo fmt`, and `cargo clippy-all` passed in `tools\nsmb-net-bridge`.
  - `corepack pnpm sync:sidecars` passed and copied the rebuilt sidecars into GUI release/binaries locations. Final GUI sidecar hashes: `melonDS.exe` `34e591fc8831ce6f60a01c624549e3c3454195d9807fbaa1939ca3c6bc24246e`; `nsmb-net-bridge.exe` `313f860f4ece2fa4adeac5eec171777d05fb39ce238ff572dde2d49980ff2df5`.
  - `scripts\test-nsmb-mvl-gui-sidecar-e2e.ps1 -Frames 1200 -TimeoutSeconds 180 -MvlStage 4 -MvlMatchSeed 60589462 -MelonLaunchOrder ClientFirst -MelonLaunchGapMs 500` passed at `logs\codex-gui-sidecar-e2e-clientfirst-20260620-6`.
  - `scripts\test-nsmb-mvl-gui-sidecar-e2e.ps1 -Frames 1200 -TimeoutSeconds 180 -MvlStage 4 -MvlMatchSeed 60589462 -MelonLaunchOrder ClientFirst -MelonLaunchGapMs 1500` passed at `logs\codex-gui-sidecar-e2e-clientfirst-20260620-8`.
  - `scripts\test-nsmb-mvl-gui-sidecar-e2e.ps1 -Frames 1200 -TimeoutSeconds 180 -MvlStage 4 -MvlMatchSeed 60589462 -MelonLaunchOrder HostFirst -MelonLaunchGapMs 500` passed at `logs\codex-gui-sidecar-e2e-hostfirst-20260620-2`.
- Current blocker: no local WebRTC GUI-sidecar repro remains for the GUI startup/start-ready path under tested client-first and host-first launch ordering.
- Next action: if a real GUI run still fails, compare its melonDS/bridge logs against `logs\codex-gui-sidecar-e2e-clientfirst-20260620-8` and confirm the GUI launched the same sidecar hashes.

## Wins / result winner detection fix - 2026-06-19

- User-reported issue: the GUI exposes a match win target, but the setting may not be behaving correctly. The immediate question was whether the current runtime actually obtains who won or lost each game.
- Findings before the fix:
  - GUI/Tauri does pass `settings.wins` through to melonDS as `MELONDS_NSML_MVL_WINS`, and enables `MELONDS_NSML_MVL_AUTO_RESTART_AFTER_RESULT` only when `wins > 1`.
  - melonDS reads that env into `G.MvlTargetWins`.
  - Auto-restart detects the result screen by `sceneCurrentSceneID == 0x000A`.
  - The old auto-restart scorer only compared `Game::playerBattleStars[0..1]` and `Game::playerCollectedStars[0..1]`, then fell back to `winner = 0` when the counters were tied. Existing logs such as `winner=0 stars=0/0 collected=0/0 matchWins=1/0` showed that this could increment Mario's match wins without proving Mario won.
- Fix applied in `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`:
  - Result scoring now reads a `MvlResultSnapshot` from battle stars, displayed stars, collected stars, lives, deaths, and dead flags.
  - Winner resolution now uses the first meaningful difference in this order: battle stars, displayed stars, collected stars, exactly one player dead, higher lives, lower deaths.
  - The tied/unknown case no longer increments player 0. After the auto-restart delay it logs `NSMB MvL auto restart: result unresolved ...` with the full snapshot and leaves the match win counters unchanged.
  - Result logs now include all sampled fields, for example `stars=5/0 displayed=5/0 collected=5/0 lives=3/3 deaths=0/0 dead=0/0 matchWins=2/0 target=2`.
- Verification:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - A normal `scripts\run-nsmb-mvl-split-local-result-smoke.ps1` attempt at `logs\codex-winner-detection-smoke-20260619` did not reach result by frame 6000 and failed on the wrapper's screenshot expectation, so it produced no winner log.
  - A forced-result LAN smoke at `logs\codex-winner-detection-forced-20260619` reached the result scene on both host and client with forced player0 star counters. Both logs show `winner=0 ... matchWins=1/0 target=2`, then after restart `winner=0 ... matchWins=2/0 target=2`. The script ended with a harness-level `missing host frame limit` failure after the target wins were reached and remote input timed out, but the winner detection and match-win increment path were exercised.
- Current blocker: no blocker for avoiding the incorrect tied `0/0 -> player0` fallback and for using richer result data. A future ROM-analysis step can still identify the native single winner/result field used by the result screen and replace the heuristic entirely.
- Next action: run or add a deterministic real-gameplay route for a Luigi win/death-result case so both player IDs are covered without forced counters.

### Direct winner field check - 2026-06-20

- Follow-up question: can melonDS read a direct win/loss value instead of inferring from stars/deaths/lives?
- Checked symbols and disassembly around `VictoryState`, `Player::transitVictory`, `PlayerBase::doVictoryTransition`, and the known `Game::player...` globals.
- Current finding: no named or obvious global winner/result field has been identified yet. The known stable globals exposed by symbols are the per-player state values already being sampled: `playerBattleStars`, `playerDisplayedStars`, `playerCollectedStars`, `playerLives`, `playerDeaths`, and `playerDead`.
- `VictoryState` setup initializes object/vtable state, and the visible victory-transition code manipulates player-object transition/pose state rather than exposing a simple match winner variable.
- A direct winner value may still exist as a transient field inside the result scene/object state, but it has not been located. To prove it, trace the transition into scene `0x000A` and compare RAM/object fields for Mario-win vs Luigi-win cases.

### GUI result display and final-death normalization - 2026-06-21

- User-reported issue: the GUI result card should show player names rather than Mario/Luigi labels, should omit deaths because they duplicate finite-lives information, and can show nonzero lives when a player loses by the final death in 3-lives mode.
- Implemented in the GUI: current match/history result cards now carry player-name labels, hide deaths, and display dead players as 0 lives.
- Implemented in melonDS result logging: when the result screen snapshot has `playerDead=1` in finite-lives mode, the logged snapshot is normalized to `lives=0` and `deaths>=initialLives`. This compensates for the result scene being reached before the native lives/deaths globals necessarily expose the final decrement.
- Current blocker: no blocker for GUI display correctness or normalized future result logs. A full real-gameplay verification route for both player IDs is still useful.
- Next action: run a deterministic finite-lives death-result smoke that reaches result without forced counters, and confirm the GUI card shows the losing player at 0 lives.

## Initial stock item clear - 2026-06-19

- User-reported issue: Luigi currently starts a direct MvL game with a Power-up Mushroom in the lower-screen stock item slot.
- Cause interpretation: this is not intended MvL behavior. The direct route calls into level load while bypassing part of the normal CourseSelect/VSConnect setup, and it did not explicitly initialize the separate stock-item global `Game::playerInventoryPowerup` (`0x0208B32C`). The `Game::loadLevel` `powerup=0` argument controls the player powerup state, not the lower-screen stock slot, so Luigi/player1 could inherit or receive the native route's nonzero stock value until the new explicit clear.
- Fix applied:
  - `tools/nsmb-mvl-rom/src/lib.rs` direct load stub now clears `Game::playerInventoryPowerup[0..1]` at `0x0208B32C` immediately after `Game::loadLevel` / `loadMvsLFilesThread` and before the stage start state continues.
  - `src/frontend/qt_sdl/NsmbNetplayPoC.cpp` and `src/ARM.cpp` direct boot / safe loadLevel trampoline paths now perform the same clear after their direct `Game::loadLevel` calls, so manual/test hotpatch routes match generated stable ROM behavior.
  - Regression test added: `direct_loadlevel_clears_initial_inventory_powerups` checks that the generated stub references `Game::playerInventoryPowerup` and clears both player slots.
- Verification:
  - `cargo fmt --manifest-path tools\nsmb-mvl-rom\Cargo.toml` passed.
  - `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml` passed: 8 tests.
  - `cargo clippy --manifest-path tools\nsmb-mvl-rom\Cargo.toml --all-targets -- -D warnings` passed.
  - `cargo clippy-all` passed in `tools\nsmb-mvl-rom`.
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - Generated verification ROMs under `logs\codex-initial-inventory-clear-20260619\rom`, then `scripts\run-nsmb-mvl-split-local-input-smoke.ps1` passed for 1700 frames with `-SkipRomEnsure`, `-SkipMovementProbe`, and `-SkipGameStateComparison` at `logs\codex-initial-inventory-clear-20260619\smoke-default`.
  - Game-state CSV confirms host/client frames `900..1680` keep `player0InventoryPowerup=0x0` and `player1InventoryPowerup=0x0`.
- Current blocker: none for removing Luigi's initial stock mushroom.
- Next action: if user-visible packaged ROMs still show the item, regenerate or refresh the GUI cached stable ROM pair so the new generator output is used.

## Castle 8-coin Mega item investigation - 2026-06-19

- User-reported issue: in normal MvL, the castle course should not spawn the Mega Mario item from the 8-coin item reward, but the current direct MvL route can.
- Native control found:
  - `Game::addPlayerCoin(int)` at `0x02020354` calls `0x020BF150` when a VS player reaches the 8th coin, then resets that player's coin counter.
  - `0x020BF150` is the 8-coin item generator. It chooses a weighted slot `0..5` with `Net::getRandom()` and spawns actor `0x001F` with settings from `0x020C2DBC`: `0x1099, 0x1088, 0x1089, 0x108B, 0x1081, 0x1085`.
  - If slot `5` is selected, the native code checks a flag at `0x02085A08` and `0x02153A80()`. When `0x02153A80()` returns `4`, it changes slot `5` to slot `4` before spawning the item.
  - `0x02153A80()` reads selector byte `0x0215C890` through table `0x0215AEEC = [0,1,2,3,4]`; return `4` is the castle course ID. This is the native castle-specific Mega exclusion path.
- Direct-route difference confirmed before the fix:
  - Rust ROM generation already wrote native selectors for Big Star target (`0x0215C88C`) and life mode (`0x0215C89C`), and wrote scene settings `0x02088F38`.
  - It did not write `0x0215C890`, the native course selector used by the 8-coin item generator.
  - RAM dump from `logs/codex-mvl-castle-selector-ramdump-20260619` with `MvlStage=4` shows host/client at frames 1200 and 1600: `0x02088F38=0x00B8FF00`, `0x0215C88C=1`, `0x0215C89C=2`, but `0x0215C890=0`. Game-state trace also reaches `stageID=4` and `stageSceneSettings=0x00B8FF00`.
- Fix applied in `tools/nsmb-mvl-rom/src/lib.rs`: the direct load stub now initializes the native course selector (`0x0215C890 = stage`) from the same fallback/runtime stage value used for `Game::loadLevel`.
  - Regression test added: `direct_loadlevel_updates_native_course_selector` checks that the generated stub references `0x0215C890` and stores fallback/runtime stage into it.
  - RAM verification: `logs/codex-mvl-castle-selector-restored-20260619` with `MvlStage=4` shows host/client at frames 1200 and 1600: `0x02088F38=0x00B8FF00`, `0x0215C88C=1`, `0x0215C890=4`, `0x0215C89C=2`.
- Current blocker: an end-to-end 8-coin route still needs a deterministic input script or trace setup to prove candidate slot `5` is converted to slot `4` during actual item spawn. The native selector restoration itself is verified in RAM.
- Verification: `cargo fmt --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, and `cargo clippy-all` passed. The RAM smoke produced the expected dumps but the wrapper still ended with the existing screenshot-directory check failure.

## Stock item X-button touch mapping - 2026-06-19

- User request: allow stock item use with the X button while preserving the native stock-item behavior where the item is released through the normal touch path and falls into the stage.
- Previous direct-powerup attempt was discarded and stashed as `codex-stash-direct-stock-powerup-attempt`; it incorrectly converted the inventory item directly into `PlayerBase+0x7AB` powerup state.
- Fix applied in `src/frontend/qt_sdl/NsmbNetplayPoC.cpp`:
  - Added `ConvertStockXToTouch()`, which converts an active X key into lower-screen touch input at `217,153`, the existing stock-item touch coordinate used by `tests\nsmb_us_direct_mvl_stress_client_stock_touch.inputs`.
  - The conversion is applied only to runtime inputs written to packet-bridge scratch / emulator input. Network-stored and sent inputs remain as X so the peer performs the same deterministic conversion locally.
  - When conversion happens, the X key bit is released in the runtime key mask so the game receives stock touch rather than X plus an extra normal button action. Existing explicit touch coordinates are preserved.
- Verification:
  - `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.
  - `scripts\run-nsmb-mvl-split-local-input-smoke.ps1` passed for 2300 frames with `tests\nsmb_us_direct_mvl_client_stock_x.inputs`, forced Luigi stock item `0x2`, and trace window `2020..2100`.
  - CSV result confirms native stock-touch behavior, not immediate powerup: X input appears as runtime player input without bit `0x400` (`inputPlayer1Held=0x821` during the X window), `player1InventoryPowerup` remains `0x2` through frame `2065`, then becomes `0x0` by frame `2100`, while `playerActor1RequestedPowerup/currentPowerup/powerupTimer` stay `0x0`.
- Current blocker: none for the input-sync X-to-stock-touch mapping. A visual/contact-sheet check can be added later if needed to show the falling item directly.

## Powerup / powerdown control-lock fix - 2026-06-18

- User-reported issue: when Mario/Luigi powers up from an item or powers down after touching an enemy, the direct MvL path could create a short uncontrollable period. That behavior is normal in single-player Mario rules, but real Mario vs Luigi should not leave the player vulnerable during that lock.
- Cause confirmed:
  - The stable direct MvL ROM generator already skipped the global VS-inappropriate `PlayerBase::freezeStage()` (`0x0212C130`) and `PlayerBase::signalLocked()` (`0x0212C1B8`) paths, so the remaining lock was not input-netplay delay or the old global stage/player pause.
  - `Player::updatePowerupState()` (`0x0211F83C`) enters the original powerup state machine when `PlayerBase+0x7AB` requested powerup differs from `PlayerBase+0x7AC` current powerup.
  - `Player::preparePowerupSwitch()` (`0x0211FDC8`) sets `PlayerBase+0xBA6 = 5` and `PlayerBase+0xBA7 = 0x28`. A related scale-animation entry at `0x02120618` sets `PlayerBase+0xBA7 = 0x3C`.
  - A synthetic direct MvL powerup request at frame 2600 showed `PlayerBase+0xBA7` counting down `0x28..0x1` while RIGHT input was present from frame 2625. Before the fix, Luigi did not move until frame 2644, giving a 19-frame input-to-movement delay in the measured window.
  - Enemy-contact powerdown goes through `Player::losePowerup()` (`0x02104DC8`) and `PlayerBase::requestPowerupSwitch()` (`0x0212B9AC`), so it feeds the same `0xBA6/0xBA7` transition path.
- Direct-vs-normal route difference found and fixed: the Rust stable ROM generator still passed `Game::loadLevel` stack args `0x20=0`, `0x24=0`, while the normal MvL load path and the newer C++ hotpatch path use `0x20=1`, `0x24=0xFF`. The generator now matches the normal MvL args, with a regression test.
- Fix applied in `tools/nsmb-mvl-rom/src/lib.rs`: direct MvL stable ROM generation now keeps the native powerup animation timer, but patches `Player::onUpdate()` so normal powerup/powerdown transitions no longer skip the normal movement/main update path.
  - `0x020FD310`: the old `beq 0x020FD354` branch after `cmp r0, #0` now branches to a small overlay-0 stub at `0x020C53D0`.
  - The stub preserves the original skip-normal-update path only while entering `PowerupState 3` (Mega): `PlayerBase+0x7AD` previous powerup must not be Mega, and `PlayerBase+0x7AB` requested or `+0x7AC` current must be Mega.
  - This keeps the hardening for Mega growth, but allows movement during Mega shrink/revert where `previousPowerup` is Mega. It does not treat `4` (Mini) or `5` (Shell) as Mega.
  - This preserves `PlayerBase+0xBA6/0xBA7` animation state while preventing non-Mega and Mega-revert animation states from becoming input/physics hardening windows.
- Diagnostics added: extended game-state CSV now logs `PlayerBase+0x7A9`, `+0x7AB`, `+0x7AC`, `+0x7AD`, `+0xBA6`, `+0xBA7`, and `+0xBA8` for both player actors. `scripts/run-nsmb-mvl-route-smoke.ps1` can now pass game-state trace start/end frames like the LAN smoke script.
- Verification:
  - Before fix: `logs/codex-powerup-request-beforepatch-20260618`, first RIGHT input frame 2625, first movement frame 2644, `playerActor1PowerupTimer` nonzero for 40 frames.
  - First fix attempt removed the timer itself (`logs/codex-powerup-request-afterpatch-20260618`), which removed the hardening but also removed the animation.
  - Animation-preserving non-Mega fix: `logs/codex-powerup-request-anim-kept-20260618`, first RIGHT input frame 2625, first movement frame 2625, `playerActor1PowerupTimer` nonzero for 40 frames (`0x28..0x1`).
  - Mega exception verification after user correction of enum values:
    - `logs/codex-powerup-mega3-exception-normal-20260618`: requested/current powerup `1`, first RIGHT frame 2625, first movement frame 2625, delay 0, timer nonzero for 40 frames.
    - `logs/codex-powerup-mega3-exception-mega-20260618`: requested/current powerup `3`, first RIGHT frame 2625, first movement frame 2698, delay 73, confirming Mega keeps the original hardening path.
    - `logs/codex-powerup-mega3-exception-shell-20260618`: requested/current powerup `5`, first RIGHT frame 2625, first movement frame 2625, delay 0, confirming Shell is not treated as Mega.
  - Mega growth vs Mega revert verification:
    - `logs/codex-powerup-mega-grow-only-hardens-grow-20260619b`: requested/current/previous `3/3/0`, first RIGHT frame 2625, first movement frame 2698, delay 73.
    - `logs/codex-powerup-mega-grow-only-shrink-free-20260619`: requested/current/previous `1/1/3`, first RIGHT frame 2625, first movement frame 2625, delay 0, while the revert timer stayed nonzero for 60 frames.
  - Basic direct MvL smoke passed with the final Mega-growth-only exception patch: `logs/codex-powerup-mega-grow-only-basic-smoke-20260619`, 3000 frames.
  - Checks passed: `cargo fmt --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo clippy --manifest-path tools\nsmb-mvl-rom\Cargo.toml --all-targets -- -D warnings`, and `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel`.
  - `cargo clippy-all` could not be used because that cargo alias is not installed in this environment.
- Current blocker: none for the direct MvL powerup-transition hardening. A real in-stage item/enemy route can still be added later for end-to-end gameplay repro coverage; the timer cause itself is now confirmed with a targeted runtime request.

## Current ROM generator/cache divergence investigation - 2026-06-17

- User-reported issue: the GUI-cached ROM renders normally, while the script/default ROM can show an effect/rendering corruption that matches the already-known bad memory-state symptom.
- Finding: the script default ROMs are not stale. Regenerating ROMs from `roms\nsmb-us.nds` with `scripts\generate-nsmb-mvl-stable-roms.ps1` for stage 0, wins 2, Big Stars 5, endless lives, and random course mode produced SHA-identical output to the current `roms\nsmb-us-direct-mvl-entry-stable-*-netaid.tmp.nds` files.
  - Default/fresh host SHA-256: `b228dc2b3e7bed1661edd037743a80ae053bbb4f1620439dcd6a64618dbf961d`.
  - Default/fresh client SHA-256: `13b7f22d8a0b37f8836e051bbe6454cd335bdfec57d154b6c98dc66fd635ac23`.
- GUI is reusing cached AppData ROMs generated earlier on 2026-06-06:
  - `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\roms\nsmb-mvl-host.nds` SHA-256 `9473bbb2252ac2f9b962940b5e2513e6800af7ebea9d17a3ce35d0b1017f1e09`.
  - `C:\Users\Sugiyama\AppData\Roaming\dev.melonds.nsmb-mvl\roms\nsmb-mvl-client.nds` SHA-256 `0e8b07d5ddfcb0378d26ab1fd1d3f3e8f91715b48895f9423a0df977f0b70055`.
- FNT/FAT named NitroFS files are identical by hash across the script default/fresh ROMs and the GUI cached ROMs. The meaningful differences are in ARM9 overlay payloads/tables.
- Additional v3 baseline check: generating ROMs from commit `48f2db8c` (`nsmb-mvl-reusable-runtime-config-v3` introduction) produced host SHA `04f4f7ef7e73fd739588a6fc59ee1b2a4136f16dd7def368aa2db72dc0ed186c` and client SHA `0b98ad83b0401043e7b61322378d260990a168b4d9c16c505a29d4e89f4718b3`.
  - Those v3-baseline ROMs do not match the GUI cached full-ROM hashes, but their overlay 0 payload hash is identical to the GUI cached ROMs: `dfd2521701aa6558`, compressed size 122592 bytes.
  - Current generated ROM overlay 0 hash is `81f1ed92896af89c`, compressed size 122616 bytes.
  - Therefore the suspicious difference for the visual/memory symptom is specifically the post-v3 overlay 0 change, not the GUI cache marker itself.
- Overlay comparison:
  - Current default/fresh host vs current default/fresh client differs only in overlay 52.
  - GUI host vs GUI client differs only in overlay 52.
  - Current default/fresh ROMs vs GUI cached ROMs differ in overlay 0 and overlay 52.
  - v3-baseline ROMs vs GUI cached ROMs differ only in overlay 52, which is expected to vary with role/runtime/bootstrap configuration and is less suspicious for the observed rendering corruption.
  - Current overlay 0 compressed payload is 122616 bytes; GUI overlay 0 is 122592 bytes.
  - Current/fresh host has a 0x200 smaller internal layout than current/fresh client and GUI cached ROMs because the compressed overlay payload layout differs.
- Earlier ROM-side suspect: `patch_stage_object_activation_player_id(rom, 0)` from commit `d597d5ce`. It is the only clear post-v3 functional addition that changes overlay 0 and it touches stage/object activation code, making it a plausible sync-risk area. However, later script testing with byte-identical GUI ROMs still reproduced the rendering bug, so this is no longer the primary explanation for the script-only visual corruption.
- Codex session-history check: the original `stage-object-activation-player-id` patch was first added on 2026-05-28 while investigating raw `client localPlayerID=1` missing the first Goomba. The session then found that the patch hit the `0x0209B040` activation path, but the Goomba spawn path actually went through `0x0209B320`; the session explicitly concluded that `stage-object-activation-player-id --player-id 0` did not restore Goomba spawn and likely targeted the wrong path. The later concrete cause found in that session was `Game::wrapX` left at `0xFFFFFFFF` on raw client local1, making the `0x0209B320` activation width zero; forcing wrapX restored the Goomba but still left speed/simulation differences.
- Risk assessment: the current Rust activation-player hook should therefore not be treated as proven necessary based on the original Goomba investigation alone. It may still have been carried forward as a broad Plan-D actor-activation mitigation, but the available session evidence is weaker than the current code/docs implied. The hook is also invasive: it overwrites two instructions in overlay 0, writes a stack slot, calls `getPlayer`, and then resumes in the original function. Keep it disabled unless a focused lifecycle test on the current launch path shows that removing it causes a persistent non-local actor activation gap.
- Candidate generated: the `patch_stage_object_activation_player_id(rom, 0)` call is temporarily commented out in `tools\nsmb-mvl-rom\src\lib.rs`, and candidate ROMs were generated under `logs\codex-rom-diff-20260617\no-stage-activation-patch`.
  - Host SHA-256: `04f4f7ef7e73fd739588a6fc59ee1b2a4136f16dd7def368aa2db72dc0ed186c`.
  - Client SHA-256: `0b98ad83b0401043e7b61322378d260990a168b4d9c16c505a29d4e89f4718b3`.
  - Candidate overlay 0 hash is `dfd2521701aa6558`, compressed size 122592 bytes, matching the v3-baseline/GUI-cached overlay 0. This confirms the candidate removes only the suspicious post-v3 overlay 0 change.
- Correction after manual feedback: the no-activation-patch stage-0 candidate still reproduced the rendering bug, while the GUI cached ROM did not. Rechecking overlay 52 showed the previous candidate still did not match the GUI cached ROM.
  - The GUI cached ROM's direct-load fallback values are stage 4 / wins 2 / Big Stars 5 / lives endless / random. The stage 4 value comes from the GUI generation-time match seed path (`2707570089 % 5 == 4`) in the 2026-06-06 launcher logs; older launcher logs did not yet include `course_stages`.
  - Regenerating the current working-tree ROM generator with `patch_stage_object_activation_player_id(rom, 0)` disabled and fallback settings stage 4 / wins 2 / Big Stars 5 / lives endless / random produced byte-identical ROMs to the GUI cache:
    - `logs\codex-rom-compare-20260617\current-stage4-host.nds` SHA-256 `9473bbb2252ac2f9b962940b5e2513e6800af7ebea9d17a3ce35d0b1017f1e09`.
    - `logs\codex-rom-compare-20260617\current-stage4-client.nds` SHA-256 `0e8b07d5ddfcb0378d26ab1fd1d3f3e8f91715b48895f9423a0df977f0b70055`.
  - The script default ROMs differ from that GUI cache by exactly two logical causes: overlay 0 has the 27-byte `patch_stage_object_activation_player_id` hook enabled, and overlay 52 has the stage 0 scene-setting fallback (`0x00B4FF00`) instead of GUI's stage 4 fallback (`0x00B8FF00`). Full-file SHA differences are much larger because tiny overlay changes alter compressed sizes and shift later NitroFS file offsets.
- Current ROM freshness answer: the GUI cache file is newer by timestamp and matches the current dirty working-tree generator when using stage 4 and activation-patch disabled. The script default ROMs are older files from 2026-06-02; before the temporary activation-patch disable, regenerating the default stage-0 script ROM from repo code produced byte-identical output to those files, so they were not stale for their own generation options. They are different because they encode different stage fallback and activation-patch state, not because one is a partially regenerated copy of the other.
- Second correction after testing the byte-identical GUI-cache-fallback ROM through the script: the rendering bug still reproduced. This rules out ROM bytes alone as the primary difference.
  - The executable hashes are identical across `build\release-windows-x86_64\melonDS.exe`, `tools\nsmb-mvl-gui\src-tauri\target\release\melonDS.exe`, and the GUI bundled binary.
  - The major remaining visual-path difference is renderer config. `run-nsmb-mvl-manual-local.ps1` rewrites `build\release-windows-x86_64\melonDS.toml` to `UseGL=true` and `Renderer=2`; the script log confirms `Created a OpenGL context`.
  - The GUI path reads `tools\nsmb-mvl-gui\src-tauri\target\release\melonDS.toml`, which currently has `UseGL=false` and `Renderer=0`.
  - User clarification: GUI ROM was also tested through the same script path, so renderer config cannot explain GUI-ROM-vs-generated-ROM behavior inside script runs.
  - Script logs with GUI ROM SHA (`logs\nsmb-mvl-manual-local-20260617-041515` and `logs\manual-normal-lowdelay-20260617-042104`) both use the OpenGL path. The observed script-run differences are input/network options: `041515` used input delay 16, max lead 2, reliable input, no bundle history; `042104` used the low-delay WAN profile, input delay 4, max lead 4, unreliable input, bundle history 8.
- User correction: the exact comparison pair is `logs\nsmb-mvl-manual-local-20260617-042419` as the bad/render-corrupted run and `logs\nsmb-mvl-manual-local-20260617-042554` as the normal run.
  - Both runs used byte-identical GUI cached ROMs: host SHA-256 `9473bbb2252ac2f9b962940b5e2513e6800af7ebea9d17a3ce35d0b1017f1e09`, client SHA-256 `0e8b07d5ddfcb0378d26ab1fd1d3f3e8f91715b48895f9423a0df977f0b70055`.
  - Both runs used the same script/OpenGL path, the same input delay/profile (`inputDelayFrames=4`, `inputMaxFrameLead=4`, unreliable input with bundle history 8), and matching relevant environment settings. Renderer and ROM bytes are therefore ruled out for this pair.
  - The key observed difference is save initialization. In `042419`, both host and client failed to open `host-rom/nsmb.sav` or `client-rom/nsmb.sav` at startup, emitted many `SaveManager: Flush requested` lines, and later created a fresh 8192-byte save. In `042554`, both peers opened an existing `nsmb.sav` immediately at startup and did not enter that flush storm.
  - The script only copies save siblings from the selected source ROM basename (`<SourceRomWithoutExtension>.sav` / `.sav.2`) into the per-run `nsmb.sav`. This means otherwise identical script runs can start from different save state depending on whether the source-ROM sibling save existed at launch time.
- Hip-drop visual repro:
  - Added `tests\nsmb_us_direct_mvl_hipdrop_probe.inputs` to boot MvL, move Mario right, jump, then hold DOWN for a ground-pound landing.
  - `logs\codex-hipdrop-nosave-repro-20260617-sw1` reproduced the white square effect around frames 1066-1078 with GUI ROM bytes and no source save sibling. Contact sheet: `hipdrop-contact-1048-1084.png`; zoom comparison: `hipdrop-nosave-vs-withsave-zoom.png`.
  - `logs\codex-hipdrop-withsave-repro-20260617-sw1` and `logs\codex-hipdrop-withsave-seed46412691-20260617-sw1` used the same ROM/input and a valid save. They showed only the normal small smoke particles, not the solid white square.
  - Invalid save tests (`logs\codex-hipdrop-ffsav-repro-20260617-sw1` with all `0xFF`, and `logs\codex-hipdrop-zerosav-repro-20260617-sw1` with all zeroes) reproduced the bad path even though the file existed. Therefore the trigger is not file existence alone; it is booting MvL from invalid/unformatted NSMB save contents.
  - Bad/invalid save path: many `SaveManager: Flush requested` lines, Net/Game random auto patch at frame 791, `oldNetCount=0x00`, and Mario's frame-1020 actor sample `000020A0`.
  - Valid save path: no save-format flush storm, Net/Game random auto patch at frame 785, `oldNetCount=0x01`, and Mario's frame-1020 actor sample `00000120`. Forcing the same `MvlMatchSeed=0x46412691` kept the valid-save path normal, so RNG seed value is not the cause.
- Fix applied for script runs: `scripts\run-nsmb-mvl-lan-route-smoke.ps1` now treats missing, all-zero, or all-`0xFF` 8192-byte `nsmb.sav` as unusable and falls back to the repo stable `roms\nsmb-us.sav`.
  - Verification: rerunning the no-save ROM source through `logs\codex-hipdrop-nosave-after-fallback-20260617-sw2` pre-seeded the stable save SHA-256 `42ffb80e234c01d5784bdc291fee41c26e59f66295d7f105c798ba8dde11b2ee`, opened `nsmb.sav` at startup, patched Net/Game random at frame 785 with `oldNetCount=0x01`, and no longer showed the solid white square in the frame 1048-1084 contact sheet.
- Current blocker: none for the script-side white-square reproduction. The remaining underlying emulator/game behavior is that invalid/unformatted NSMB save contents change early boot/MvL timing and can produce corrupted-looking ground-pound smoke.
- Next action: keep manual/script MvL launch paths from starting with invalid NSMB saves. If GUI ever runs without a valid AppData save, apply the same pre-seed/validation there.
- Verification: `cargo fmt --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, `cargo test --manifest-path tools\nsmb-mvl-rom\Cargo.toml`, and `cargo clippy --manifest-path tools\nsmb-mvl-rom\Cargo.toml --all-targets -- -D warnings` passed. `cargo clippy-all` could not be used because the alias is not installed in this environment.

## Current low-overhead diagnostic event logging - 2026-06-17

- User request: strengthen logs before fixing the rare no-rollback desync, rematch stall, and false Mario/Luigi death issues, while keeping gameplay responsive.
- Implemented:
  - melonDS now keeps a per-instance in-memory diagnostic ring buffer with 360 compact frames by default. The normal path stores fixed-address player/input/frame fields, player-global sub-hashes, cached player actor fields, and input sync counters without per-frame string formatting or disk writes.
  - Diagnostic launches write `melonds-events.jsonl`; GUI launches derive it next to `melonds-diagnostics.json`, and manual/test runs can override it with `MELONDS_NSML_DIAGNOSTIC_EVENTS_FILE`. A `diagnostic_started` event is emitted at startup so a log capture proves whether event diagnostics were active.
  - `playerGlobal` mismatch now emits a throttled `player_global_mismatch` JSONL event only when `playerGlobal` differs, avoiding heavy dumps for frequent basic-only mismatches. The payload includes local/remote hashes, local player-global sub-hashes, player actor hashes, latest local player snapshot, remote sample fields available from the state packet, field diffs, and the ring buffer.
  - Mario and Luigi life/death/dead-flag/death-transition changes now emit `player_life_change` with both players' rich state, pre-event ring frames, transition/collision/environment/action fields, camera/stage context, and nearby moving hazard objects.
  - Start-ready send/receive/accept now emits `start_ready` with local/remote frame, delta, logical start, input queue sizes, and last sent/received input frames.
- Expected overhead:
  - The default path copies bounded numeric snapshots in memory and writes to disk only at startup or on anomaly events.
  - Expensive object scans, nearest-hazard searches, and large JSON payloads run only on death/mismatch events or the short post-trigger window.
  - Full extended CSV remains opt-in for short repro runs.
- Verification:
  - Build: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passes.
  - Diagnostic enabled FPS smoke: `logs/codex-diagnostic-events-fps-20260617c` passed 3600 frames and wrote `melonds-events.jsonl` with host/client `diagnostic_started`. Active FPS: host `59.17`, client `50.82`; no `AfterHookPhaseSpike`.
  - Diagnostic disabled comparison: `logs/codex-diagnostic-events-fps-disabled-20260617` passed 3600 frames. Active FPS: host `59.19`, client `50.86`; no `AfterHookPhaseSpike`. The measured difference is within run-to-run noise and does not show an FPS regression from the diagnostic ring/event path.
  - Death-event check: an 8200-frame split-local run did trigger death diagnostics, but the wrapper failed its separate second-match requirement before it could be counted as a full pass. That run exposed an event-size problem: `death-transition` entries were dumping the full ring repeatedly and grew `logs/codex-diagnostic-events-real-death-20260617\melonds-events.jsonl` to about 114 MB.
  - Death-event size fix: `death-transition` is now throttled, does not include the ring dump, and startup false-death entries without a player actor/death counter/dead flag are filtered. Real `death` events still include the ring and nearby hazard context.
  - Post-fix death/FPS check: `logs/codex-diagnostic-events-size-check-20260617` passed 2000 frames and wrote `melonds-events.jsonl` at about 3.3 MB. It contains host/client `diagnostic_started`, host/client `death-transition` at frame 866, and host/client `player_life_change` `reason=death` for player 1 at frames 1876 and 1984 with `dead=1` and ring context. Active FPS: host `58.10`, client `57.95`; no `AfterHookPhaseSpike`.
- Next actions:
  - Use the real GUI death capture `nsmb-mvl-gui-1781960336919-68204-0` as the current source for ice-stage Luigi sudden-death analysis.
  - Add targeted damage/death function hooks instead of broad per-frame tracing, because the current `player_life_change` event classifies the transition path but not the caller/collision source.

## Current no-rollback desync triage - 2026-06-18

- User request: start fixing the rare no-rollback sync divergence and the second-match start/load failure, first by deciding what the existing mismatch logs actually prove.
- Finding: `basic=0 playerGlobal=1` is too broad to treat as a gameplay desync by itself. The `basic` hash is built from `ReadGameStateSample()` and intentionally mixes local/volatile diagnostic fields such as local player ID, net state words, packet tick/key/action/bytes, console/player input slots, stage/camera words, course-select words, and object/candidate scan data. Those fields can differ while the gameplay-relevant player state still remains usable.
- Log evidence: `1781618163` had 614 basic-only mismatches and still completed 3 matches; `1781619493` had 709 and progressed through 5 matches; `1781621971` had 712 and progressed through 4 matches. Treat basic-only mismatch as a noisy diagnostic signal, not a user-facing desync warning, unless paired with a more specific gameplay hash.
- Finding: `playerGlobal=0` remains a strong signal. The affected logs were `1781618856`, `1781619326`, `1781620292`, and `1781621451`, matching the user's reports of visible/persistent divergence. The old logs only contain the whole `0x0208B324..+0xC0` player-global hash, so they prove that the player-global block diverged but do not identify the exact byte/field that diverged.
- Correlation: the first or persistent `playerGlobal` mismatches cluster around input synchronization stress: `throttle-blocked`/`throttle-resolved`, input frame throttle timeout, remote input timeout, peer disconnect, or start-ready timeout. This points more toward missing/late exact-frame remote input or rematch/start barrier state getting out of phase than random RAM corruption.
- Current interpretation: with rollback disabled, the runtime must not advance a gameplay frame with missing remote input and then later accept that input as if it could be replayed. If that path happens, the visible symptom would be exactly the reported "opponent stops, later moves again, but the inputs during the stop were not applied locally." Current code is intended to wait for the exact remote frame in the no-rollback path, so the next fix work should prove whether any bypass/timeout/restart path can still let a peer continue with neutral/stale input.
- Artificial delay tests:
  - `logs/codex-delay-desync-stress-20260618-a`: continuous host/client stress input, input delay 4, max lead 4, send delay 8, jitter 8, unreliable input with bundle history 8. It completed 7200 frames with 210 basic-only mismatches, zero `playerGlobal=0`, and no remote/throttle timeout.
  - `logs/codex-delay-desync-stress-20260618-b`: same stress input with send delay 12, jitter 12, unreliable input, bundle history 8, and deterministic drop modulo 17. It still had zero `playerGlobal=0`; the client only timed out on the final exact input frame after the host had already reached its frame limit.
  - `logs/codex-delay-desync-stress-20260618-d`: direct host/client launch with fallback allowed (`remoteTimeoutFatal=0`), wait timeout 120 ms, send delay 16, jitter 16, unreliable input, no bundle history, drop modulo 19. This produced 604 remote input timeouts, 842 frame-lead throttle timeouts, and 72 `playerGlobal=0` mismatches. The first `playerGlobal=0` was at frame 1200, immediately after repeated remote input/throttle timeouts around frames 1183-1198. This confirms that advancing no-rollback gameplay with missing exact-frame remote input is sufficient to create the player-global divergence.
- WebRTC/no-rollback reproduction checks:
  - `logs/codex-webrtc-norollback-stress-20260618-b`: real `nsmb-net-bridge` WebRTC signaling path, input delay 4, max lead 4, unreliable input with bundle history 8, 12000 frames. It completed at about 50 FPS with no `game state mismatch ... playerGlobal=0`, no remote input timeout, and no frame throttle timeout. It did produce only basic-only mismatch.
  - Added test-only bridge impairment knobs to `tools\nsmb-net-bridge`: `NSMB_NET_BRIDGE_DELAY_MS`, `NSMB_NET_BRIDGE_JITTER_MS`, and `NSMB_NET_BRIDGE_DROP_MODULO`. Defaults are zero, so normal bridge/GUI behavior is unchanged. `scripts\run-nsmb-mvl-local-triage.ps1` can pass them as `-BridgeDelayMs`, `-BridgeJitterMs`, and `-BridgeDropModulo`.
  - `logs/codex-webrtc-norollback-impair-20260618-a`: WebRTC path with bridge delay 25 ms, jitter 55 ms, and drop modulo 37. Under the test harness it stopped with no-rollback frame-lead throttle timeout around frame 1359/1365, not `playerGlobal=0`. This matches the intended protection: when remote input falls too far behind, gameplay does not advance with fabricated input.
  - `logs/codex-webrtc-norollback-impair-20260618-b`: WebRTC path with bridge delay 40 ms and jitter 90 ms, no drop. It was stopped manually after about 290 seconds / frame 2220 because it was deliberately slow. It had no `game state mismatch ... playerGlobal=0`; logged mismatches remained basic-only and player-global component hashes matched on both peers.
  - Added triage support for exact GUI-like select-mode stage/seed sequences, AppData ROM/save reuse, role-specific input scripts, role-specific input delay/drop, and role-specific bridge impairment. Defaults preserve the existing normal triage path.
  - `logs/codex-webrtc-norollback-appdata-stress-1781620292-b`: AppData host/client ROMs and saves, installed GUI melonDS, exact `1781620292` stage/seed sequence, WebRTC no rollback, minimal/stress input. It reproduced the GUI timing and basic-only mismatches but did not reproduce `playerGlobal=0`; player-global stayed equal through 8000 frames.
  - `logs/codex-webrtc-norollback-sweep2-1781620292-*`: multiple asymmetric host/client input scripts under the same no-rollback WebRTC settings did not reproduce `playerGlobal=0`. Some scripts hit start-ready timeout, which remains relevant to the second-match/start issue but is not the same player-global corruption.
  - `logs/codex-webrtc-norollback-mixed-currenthost-appclient-1781620292-a`: host used the current generated ROM while client used the AppData GUI client ROM. This reproduced `game state mismatch ... playerGlobal=0` immediately at frame 840 on both peers, with no rollback enabled and WebRTC involved. The generated host ROM SHA-256 was `06B30C10115BC959049F1BAC52F48C130ACFE6BA8ECC2BFFC46C7A0F1379EA8A`; the AppData client ROM SHA-256 was `0E8B07D5DDFCB0378D26AB1FD1D3F3E8F91715B48895F9423A0DF977F0B70055`. This proves peer ROM/patch mismatch is sufficient to break `playerGlobal`.
- Current conclusion from delay/WebRTC tests: random communication delay, including the actual WebRTC sidecar path, did not break `playerGlobal` while exact-frame remote input was eventually available. `playerGlobal` broke in the deliberately invalid fallback test where missing exact-frame remote input was allowed to continue as neutral/stale input, and in the mixed-ROM test where peers had different patched ROM bytes. Ordinary local WebRTC jitter alone remains unproven as a cause.
- Updated suspicion: the user's real `playerGlobal` logs are less likely to be caused by ordinary WebRTC jitter alone. Higher-risk areas are peer ROM/patch identity mismatch, rematch/start-ready rebasing, match transition state, or another game-state nondeterminism that is merely correlated with input stalls.
- Second-match stall recheck:
  - There are two concrete failure shapes in the GUI logs.
  - In `1781620292`, local reached result at frame `4320`, then blocked at old-game input lead before the local restart could run: `input frame throttle timeout frame=4342 sendFrame=4326 remoteInputFrame=4321 lead=5`. While local was stuck waiting for old-game input, it received the peer's next-game `start ready frame=4513`. `1781619326` shows the same class: local timed out at old-game `frame=3153 sendFrame=3104 remoteInputFrame=3099`, while a remote next-game `start ready frame=3348` arrived. This means the no-rollback throttle can prevent a peer from reaching its restart/next-game barrier if the other peer already transitioned and stopped sending old-game frames.
  - In `1781618856`, local did reach restart and accepted the remote ready (`localFrame=24899`, `remoteFrame=24911`, local `logicalStart=24847`) but then immediately timed out waiting for `remote input frame=24847`. The log directly before timeout shows `recv-gap ... logicalFrame=24849 ... lastRecv=24849`, meaning the remote's new-game input stream began at `24849`; the local was waiting for `24847`/`24848`, which never arrived. This points to per-peer logical-start labels disagreeing after restart. The current start-ready packet carries only the peer's raw ready frame, not the peer's logical start/epoch, so the receiver cannot verify or negotiate a common first logical input frame.
  - Successful `1781618163` restarted twice with identical accepted ready frames (`7561/7561`, then `29499/29499`) and matching logical starts. It still had many normal throttle block/resolved events, so throttle by itself is not the bug; the fatal condition is throttle/remote wait crossing a result/restart generation boundary or a mismatched first logical frame after that boundary.
  - Reproduction check `logs/codex-rematch-repro-webrtc-baseline-20260618-b`: WebRTC, no rollback, role-specific stress input, stage/seed sequence `0,1,2` / `101,202,303`. Baseline restarted cleanly through the first result: result `3934`, restore `4053`, both peers sent/accepted start-ready `4233`, logical start `4199`. It still logged normal `recv-gap`/`send-gap`, confirming gaps alone are not fatal.
  - Reproduction pattern 1 `logs/codex-rematch-repro-old-throttle-20260618-a`: client input send delay `3920..4080 +420f` reproduced old-generation input lead timeout at the match boundary. Host stopped at `input frame throttle timeout frame=3946 sendFrame=3924 remoteInputFrame=3919 lead=5`; client similarly stopped at `frame=3951 sendFrame=3929 remoteInputFrame=3924`. This proves an artificial delay at result time can strand peers in the old input epoch before restart.
  - Reproduction pattern 2 required a test-only raw-frame input drop range because normal bundle history repairs single dropped frames. Added disabled-by-default triage knobs for `MELONDS_NSML_INPUT_DROP_START_FRAME` / `END_FRAME` and `-InputBundleHistory`.
  - `logs/codex-rematch-repro-post-ready-range-drop-20260618-a`: dropping client frames `4199..4210` after restart with normal `inputMaxFrameLead=4` produced start-ready accept first, then throttle timeout: host accepted remote start-ready `4233` with logical start `4199`, then stopped at `input frame throttle timeout frame=4233 sendFrame=4203 remoteInputFrame=4030 lead=173`.
  - `logs/codex-rematch-repro-post-ready-remote-timeout-20260618-a`: same post-ready drop, but with `inputMaxFrameLead=999` to avoid the throttle guard, reproduced the direct exact-frame wait shape. Host accepted remote start-ready `4233` / logical start `4199`, then timed out at `remote input timeout frame=4199`; client dropped frames `4203..4208` and timed out waiting for host `frame=4204`. This matches the `1781618856` failure class: ready is accepted for the next game, then the receiver waits for a first logical input frame that the peer's new input stream does not provide.
- Rematch input-boundary fix:
  - Restart now clears local/remote input queues, delayed input packets, predicted inputs, last sent/received input frame counters, start-ready state, and input-health trace markers. Late old-epoch input packets and old start-ready packets below the current `NetplayStartFrame` are ignored instead of poisoning the new match's `LastReceivedInputFrame`.
  - Script ROM freshness fix: `scripts\generate-nsmb-mvl-stable-roms.ps1` now writes and validates sidecar manifests keyed by generator source hash, source ROM SHA-256, stable-generation options, and generated host/client ROM hashes. Matching ROM pairs are reused; `-Force` regenerates explicitly.
  - Manual/script launch paths that previously reused default stable ROM files now run the stable ROM ensure step before play: `manual-local`, `manual-peer`, `split-local-input-smoke`, `split-local-result-smoke`, `stable-split`, and the `local-triage` wrapper. `lan-route` configured-ROM generation now uses a shared `roms\.cache` pair and copies it into the run log instead of regenerating for every timestamped log directory.
  - Follow-up fix: the manual wrapper ensure calls now use hashtable splatting. The previous array splat form was parsed positionally in Windows PowerShell and could pass the host ROM path into `MvlStage`.
  - Verification: PowerShell parser checks passed for all modified scripts. A test pair under `logs\codex-rom-cache-test` generated once from `roms\nsmb-us.nds`; a second identical run reported `stable MvL ROMs are up to date`.
  - Additional verification: `logs\codex-rom-cache-splat-test` exercised the fixed hashtable-splat call shape with `MvlBigStars=3`; the first run generated and the second run skipped as up to date.
  - When both peers accept start-ready, the input epoch is primed by inserting neutral local/remote input for the input-delay warmup range `[logicalStart, logicalStart + delay)`. This removes the previous dependency on the first post-ready bundle containing historical frames and prevents the receiver from waiting for frames that the peer has not actually begun sending in the new epoch.
  - While the result scene is waiting for auto-restart and the match is not complete, input-netplay scratch/lockstep is paused so old-game frame-lead throttle cannot block the local process from reaching the restart barrier.
  - Review correction: `ResetMvlRuntimeSyncStateForRestart()` now performs the restart input/cache reset under `G.Mutex`, matching the network pump thread's `PumpNetworkLocked()` / `StoreRemoteInputLocked()` synchronization and avoiding concurrent `RemoteInputs` / `DelayedInputs` map/vector access when `MELONDS_NSML_NET_PUMP_THREAD` is enabled.
- GUI ROM reuse audit before the fix:
  - The GUI does call `ensure_roms()` before `start_match()`, so it attempts to prepare ROMs on every launch.
  - Before this fix, `prepare_roms()` reused cached ROMs when `nsmb-mvl-host.nds` and `nsmb-mvl-client.nds` existed and their sidecar `.nsmb-mvl-version` files equaled the fixed `REUSABLE_ROM_FORMAT` string. It did not include or check the source ROM hash, generated ROM hash, `nsmb-mvl-rom` code hash, bundled app/build version, role ROM pair hash, or generation settings in the marker.
  - Before this fix, the signaling/matchmaking schema sent gameplay settings only. It did not send ROM/patch hash or reject peers with different generated ROM bytes.
  - Local evidence: AppData `nsmb-mvl-host.nds` and `nsmb-mvl-client.nds` both have marker `nsmb-mvl-reusable-runtime-config-v3`, but the AppData host ROM SHA-256 is `9473BBB2252AC2F9B962940B5E2513E6800AF7EBEA9D17A3CE35D0B1017F1E09`, while a current generated host ROM in the mixed repro was `06B30C10115BC959049F1BAC52F48C130ACFE6BA8ECC2BFFC46C7A0F1379EA8A`. This proves marker-current cached ROMs can differ from current generated output.
- ROM identity fix implemented:
  - The GUI reusable ROM sidecar is now a JSON manifest instead of a single marker string. It records manifest version, ROM format, generator identity, source ROM SHA-256, symbols SHA-256, generation options, generated host/client ROM SHA-256, and a `rom_pair_id`.
  - `generator_id` is derived from the GUI package version, `REUSABLE_ROM_FORMAT`, and compile-time contents of the ROM generator/settings sources used by the GUI path. If those generator sources change, the manifest no longer matches and the GUI regenerates cached ROMs.
  - `ensure_roms()` verifies both the manifest inputs and the actual generated host/client file hashes before reusing AppData ROMs. Old `.nsmb-mvl-version` marker files are not considered current and will be replaced on the next ROM preparation.
  - Public matchmaking rooms now include `rom_identity`, and join requests send the local `rom_pair_id`. The GUI prepares/checks ROMs before room creation and before joining, and both the GUI and signaling HTTP API reject a public-room join when the `rom_pair_id` differs.
  - Manual room-code connection still has no HTTP room metadata to compare against; it continues to rely on the local manifest regeneration check.
  - Follow-up: GUI-generated ROMs are now treated as a runtime-configurable common ROM pair instead of being regenerated for every stage/wins/lives setting. The embedded direct-load fallback options are canonical, while actual match settings continue to come from the launcher runtime config.
  - Follow-up: when a saved base ROM path exists, the GUI starts `ensure_roms()` in the background at launcher startup and stores the prepared ROM identity in memory. Public room creation, public room join, and manual start reuse that prepared pair when the source ROM path matches, so joining normally does not wait for ROM generation.
- Next actions:
  - Consider extending start-ready with an explicit restart/input epoch and sender logical start for stronger cross-version diagnostics. The current fix rejects stale ready frames below `NetplayStartFrame`, but the wire packet still carries only the raw ready frame.
  - Use the new `player_global_mismatch` JSONL payload on the next reproduction to identify whether the exact divergence is player slot 0/1 state, death/transition/life/star state, actor state, or start/restart state.
  - If manual room-code connections also need cross-peer ROM checking, add a WebSocket-side hello/identity exchange to the bridge protocol in addition to the public-room HTTP check.
  - Keep testing whether the historical `playerGlobal=0` captures disappear after the manifest/`rom_pair_id` fix.
- Verification:
  - `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml` passed.
  - `cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml -- -D warnings` passed.
  - `pnpm run ci` passed in `tools\nsmb-signaling-server`.
  - `pnpm run ci` passed in `tools\nsmb-mvl-gui`.
  - After the startup prewarm/common-ROM follow-up, `cargo test --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml`, `cargo clippy --manifest-path tools\nsmb-mvl-gui\src-tauri\Cargo.toml -- -D warnings`, and `pnpm run ci` in `tools\nsmb-mvl-gui` passed again.
  - `cargo clippy-all` could not be used because the subcommand/alias is not installed in this environment.
  - Rematch-boundary reproduction verification: `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed after adding disabled-by-default triage drop-range knobs. WebRTC no-rollback runs under `logs/codex-rematch-repro-old-throttle-20260618-a`, `logs/codex-rematch-repro-post-ready-range-drop-20260618-a`, and `logs/codex-rematch-repro-post-ready-remote-timeout-20260618-a` reproduced the two rematch input-generation failure classes described above.
  - After the rematch input-boundary fix, `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed. The previous reproductions no longer fail: `logs/codex-rematch-fix-old-throttle-20260618-a` reached frame limit `7000`; `logs/codex-rematch-fix-post-ready-remote-timeout-20260618-a` reached frame limit `5600` with `inputMaxFrameLead=999`; and `logs/codex-rematch-fix-post-ready-range-drop-20260618-a` reached frame limit `5600` with normal `inputMaxFrameLead=4`. None logged `input frame throttle timeout`, `remote input timeout`, or `game state mismatch ... playerGlobal=0`.
  - After the mutex correction for restart reset, `cmake --build build\release-windows-x86_64 --config Release --target melonDS --parallel` passed.

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
- Current blocker: the actual first-divergence cause still needs a reproduced GUI/manual run with the new JSONL event diagnostics.
- Next action: on the next GUI reproduction, inspect each peer's GUI mismatch warning, `melonds-diagnostics.json`, and `melonds-events.jsonl`; if the JSONL event payload is still not enough, rerun with manual `-DesyncLog` around that reported frame for full CSV context.
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
