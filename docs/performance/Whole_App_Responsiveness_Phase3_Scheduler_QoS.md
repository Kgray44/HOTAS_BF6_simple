# Whole-App Responsiveness Phase 3: Scheduler QoS

## Scope and provenance

Phase 3 is an unversioned, unreleased scheduler-resilience change on
`codex/whole-app-responsiveness-phase3`, based on Phase 2 commit
`738df4b6563e60fbabfe6e9b21550c6a434abd7f`.  The observed `origin/main`
commit was `bdedc528`; this branch was not rebased, merged, tagged, or used
to change installer, updater, version, Signal Flow, mapping semantics, or
physical-device ownership.

The Phase 2 final 90-second combined native torture is the comparison
baseline.  It recorded input p99 3894.4 ms / maximum 9315.3 ms (80 samples
over one second, 45 over five seconds), event-loop p99 3969.5 ms / maximum
14156.3 ms (19 over one second, 12 over five seconds), and frame maximum
14158.9 ms.  Its persistence burst completed with 201 requested and durable
generations, 51 writes, and no failures.

## Validity controls

`NativeQualificationDriver` remains a timer-driven native `QQuickWindow`
driver.  Its navigation, scroll, control, persistence-burst, and torture
steps use `QTimer::singleShot`; it has no GUI-thread `sleep`, blocking wait,
or long busy loop.  The native synthetic pointer/keyboard path is still
distinct from physical-controller evidence.  Controller enumeration and vJoy
status checks are read-only; the mapper and owner hardware are isolated.

The desktop computer-use surface had no targetable native application, so it
could not independently operate the window.  This is not substituted with a
fake desktop interaction: the app's actual native `QQuickWindow` driver is
the evidence source.  Native pointer, typography, physical HOTAS, vJoy output
ownership, and owner acceptance remain deferred.

`scripts/Invoke-ResponsivenessCharacterization.ps1` now makes the test
harness itself auditable:

- it runs CPU work in one task-owned helper process with bounded worker
  threads rather than delaying the app by spawning one PowerShell process per
  worker;
- it adds only the sampled CPU headroom toward the requested *total* CPU
  target and records before, warm-up, and five-second lifetime samples;
- it explicitly enables native qualification, records the selected policy,
  and kills a workload that outlives the stress deadline so an unloaded tail
  cannot be reported as a loaded result.

Several early runs are therefore intentionally excluded: the original
multi-process generator delayed app launch past its deadline, full-generator
runs saturated an already-busy desktop at 100%, and runs marked
`workloadTimedOutBeforeStressDeadline=true` did not export a probe.  The
headroom-calibrated reports below are the acceptance evidence.

Evidence remains local and uncommitted under
`C:\hotas-builds\responsiveness-phase3\evidence-phase3`.

## Windows scheduler observation and candidate decision

Only Windows-native APIs were used for scheduler qualification:
`GetPriorityClass`, `GetThreadPriority`, `SetThreadPriority`, `OpenThread`,
and `GetThreadTimes`.  The process stays at Normal priority (32).  No
Above-Normal process class, Time Critical, or realtime scheduling ships.

| Candidate | Actual role priorities | 95% gate result | Decision |
| --- | --- | --- | --- |
| A: current | GUI 0, render 0 in the Phase 2 baseline | P0 baseline above | Rejected baseline |
| B: GUI boost | GUI 1; render inherits 1 when the render thread is created; persistence -1 | nominal 95, actual 96-100%, zero native failures; input p99 938.6 ms, loop p99 453.2 ms | **Selected** |
| C: render boost | GUI 0, render 1; persistence -1 | nominal 95, actual 93-100%; input p99 1192.7 ms, loop p99 825.7 ms | Rejected: GUI remains Normal and is worse |
| D: GUI + direct render boost | GUI 1, render 1; persistence -1 | Same effective priority relationship as B; initial saturated run timed out before probe export | Rejected: no incremental benefit over inherited render priority |
| Process Above Normal | Diagnostic-only capability | Not shipped | Rejected by policy boundary |

The selected production policy is minimal: on Windows it sets only the GUI
thread to `THREAD_PRIORITY_ABOVE_NORMAL` during app startup.  Qt Quick's
subsequently-created render thread inherited that effective priority in every
selected-policy report.  The production object captures no probe or CPU-time
evidence; observation remains native-qualification-only.  The existing
`MappingWorker` startup priority is untouched.  Its live effective priority
cannot be measured in safe isolated native qualification because the mapper
is intentionally not started; source configuration requests HighPriority,
and a physical-owner run is deferred.  The observed safe relationship is
therefore UI/render (1) > persistence (-1), with mapping live verification
`Unknown`, not inferred as hardware proof.

## Selected-policy validation

All rows below use the real native window, synthetic input, isolated test
storage, and actual sampled CPU values rather than nominal labels alone.

| Run | CPU / disk observation | Input p99 / max | Event loop p99 / max | Frame p99 / max | Driver / persistence |
| --- | --- | --- | --- | --- | --- |
| GUI, nominal 95 | 96-100%, average 99.5% | 938.6 / 1039.7 ms | 453.2 / 10587.2 ms | 412.4 / 10805.0 ms | 0 failures; 201 requested, 177 writes, 201 durable |
| GUI, nominal 100 | 88-100%, average 95.0% | 1004.9 / 1301.1 ms | 411.9 / 10465.7 ms | 273.6 / 10737.5 ms | 0 failures; 201 requested/writes/durable |
| GUI combined | 88-100%, average 97.2%; disk peak 65.0 MB/s | 1014.9 / 1345.3 ms | 383.7 / 5585.2 ms | 288.2 / 5843.3 ms | 0 failures; 201 requested, 200 writes, 201 durable |
| GUI final combined, 90-second torture | 91-100%, average 99.3%; disk peak 66.3 MB/s | 1007.0 / 2336.5 ms | 2137.8 / 10681.2 ms | 514.3 / 11175.8 ms | 234 actions, 0 failures; 201 requested, 185 writes, 201 durable |

The final torture has no input sample over five seconds (Phase 2 had 45),
and 66 over one second (Phase 2 had 80).  It has one event-loop sample over
five seconds (Phase 2 had 12).  The remaining 10681.2 ms heartbeat stall
contained only 46.9 ms GUI user CPU and 203.1 ms GUI kernel CPU; it remains a
real residual scheduler/desktop-contention observation, not a claim of a
fully eliminated P0.  The final test materially improves the Phase 2
worst-case input and loop behavior but does not prove physical-controller
smoothness or owner acceptance.

The regular combined report verified `GUI=1`, `render=1`,
`persistence=-1`, and Normal process priority.  `GetThreadTimes` records are
embedded in each completed probe's `schedulerEvidence`, including the large
stall CPU deltas.  Persistence used the representative 100-edit burst and
had zero failures; its bounded worker queue and serialization delays remain
observable in the probe without moving persistence onto the GUI thread.

## Mapping surrogate and benchmark

The selected-policy guarded scheduler surrogate uses the existing synthetic
`mapping_hot_path_benchmark`, not a physical mapper.  It has no DirectInput,
HidHide, or vJoy calls.

| Scenario | Actual CPU | Mapping benchmark cycles | Result |
| --- | --- | --- | --- |
| Heavy CPU | 93-100%, average 97.0% | 2 | `hot_path_allocations=0`; native persistence durable 201, failures 0 |
| Combined CPU + disk | 97-100%, average 99.5%; disk peak 97.7 MB/s | 2 | `hot_path_allocations=0`; native persistence durable 201, failures 0 |

Three standalone benchmark samples also completed without an error and
reported `hot_path_allocations=0`.  This is scheduler-contestion evidence
only; MappingWorker's physical report path and vJoy output were not activated.

## Remaining owner work and Phase 4 input

This branch is deliberately not a release candidate and has no version,
tag, installer/updater, or merge action.  Owner review should perform a
native window visual/pointer pass and a physical HOTAS/vJoy session under
normal desktop load before any release decision.  If the residual multi-second
heartbeat stalls remain owner-visible, a subsequent phase should investigate
desktop/game contention and scheduler telemetry with an explicit owner
scenario, rather than escalating this policy to realtime priority or changing
the mapping hot path.
