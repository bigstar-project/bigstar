# NSMB Mario vs Luigi Rollback Design Notes

## 2026-06-02 current status - Plan-D actor/global snapshot path

Current best Plan-D-like direction is no longer a rollback backend. It is a small actor/global/world snapshot path that avoids full NDS rollback restore/resim:

- New wire packet: `WirePlayerState`, 168 bytes. The base actor fields are always present; player global fields are read/applied only when `MELONDS_NSML_PLAYER_STATE_GLOBALS=1`.
- New host-authoritative wire packets: `WireWorldState`, 320 bytes, for the real Big Star actor and item-specific `Item(0x01F settings=0x00080002)` event; and `WireMovingHazardState`, 424 bytes, for up to four active moving-hazard actors.
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
- The packet carries actor transform, velocity, action/subaction/physics flags, damage cooldown, transition/collision/environment flags, and compact runtime byte flags.
- The optional global section currently carries per-player life/death/pipe/star counters. It is applied as event-only state, not as a full global overwrite.
- During player actor transition steps other than `1`, the actor+global route skips transform/full runtime writes and applies only minimal visible/defeated bytes. This avoids fighting the game's pipe/death transition code.
- The receiver applies the latest remote player actor snapshot before frame execution and again before game-state trace/sync.
- Existing fixed-size wire packets were also unblocked from the input-bundle branch so `WireNSMLPacket`, `WireGameState`, `WirePlayerState`, `WireWorldState`, and `WireMovingHazardState` can reach their exact handlers.

Verification:

- Build passed: `cmake --build build\release-windows-x86_64 --config Release --target melonDS -j 4`.
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
- `scripts/run-nsmb-mvl-manual-local.ps1 -PlanDActorSnapshot -AllowJit` is the lightweight manual-play command. It enables input delay `0`, frame lead `8`, network pump `50us`, player actor/global, Big Star, item-specific client spawn, and compact multi-instance moving-hazard snapshots every `2` frames, at most `1` prediction frame, and a 30-frame process-list rescan interval. It intentionally leaves detailed game-state/perf breakdown traces off unless explicitly requested. A short visible-window launch `logs/codex-manual-pland-runtimelean-launch-1800-20260602` propagated `playerStateSync=1 playerStateApply=1 playerStateGlobals=1`, ran with no rollback/resim, and measured active avg `17.665/17.663ms`, max `33.253/33.132ms`, `over33ms=0/0`.
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
  - `scripts/analyze-nsmb-mvl-object-lifecycle-diff.ps1` compares host/client lifecycle logs by sampled frame and aggregates actor-count differences by `objectID/settings`.
  - `logs/codex-pland-processlist-lifecycle-stage1-2400-20260602`: a 10-frame lifecycle trace window on generated course `1` passed Item spawn and all world/global gates. Active avg was `17.635/17.640ms`, max `45.892/42.345ms`, and max consecutive slow frames `1/1`. Same-frame `objectID/settings` comparison found no new persistent world gap after Item repair: remaining differences were the local-role `0x012` settings variant and one sampled row of the already-known host-only `0x0F0 settings=0x01080002` transient.
  - `logs/codex-pland-processlist-lifecycle-result-restart-6200-20260602`: a 30-frame lifecycle trace across result/restart passed the second-game, Big Star, moving-hazard, and 34-field manager/global gates. Active avg was `17.280/17.279ms`, max `49.521/47.596ms`, and max consecutive slow frames `1/2`. Aggregated differences were only local-role `StageFX(0x012)` variants: the regular `settings=0x00008000/0x00008010` split and a host-only `settings=0x00000005` effect while Luigi was dead before result transition. ROM resource symbols map logged vtable `0x02127840` to `_ZTV7StageFX`, so these visual actors stay local.
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

Current blocker / caveat:

- Strict full game-state comparison still fails early because the existing comparison assumes deterministic same-frame actor equality. The player-state path is an actor replication/visual correction path, not deterministic rollback. Current CSV traces show player slot/timing differences around frame 930 even while player actor motion is present.
- The 12000F three-game route no longer freezes under automated result/restart stress, but ordinary non-rollback frame spikes still exist (`50-52ms` max in that run). Manual play remains required before promotion.
- Added `tests/nsmb_us_direct_mvl_repeat_result_stress.inputs` and exposed `-MvlWins` in the split wrapper. A 12000F repeated death/result/checkpoint-restart route reached MvL stage entries at frames `870`, `5790`, and `9990`.
- Earlier strict runs exposed occasional paired-process stalls in the `278-825ms` range across three-game, Big Star, coin-sync, and item-sync routes. At least the newly reproduced large stalls were observer interference rather than Plan-D snapshot cost: stdout trace flush, diagnostic full-Main-RAM actor scans, and synchronous heartbeat file flush were each isolated and removed from the emulation thread. Keep the older logs as historical caveats because they predate the finer phase traces, but use the current async-heartbeat route for new performance decisions.
- Moving hazards now use a compact multi-instance snapshot with persistent GUID mapping and nearest-position fallback, but application still waits for equal host/client active counts. Automated lifecycle churn and stage variation passed; longer manual play is still required before treating it as complete.
- Course `1` host-only `Item` creation is now covered by an item-specific client spawn. The associated `0x0F0 settings=0x01080002` transient is not replicated yet; add it only if a concrete visible or gameplay mismatch appears. Blindly spawning every host-only actor would also replicate local-only effects.
- Result/restart lifecycle traces show local-role `StageFX(0x012)` differences, including a host-only lose/result lead-in effect. Keep these local unless a concrete visible defect appears; do not clone them as gameplay world actors.
- The selected MvL manager/global/stage-scene fields stayed equal during real star acquisition and repeated result/restart. Do not add blind runtime writes for them unless the new observation gate finds a concrete divergent route.
- This means the actor/global snapshot path is now a much more practical Plan-D-like route for "does not freeze / does not rollback-spike / remote actor moves / pipe death visibility survives", but it is still not a correctness replacement for deterministic rollback.

Next actions:

- Run longer manual play with the promoted item-specific Plan-D path, especially course `1` item interactions, and inspect the associated `0x0F0` transient only if a visible or gameplay issue appears.
- Stress the actor+global route with longer manual play, especially repeated result/restart, star acquisition, fall death, respawn, and pipe transitions. Keep automatic FPS-spike logging enabled in manual Plan-D mode.
- Keep the three-game stress route in repeated performance sweeps so occasional paired-process stalls remain visible instead of being hidden by average FPS.
- Keep the phase traces and async dedicated-heartbeat stall detector enabled in automation. Remaining occasional single-frame costs are mostly emulator `runFrame`, packet-bridge peer waits, or scheduling-like time outside the instrumented phases. Use the phase split before changing the runtime actor snapshot.
- Stress the compact multi-instance moving-hazard snapshot with longer manual play. Keep the strict drift gate enabled in automation.
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
