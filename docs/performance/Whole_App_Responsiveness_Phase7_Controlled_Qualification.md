# Whole-App Responsiveness — Phase 7: Controlled Qualification

## Verdict and scope

Phase 7 closes the two evidence gaps left by Phase 6: the hostile runs now hold a documented total-host CPU band, and the scroll matrix uses representative Axes, Buttons, and Automation presentation fixtures. The harness objective passed in both hostile cases. The CPU-only run averaged 92.6% and the CPU-plus-disk run averaged 92.9% over explicit five-second stability windows; both meet the declared 88–97% acceptance envelope for at least 80% of those windows.

This is not a whole-app smoothness pass. Each hostile run also recorded one startup event-loop heartbeat above one second (1,305.25 ms CPU-only and 1,092.92 ms CPU-plus-disk). Those records began at the probe start and have no page attribution, but they occurred while the app was launched under the controlled load and must remain an open finding. The per-session scroll matrix completed, and every representative fixture session moved content, but neither result substitutes for native physical-pointer/touchpad review or owner acceptance.

Phase 7 changes qualification infrastructure only. It does not modify QML scroll policy, Flickable physics, bounds behavior, mapping, vJoy, DirectInput, persistence design, contention thresholds, product versioning, installers, updaters, tags, releases, `main`, or Phase 6's draft PR.

## Provenance

| Item | Value |
| --- | --- |
| Candidate branch | `codex/whole-app-responsiveness-phase7` |
| Exact Phase 6 parent | `a920c1fbe3c71212b720952d701dc894b4cd6604` |
| Phase 6 PR | #77, draft and unmerged; base remains Phase 5 `3409670d30a5b217e329f75a18a285af98da675a` |
| Worktree | `C:\\Users\\kkids\\Documents\\HOTAS_BF6-responsiveness-phase7` |
| Build directory | `C:\\hotas-builds\\responsiveness-phase7` |
| Evidence root | `C:\\hotas-builds\\responsiveness-phase7\\evidence-phase7` |
| Product QML files changed | none |

The native driver stays inside the established whole-app page list. No out-of-scope page was navigated, measured, or modified by this phase.

## Controlled-host-load method

The PowerShell harness replaces the old five-second `Win32_Processor`/WMI CPU observation with the lightweight `GetSystemTimes` total-host calculation already appropriate for the Phase 4 controller. CPU control and samples occur every 1,000 ms. Disk counters are observed only for the disk scenario, at harness sampling cadence; they are not in the CPU control loop.

One hidden task-owned helper process hosts a bounded pool of 15 managed worker threads. A four-byte named in-memory duty channel lets the parent make small closed-loop adjustments without spawning per-core PowerShell processes or writing a duty log on every turn. The threads use staggered 100 ms duty windows. The optional disk case is one bounded 128 MiB task-owned disk job. Both helpers have the same finite deadline as the workload and are stopped in the harness `finally` block.

The JSON retains every raw one-second host reading and every applied duty change. Qualification uses overlapping five-second means derived from those raw samples: this measures sustained host pressure while making the raw 76.9–100% / 78.7–100% scheduler variation visible. A run is stable only if its window mean stays in the requested 90–95% band and at least 80% of windows are in the predeclared 88–97% envelope.

| Host-load run | Worker threads | Five-second windows | Min / mean / max | In 90–95% target | In 88–97% envelope | Longest continuous envelope interval | Stable |
| --- | ---: | ---: | --- | ---: | ---: | ---: | --- |
| CPU 90–95 | 15 | 146 | 87.2 / 92.6 / 99.2% | 74.7% | 93.8% | 46 s | yes |
| CPU 90–95 + 128 MiB disk | 15 | 147 | 87.5 / 92.9 / 97.3% | 80.3% | 98.0% | 92 s | yes |

The first hostile run had 150 raw steady readings (76.9 / 92.5 / 100%) and the combined run had 151 (78.7 / 92.9 / 100%). These are retained in `cpuControlSamples` and `cpuLoadQualification.rawControlSamples`; the windowed result is not a replacement for raw data.

## Representative isolated fixtures

The opt-in native driver now installs presentation-only fixture data after navigating to a page and before assessing scrollability. It does not call mapping/configuration APIs to create these data. The native summary records the fixture inventory, so the evidence distinguishes a populated view from an empty default installation.

| Page | Isolated presentation fixture | Result in each accepted run |
| --- | --- | --- |
| Axes | 8 populated axes with mappings and response summaries | 8 scrollable sessions: normal and compact × slow/normal/rapid/reverse |
| Buttons | 32 mapped buttons and one POV | 8 scrollable sessions: normal and compact × slow/normal/rapid/reverse |
| Automation | 12 plausible automation rules | 8 scrollable sessions: normal and compact × slow/normal/rapid/reverse |

All three runs had 92 total scroll sessions, 92 scrollable sessions, zero fixture-driver failures, and zero non-scrollable Axes/Buttons/Automation records. The fixtures exist only inside the isolated native qualification process; they do not establish physical device inventory or controller behavior.

## Native-window results

The matrix retains Phase 6's definitions: slow (5 events at 100 ms), normal (10 at 35 ms), rapid (16 at 16 ms), and reverse (16 at 16 ms with a halfway direction change). The sources are Qt Test window-system wheel-angle-delta events routed to a real `QQuickWindow`, not physical pointer or touchpad input.

| Run | Exit / timeout | Worst scroll movement p95 | Worst wheel-to-frame p95 | Worst active-scroll frame p95 | Largest active-scroll frame | Input / frame / event-loop samples over 1 s |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Idle representative matrix | 0 / no | 60.00 ms | 61.08 ms | 19.75 ms | 103.46 ms | 0 / 0 / 0 |
| Controlled CPU 90–95 | 0 / no | 43.52 ms | 46.61 ms | 29.83 ms | 101.74 ms | 0 / 0 / 1 |
| Controlled CPU 90–95 + disk | 0 / no | 43.46 ms | 47.34 ms | 30.79 ms | 101.34 ms | 0 / 0 / 1 |

The two over-one-second event-loop records are startup heartbeat delays at `startedSinceStartMs: 0`; their native summaries otherwise have no failures. The hostile cases finished all 92 sessions despite those startup records. They are an evidence-backed remaining concern, not a basis for a scroll-policy rewrite or a claim that the entire app is smooth under pressure.

## Preserved boundaries

- The Phase 6 scroll instrumentation, patterns, window profiles, movement definitions, and existing page policies are preserved.
- No shared scroll component, wheel handler, `StopAtBounds` choice, velocity/deceleration setting, or nested-control behavior changed.
- The Phase 4 automatic contention policy was observed, not retuned. Both hostile runs transitioned to `severe`; presentation degrades before interaction and the mapping path remains outside this work.
- Existing Phase 5 Profiles virtualization is untouched.
- The native runs are synthetic and isolated. They are not physical HOTAS, physical mouse/touchpad, native typography, accessibility, or owner-acceptance evidence.

## Verification

| Check | Result |
| --- | --- |
| PowerShell parser for `Invoke-ResponsivenessCharacterization.ps1` | passed |
| Release build: `HOTASMapper` and `responsiveness_probe_tests` | passed; subsequent build reported `ninja: no work to do` |
| `responsiveness_probe_disabled` and `responsiveness_probe_aggregation` | 2 / 2 passed |
| Mapping hot-path benchmark | 3 completed runs; each exited 0 and preserves the benchmark's zero-allocation assertions |
| Idle native qualification | exit 0; no timeout; no driver failures; all representative fixture sessions scrollable |
| Controlled CPU native qualification | exit 0; no timeout; stable-host-load criterion met; no driver failures |
| Controlled CPU-plus-disk native qualification | exit 0; no timeout; stable-host-load criterion met; no driver failures |
| `ui_release_contract_tests` | exits 2 directly and via CTest in this clean build; no Phase 7 source touches its covered areas, so this is not claimed as a Phase 7 regression verdict |
| `app_qml_startup_tests --isolated-presentation` | did not terminate during the observed local run and was stopped only after verifying its executable and CTest parent; **NO NEW LOCAL QML VERDICT** |

The no-terminal-result QML run is not reported as a pass or a product failure. It does not alter the completed native-window qualification results.

## Follow-up boundary

Do not use this phase as authorization to retune product rendering, rewrite scroll behavior, or alter the mapping/persistence hot paths. The next decision, if desired, is a narrowly scoped investigation of the reproducible startup heartbeat stalls under controlled load, followed by separate native physical-pointer/touchpad and owner review. Those are not completed here.
