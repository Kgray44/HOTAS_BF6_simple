# Whole-App Responsiveness Phase 4 — Contention Resilience

## Provenance and boundaries

This is an unreleased, unversioned Phase 4 candidate on
`codex/whole-app-responsiveness-phase4`, created from Phase 3 head
`f0dac249d960d15dd213a293960f32669dc2a7c8`. At branch creation,
`origin/main` was `bdedc52848ec18ddc4f6a387776526dcde1c7ce5`; the candidate
was not rebased, merged, tagged, or used to change release, installer,
updater, manifest, or version metadata.

The production process remains Normal priority. Phase 3's scheduler contract
is unchanged: GUI `+1`, render inheriting `+1`, persistence `-1`, and the
MappingWorker's existing High-priority policy is untouched. Phase 4 adds no
mapping-path dependency and does not start MappingWorker in qualification.
It also changes no persistence semantics and makes no Signal Flow runtime,
qualification, benchmark, or source change.

The evidence is from the existing timer-driven native `QQuickWindow`
qualification driver with isolated storage. Computer-use automation could not
enumerate the native app window on this desktop, so it supplied no native
pointer result. The in-app driver did create a native window and inject
synthetic mouse, keyboard, wheel, resize, navigation, and slider activity.
This is not physical HOTAS, vJoy-output, native-pointer, typography, or owner
acceptance proof; all remain deferred.

## Controller architecture

```text
Windows GetSystemTimes + 200 ms GUI monotonic heartbeat
                         |
                         v
             ContentionResilienceController
                         |
       +-----------------+------------------+
       |                                    |
       v                                    v
AppBackend presentation/background       Adaptive Response QML
timers, optional update deferral          live graph cadence + decoration
       |
       +-- never reaches DirectInput -> MappingWorker -> vJoy
```

The controller is a GUI/control-plane object. It does not start external
processes, WMI, per-tick disk logging, or the opt-in `ResponsivenessProbe`.
`HOTAS_CONTENTION_LEVEL=normal|pressure|severe|auto` is a process-local
development/qualification override only; no user-facing setting was added.

## Inputs and hysteresis

The heartbeat runs at 200 ms. `GetSystemTimes` is sampled on the GUI thread at
approximately 750 ms after a one-time construction baseline. A first true
near-saturation CPU sample is therefore usable immediately rather than being
lost to baseline establishment.

| Transition | Final condition |
| --- | --- |
| Normal -> Pressure | Two CPU samples at >=90%, or two observations with >=320 ms heartbeat lateness; a first CPU sample >=97% or a >=1000 ms heartbeat enters Pressure immediately. |
| Pressure -> Severe | One CPU sample >=97%, one >=550 ms heartbeat delay, or a second >=1000 ms heartbeat while already in Pressure. |
| Severe -> Pressure | Four CPU samples below 94% with heartbeat below 300 ms (about three seconds at normal sampling cadence). |
| Pressure -> Normal | Six CPU samples below 84% with heartbeat below 260 ms (about 4.5 seconds). |

The Severe state never demotes due to another major heartbeat. A regression
test covers that exact state-retention case, as well as normal/pressure/severe
overrides, ordinary-hitch stability, saturation entry, and slow recovery.

## Applied policy and adapted work

| Level | Telemetry / live graph policy | Adaptive history policy | Background read-only polling | Decorative motion |
| --- | --- | --- | --- | --- |
| Normal | 33 ms (about 30 Hz) | Established 12 ms cadence | 1x | Enabled, 1.0 scale |
| Pressure | 50 ms (20 Hz) | 50 ms (20 Hz) | 2x interval | Enabled, 0.5 scale |
| Severe | 83 ms (about 12 Hz) | 83 ms (about 12 Hz) | 4x interval | Disabled, 0.0 scale |

The policy changes only proven recurring presentation/control work:

- visible snapshots and button telemetry;
- legacy/numeric telemetry, controller discovery, game/foreground discovery,
  and minimized/tray polling;
- Adaptive Response live graph, simulator/replay presentation, history, and a
  nonessential toggle animation;
- an update check that has not started may defer for 30 seconds during Severe.

User commands are not disabled. Running update work, persistence durability,
mapping configuration, and every MappingWorker/DirectInput/vJoy path are
unchanged. Normal uses the prior 33 ms presentation cadence and 12 ms
Adaptive Response history cadence exactly. A final broad scope check of the
shared backend surfaced pre-existing Signal Flow identifiers; no Signal Flow
behavior was analyzed, run, benchmarked, or changed.

## Qualification cadence and load behavior

The driver records the policy actually applied at export. Its aggregate
publication counters are activity-dependent—not a replacement for timer
interval measurement—because the Adaptive page is visible for only part of a
run and a saturated Windows scheduler can deliver fewer turns than the policy
permits.

| Run | Load record | Controller signal / final policy | Result |
| --- | --- | --- | --- |
| Idle automatic | 19–51% harness CPU (31.5% average) | Normal; no transitions; 33/33 ms, 1x | No pressure overreaction; zero input samples over 1 s or 5 s. |
| Moderate automatic | 87–97% harness CPU (92.0% average) | `GetSystemTimes` 74–95%; final Pressure; 50/50 ms, 2x | Mild throttling; zero input samples over 5 s. |
| High automatic | 93–99% harness CPU (97.8% average) | `GetSystemTimes` 87–100%; Pressure -> Severe -> Pressure; final 50/50 ms, 2x | Zero driver failures and zero input samples over 5 s. |
| Final Combined | 100% CPU plus bounded 128 MiB disk workload for the native run | `GetSystemTimes` 100% (78 samples); Pressure at 955 ms, Severe at 1831 ms; 83/83 ms, 4x, decoration off | 90-second native torture completed, exit 0, zero driver failures. |

The final Combined driver completed 246 actions, including its
`combined-torture-loop-90s` loop. Under that hostile desktop condition the
controller's aggregate evidence recorded 568 telemetry publications and 211
live-graph refreshes over 142.4 seconds of app lifetime (about 4.0/s and
1.5/s respectively). Those lower-than-policy observed rates are scheduler
starvation, not a claim that optional work exceeded the Severe ceiling.

## Idle regression

The automatic idle run remained Normal with no transitions. Native synthetic
input p99/max were 904.5/904.6 ms, event-loop p99/max 83.1/410.1 ms, and frame
p99/max 436.8/989.0 ms; there were no input, loop, or frame samples over five
seconds. It completed the 100-edit persistence sequence at durable generation
201 with zero failures. This is a native-driver regression check, not owner
visual acceptance.

## Final 90-second Combined comparison

Phase 3's final is the direct comparison record: native synthetic input p99
1007.0 ms / max 2336.5 ms / 66 samples over one second / zero over five
seconds; event-loop max 10681.2 ms with one sample over five seconds; frame
max 11175.8 ms.

| Metric | Phase 3 final | Phase 4 final automatic Combined | Reading |
| --- | ---: | ---: | --- |
| Input p99 | 1007.0 ms | 3454.5 ms | Regressed in this host's 100% CPU+disk run. |
| Input max | 2336.5 ms | 4005.0 ms | Regressed; the preferred <1.5–2.0 s target was not met. |
| Input >1 s / >5 s | 66 / 0 | 92 / 0 | The no->5-second input result held, but >1-second input did not improve. |
| Event-loop max / >5 s | 10681.2 ms / 1 | 6546.0 ms / 1 | Maximum improved, one >5-second sample remains. |
| Frame max / >5 s | 11175.8 ms / not separately counted | 6571.6 ms / 1 | Maximum improved; one >5-second frame interval remains. |
| Navigation / scrolling | Full native sequence | Full native sequence, Profiles navigation max 4004.7 ms | Scroll actions executed; no separate scroll-percentile field is emitted. |
| Persistence / driver | Durable 201, zero failures | 201 requests, 196 writes, durable 201, zero failures; driver zero failures | Durability and driver completion preserved. |

This is intentionally **not** presented as a full success against the Phase 4
input target. The retained native evidence identifies repeated Profiles-page
loader activation as the dominant residual: it consumes roughly 3–4 seconds
of GUI user CPU during several navigation events under the fully saturated
CPU+disk workload. Severe remained latched (no P<->S flapping), so the trace
does not support blaming the controller for those page-construction stalls.
The controller did retain zero >5-second user input, but the remaining
multi-second input stalls require dedicated page-construction work.

## Mapping benchmark and persistence

`HOTAS_BUILD_PERFORMANCE_BENCHMARK=ON` was enabled only for local
qualification. Three standalone runs of `mapping_hot_path_benchmark` completed
successfully; every reported mapping, profile-control, and automation scenario
reported `hot_path_allocations=0`. An additional filtered verification run also
reported zero allocations throughout. This synthetic report-to-output-decision
benchmark intentionally has no DirectInput device or vJoy driver call, so it
does not claim physical mapping proof.

Phase 4 did not alter the Phase 1 persistence coordinator. In the final native
Combined run it received 201 requests, committed 196 coalesced writes, reached
durable generation 201, and reported zero failures. Worker total p99/max were
323.7/337.3 ms; no synchronous persistence work was moved onto the GUI.

## Residual issues and Phase 5 recommendation

Do not redesign Profiles in Phase 4. The evidence points to a narrow Phase 5
decision: measure and then selectively lazy-construct or virtualize the
Profiles-page heavy content (and only other similarly proven page loaders).
That work should preserve user-command authority and be qualified again at the
same 90-second Combined load. If a later trace instead shows broad notifier
fanout rather than construction, narrow that fanout; do not alter mapping or
the protected scheduler relationship.

Owner work remains a native visual/pointer review and a physical HOTAS/vJoy
session at normal desktop load. No release decision follows from this Phase 4
candidate.
