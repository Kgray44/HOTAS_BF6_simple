# Whole-App Responsiveness Phase 2 — Native Qualification

**Status:** **AUTOMATED QUALIFICATION COMPLETE — OWNER ACCEPTANCE DEFERRED.**

This is an unversioned responsiveness-campaign record, not a product release.
It does not authorize a merge, tag, installer, updater, version change, or
Phase 3 remediation. Signal Flow was excluded from the driver and results.

## Provenance and boundaries

| Item | Value |
| --- | --- |
| Phase 0 characterization | `f137c5405698989b6cb664aa039a8d4b9486026b` |
| Phase 1 base | `4325fdac929f0a59fb5769b3c43a2e709937ef66` |
| Phase 2 native-driver implementation | `1fd8b616ccc05cc9b2f166f1503bb0621ed53ad5` |
| Branch / PR | `codex/whole-app-responsiveness-phase2` / draft PR #72 |
| Release build / evidence root | `C:\\hotas-builds\\responsiveness-phase2` / `evidence-phase2` |

The opt-in driver launches the real Release executable and attaches only after
the root `QQuickWindow` exists. Its mode is `native-window-synthetic`: it uses
Qt native-window mouse, wheel, key, and resize delivery rather than an
offscreen QML fixture. Qualification mode isolates test configuration and
suppresses external setup inspection; it neither starts nor controls the
owner's mapper process.

The driver makes two cycles through Overview, Devices, Axes, Buttons, Curve
Editor, Profiles, Adaptive Response, Automation, Diagnostics, and Settings.
It deliberately never routes to Signal Flow. It performs 72 wheel events
(Settings light, Axes medium, Adaptive Response heavy), panel interaction, 100
native slider pointer interactions, six native key events in Profile search,
and window sizes 900x650, 1320x840, and 1600x1000. The safe persistence path
uses an isolated profile; 100 explicit persistence-boundary updates accompany
the slider workload. It is accurate to call the pointer events native QML
interaction and the writes isolated profile persistence; this is not an owner
configuration edit.

## Automated native matrix

All five runs completed with driver failure count zero, 100 slider events, 100
isolated persistence operations, and no ordinary matrix wheel event over one
second. Percentiles are milliseconds. Input is application input-to-next-frame
instrumentation, not physical-display scanout.

| Scenario | CPU % | Disk MB/s | Input p50 / p95 / p99 / max | Input >1 s |
| --- | ---: | ---: | ---: | ---: |
| Idle | 12 | 4.7 | 78.4 / 569.2 / 902.4 / 902.5 | 0 |
| Heavy CPU | 99 | 0.7 | 95.1 / 587.7 / 920.9 / 937.1 | 0 |
| Disk | 9 | 138.5 | 77.9 / 567.8 / 901.0 / 901.2 | 0 |
| Combined CPU + disk | 100 | 61.6 | 68.3 / 620.9 / 954.4 / 2677.7 | 5 |
| Near-saturation CPU | 100 | 0 | 73.7 / 613.1 / 946.9 / 2517.3 | 15 |

The normal workload's wheel maxima were 902.5 ms (Idle), 921.3 ms (Heavy
CPU), 901.2 ms (Disk), 954.5 ms (Combined), and 947.0 ms (near saturation):
none crossed one second. Page-navigation maxima nevertheless show contention
is significant: Profiles 2677.5 ms, Adaptive Response 422.4 ms, Settings
228.4 ms, Devices 172.8 ms, Axes 147.7 ms. These are measurements, not an
owner observation of feel.

## Event loop, frames, and persistence

| Scenario | Loop p99 / max / >1 s | Frame p99 / max | Persistence requests / writes / durable |
| --- | --- | --- | --- |
| Idle | 72.6 / 480.0 / 0 | 447.7 / 1032.3 | 201 / 201 / 201 |
| Heavy CPU | 158.5 / 918.6 / 0 | 514.0 / 1316.5 | 201 / 191 / 201 |
| Disk | 86.4 / 753.6 / 0 | 472.9 / 1421.2 | 201 / 197 / 201 |
| Combined | 911.1 / 9742.5 / 6 | 2092.0 / 9756.9 | 201 / 188 / 201 |
| Near-saturation | 290.1 / 8983.8 / 6 | 2427.2 / 8997.8 | 201 / 201 / 201 |

Every matrix run recorded zero persistence failures and eventual durable catch
up. The worker may complete fewer intermediate writes than requests because
the existing coordinator coalesces generations; durability reached 201 in each
case. This confirms the native isolated workload does not restore synchronous
ordinary GUI persistence, but does not prove an owner's production profile.

## P0 reproduction and classification

A separate 90-second Combined native torture run completed cleanly: CPU 100%,
disk 77.9 MB/s, 166 actions, driver failures zero, and 100 native slider / 100
isolated persistence actions. It reproduced the reported failure class:

| Metric | Result |
| --- | ---: |
| Input p99 / max / >1 s / >5 s | 3894.4 / 9315.0 / 80 / 45 |
| Event-loop p99 / max / >1 s / >5 s | 3969.5 / 14156.3 / 19 / 12 |
| Frame p99 / max / >5 s | 4994.9 / 14158.9 / 12 |
| Major stalls | 64 |

The top event-loop/frame stalls occurred while Profiles was active; a worst
mouse press was 9315 ms on Adaptive Response. This is a **P0 scheduling-
starvation finding** under saturation, not a claim of a specific GUI-thread
blocker. No product behavior was changed in this evidence phase.

## Mapping evidence and physical limit

The controlled mapping A/B and existing hotpath benchmark evidence remains
allocation-free with no measured material regression from the Phase 2 driver:
the driver is opt-in and outside the DirectInput → MappingWorker → vJoy
per-report path. A scheduler surrogate ran the existing mapping benchmark
during native Idle, Heavy CPU, and Combined qualification. Under Combined load
it observed input p99/max 7591.5/12288.7 ms and loop p99/max 362.6/18363.6 ms;
that strengthens the scheduling hypothesis but does **not** establish that the
live MappingWorker caused the native stalls.

Read-only discovery saw a physical HID candidate at the PnP layer and a vJoy
DirectInput device. The vJoy ownership query reported device 1 busy by another
process; no acquire, reconfiguration, or mapper control was attempted. A human
physical HOTAS-to-vJoy run, mapping-off/on comparison with the actual owner
mapper, and owner-observed scroll/control feel therefore remain unqualified.

## Verdicts and Phase 3 recommendation

| Subsystem / question | Verdict |
| --- | --- |
| Real native-window automated interaction | Qualified |
| Idle, CPU, disk, combined, near-saturation characterization | Qualified |
| Native safe persistence and eventual durability | Qualified |
| Signal Flow exclusion | Qualified |
| Multi-second ordinary interaction under severe load | Issue reproduced (P0) |
| Root cause within GUI vs scheduler vs live mapper | Inconclusive; scheduler starvation is the leading evidence |
| Physical HOTAS → MappingWorker → vJoy under contention | Owner acceptance deferred |
| Owner-perceived smoothness and usability | Owner acceptance deferred |

**Phase 3 candidate:** scheduler/QoS and GUI-starvation investigation. First
reproduce below full saturation with the actual mapper on/off, correlate ready-
to-run GUI work with worker activity, and preserve the allocation-free mapping
hot path. Do not change worker priority, persistence architecture, loader
architecture, or Signal Flow until that evidence exists.

## Completion checks

The final Release all-target build passed. Focused regressions passed:
`mapping_core_tests`, `config_persistence_coordinator_tests`,
`responsiveness_probe_disabled`, `responsiveness_probe_aggregation`, and
`app_qml_startup_tests`. Evidence is retained outside the repository at
`C:\\hotas-builds\\responsiveness-phase2\\evidence-phase2`.

Before owner acceptance, use a safe test profile with the owner's physical
controller and vJoy mapper active; run the Combined workload while navigating,
scrolling, resizing, and editing; record mapping-on/off comparison, actual
vJoy movement, and owner feel. Stop after that acceptance capture—no Phase 3
implementation, merge, tag, release, or version change is authorized here.
