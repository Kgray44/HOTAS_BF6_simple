# Whole-App Responsiveness — Phase 7: Controlled Qualification Closeout

## Automated campaign decision

**COMPLETE for the authorized automated scope.** The two Phase 6 release-contract debts are resolved against the real Phase 4 architecture; the bounded QML lifecycle executable passes and now fails cleanly with progress if it regresses; and the startup heartbeat is classified as startup readiness rather than a runtime stall. The completed native qualification evidence remains valid: these closeout changes alter only qualification/reporting and test harnesses, not the product scroll policy, mapping worker, DirectInput, vJoy, persistence design, or contention thresholds.

This is not physical HOTAS, native physical-pointer/touchpad, accessibility, typography, or owner-acceptance proof. Those owner-review tasks remain explicitly deferred.

## Provenance and boundaries

| Item | Value |
| --- | --- |
| Candidate branch | `codex/whole-app-responsiveness-phase7` |
| Exact Phase 6 parent | `a920c1fbe3c71212b720952d701dc894b4cd6604` |
| Existing Phase 6 draft PR | #77; remains draft and unmerged |
| Worktree | `C:\\Users\\kkids\\Documents\\HOTAS_BF6-responsiveness-phase7` |
| Build directory | `C:\\hotas-builds\\responsiveness-phase7` |
| Evidence root | `C:\\hotas-builds\\responsiveness-phase7\\evidence-phase7` |
| Product QML files changed | none |
| Product mapping/persistence policy changed | none |

The Phase 7 branch preserves commit `9b791e3`. This closeout adds only source-contract/test coverage, bounded test-lifecycle diagnostics, and probe reporting boundaries. It does not rebase onto `main`, merge, tag, release, or change a version. The explicitly excluded Signal Flow surface was not navigated, profiled, benchmarked, or modified.

## Controlled-host-load evidence retained

The original Phase 7 PowerShell harness uses `GetSystemTimes` total-host CPU sampling every second, a task-owned pool of 15 managed worker threads with a bounded named-memory duty channel, and an optional bounded 128 MiB disk job. It retains every one-second raw observation and uses overlapping five-second means for the acceptance decision.

| Host-load run | Worker threads | Five-second windows | Min / mean / max | In 90–95% target | In 88–97% envelope | Longest continuous envelope interval | Stable |
| --- | ---: | ---: | --- | ---: | ---: | ---: | --- |
| CPU 90–95 | 15 | 146 | 87.2 / 92.6 / 99.2% | 74.7% | 93.8% | 46 s | yes |
| CPU 90–95 + 128 MiB disk | 15 | 147 | 87.5 / 92.9 / 97.3% | 80.3% | 98.0% | 92 s | yes |

The raw samples remain visible rather than replaced by windowed means: CPU-only was 150 samples at 76.9 / 92.5 / 100%, and CPU-plus-disk was 151 at 78.7 / 92.9 / 100%. No CPU or CPU-plus-disk matrix was repeated during closeout.

## Representative fixtures and native-window result retained

The isolated native driver populated presentation-only fixtures before scroll assessment: eight mapped axes, 32 mapped buttons plus one POV, and 12 automation rules. Across idle, CPU-only, and CPU-plus-disk runs, all 92 sessions were scrollable, with zero fixture-driver failures and zero non-scrollable Axes/Buttons/Automation records. The fixtures are not physical-device evidence.

| Run | Exit / timeout | Worst movement p95 | Worst wheel-to-frame p95 | Worst active-scroll frame p95 | Largest active-scroll frame | Input / frame / heartbeat samples over 1 s |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Idle representative matrix | 0 / no | 60.00 ms | 61.08 ms | 19.75 ms | 103.46 ms | 0 / 0 / 0 |
| Controlled CPU 90–95 | 0 / no | 43.52 ms | 46.61 ms | 29.83 ms | 101.74 ms | 0 / 0 / 1 startup record |
| Controlled CPU 90–95 + disk | 0 / no | 43.46 ms | 47.34 ms | 30.79 ms | 101.34 ms | 0 / 0 / 1 startup record |

The retained >1 s records are 1,305.25 ms under CPU-only and 1,092.92 ms under CPU-plus-disk. Both were attributed to the probe start (`startedSinceStartMs: 0`) with no page. They are retained as startup-readiness cost; they are not discarded or relabeled as a passing runtime measurement.

## Release-contract debt resolution

`ui_release_contract_tests` had exactly two stale assertions, both asserting pre-Phase-4 literals rather than the implemented architecture.

| Stale assertion | Actual contract now tested | Resolution |
| --- | --- | --- |
| Minimized snapshot timer starts directly at `kMinimizedSnapshotIntervalMs` | `AppBackend` routes visible telemetry through the controller and uses `scaledBackgroundInterval(...)` for minimized/tray polling; Normal / Pressure / Severe are 33 / 50 / 83 ms with background multipliers 1 / 2 / 4 | Accepted Phase 4 background-work degradation; test now asserts that controller-owned contract |
| Flight Deck live graph contains `interval: 33` | The visualizer reads `contentionResilience.liveGraphIntervalMs`, and all five graph timers use that presentation-only interval | Accepted Phase 4 dynamic presentation cadence; test now asserts binding, all five timers, and refresh accounting |

The repaired release contract also asserts that presentation lifecycle ownership does not enter `MappingWorker`. The Phase 4 controller remains control-plane-only: it degrades presentation before interaction and does not retune the mapping hot path.

## Bounded QML lifecycle diagnosis

The previous `app_qml_startup_tests --isolated-presentation` invocation had a test-harness defect: CTest supplied the argument, but the executable did not consume it and silently fell through to its broad default matrix. The prior stopped process therefore did **not** establish a product shutdown hang or a terminal QML verdict.

The argument now activates the existing isolated ten-page navigation workload. It runs two passes over Overview, Devices, Axes, Buttons, Curve Editor, Profiles, Adaptive Response, Automation, Diagnostics, and Settings; all 20 cases passed in the terminal run. It leaves the excluded surface out of scope. The test has both a 75-second CTest timeout and a 60-second test-only watchdog. On timeout the watchdog writes elapsed time, the active or last-completed stage, and active asynchronous-operation count, then exits with code 124. It is intentionally incapable of becoming an anonymous indefinite process.

Terminal evidence: `app_qml_startup_tests --isolated-presentation` passed in 1.49 s (and 1.14 s with verbose probe capture). The verbose run showed all 20 navigation cases passing and a final `isolated presentation complete` marker.

## Startup-readiness boundary

The probe formerly started its 16 ms heartbeat before synchronous root construction. Its first timer delivery therefore measured probe start through construction/initial presentation and emitted a runtime-style major event at time zero. That is the exact mechanism behind the retained >1 s records.

The probe now reports `startupReadiness` separately:

1. probe start to `QQuickWindow` ready;
2. window ready to first `frameSwapped` presentation;
3. first heartbeat timing and whether it preceded first presentation;
4. the first post-presentation heartbeat, which establishes the steady-state 16 ms heartbeat baseline.

Only heartbeats after that baseline contribute to `eventLoop` runtime samples and scheduler observation. The startup cost remains in the exported report.

In the bounded post-fix capture, window ready was 291.56 ms after probe start; the first heartbeat was 291.60 ms and preceded first presentation; first presentation arrived at 312.43 ms; and the runtime baseline armed at 312.80 ms with a 21.20 ms interval. The post-presentation runtime sample set had 25 samples, p95 32.86 ms, maximum 36.78 ms, and no major stalls. This is a classifier-validation capture, not a replacement hostile-load run.

## Verification

| Check | Result |
| --- | --- |
| Release build of changed targets | passed; only existing Qt QTP0004 author warnings |
| `ui_release_contract_tests` | passed via CTest, 0.08 s |
| `responsiveness_probe_aggregation` | passed via CTest, 0.06 s; validates startup-readiness export and baseline separation |
| `app_backend_startup_tests` | passed via CTest, 14.84 s; validates Normal / Pressure / Severe presentation cadence and recovery without changing mapping request truth |
| `app_qml_startup_tests --isolated-presentation` | passed via CTest, 1.49 s; bounded 20-case isolated route, watchdog retained |
| Probe classifier capture | passed; first heartbeat before first presentation, then separate runtime baseline |
| Mapping hot-path benchmark | one fresh completed sanity run; every mapping/profile-control/automation line reported `hot_path_allocations=0` |
| Persistence | remains valid from retained qualification and startup lifecycle coverage; no persistence redesign or repeated hostile persistence matrix |

## Deferred owner work

The automated campaign does not authorize native physical mouse/touchpad review, real HOTAS/vJoy/HidHide qualification, accessibility review, typography review, or owner acceptance. Those items remain **DEFERRED TO OWNER**. There is no merge, version bump, tag, release, or protected-main claim in this phase.
