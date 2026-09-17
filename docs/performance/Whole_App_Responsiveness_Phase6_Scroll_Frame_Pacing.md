# Whole-App Responsiveness — Phase 6: Scroll and Frame Pacing

## Verdict and scope

Phase 6 adds bounded, opt-in measurement for the native-window scroll path and does **not** change product QML scroll policy. The accepted idle matrix found no common viewport defect: every scrollable page/session moved content with bounded wheel-to-content and wheel-to-first-frame measurements. Changing `boundsBehavior`, adding a shared scroll component, or retuning Flickable physics would therefore be speculation, so it was deliberately not done.

This is a native-window *synthetic* qualification, not a physical mouse, touchpad, hardware-controller, typography, or owner-acceptance result. The driver injects angle-delta wheel events through Qt Test's window-system path into an actual `QQuickWindow`; it does not claim physical-device equivalence.

The final combined run completed with exit code 0, no stress-deadline timeout, no native-driver failures, 90 seconds of sustained torture, and no recorded input, frame, or event-loop interval over one second. Its CPU trace was 99--100%, which is stronger than (but not a substitute for) a stable 90--95% band. The closest calibrated CPU-only run averaged 94.1%, but individual samples ranged from 76% to 100%; it is evidence for an average-band scenario, **not** a claim of sustained 90--95% load.

No release, version, installer, updater, main-branch, or merge action is part of this phase. The Phase 3 scheduler policy, Phase 4 contention controller, mapping hot path, persistence design, and Phase 5 Profiles virtualization are preserved.

## Provenance

| Item | Value |
| --- | --- |
| Candidate branch | `codex/whole-app-responsiveness-phase6` |
| Exact Phase 5 parent | `3409670d30a5b217e329f75a18a285af98da675a` |
| Parent subject | `Virtualize Profiles library construction` |
| Worktree | `C:\\Users\\kkids\\Documents\\HOTAS_BF6-responsiveness-phase6` |
| Build directory | `C:\\hotas-builds\\responsiveness-phase6` |
| Instrumented source files | `src/responsiveness_probe.*`, `src/native_qualification_driver.*`, and `tests/responsiveness_probe_tests.cpp` |
| Product QML files changed | none |
| Evidence root | `C:\\hotas-builds\\responsiveness-phase6\\evidence-phase6` |

The driver enumerates only Overview, Devices, Axes, Buttons, Curve Editor, Profiles, Adaptive Response, Automation, Diagnostics, and Settings. No out-of-scope interaction is navigated, benchmarked, or modified by Phase 6.

## Phase 5 baseline

The exact Phase 5 parent executable was run at idle before the Phase 6 source was introduced (`phase5-exact-idle`). It completed successfully, but it had no page-scroll session model and therefore cannot answer Phase 6's wheel-to-content or wheel-to-frame question. Its generic native-window input histogram was p99 471.09 ms (maximum 471.31 ms), frames were p99 266.61 ms (maximum 809.36 ms), and no input sample exceeded one second. That generic histogram included driver/action timing and is not comparable to the new, separately correlated scroll-session metrics.

The Phase 5 Profiles library virtualization remains untouched. The focused Dark and Light native QML fixture both passed in this phase; the fixture's large-library assertion continues to require fewer than 96 instantiated library delegates after scrolling to profile 95. Phase 6 did not change `FlightDeckProfiles.qml`, its data model, or delegate construction policy.

## Scroll inventory

The ten included page roots already use clipped Flickable/ScrollView-style surfaces with content widths constrained to the viewport and policy-controlled scroll bars. Axes, Buttons, Curve Editor, Profiles, Adaptive Response, Automation, and Settings explicitly use `StopAtBounds`; Overview, Devices, and Diagnostics retain their existing default bounds behavior. There was no page-root custom wheel handler, `flickDeceleration`, or `maximumFlickVelocity` override to normalize.

Nested lists/popups remain local to their owners (device lists, selectors, Profile Library, Adaptive Response popup, Automation popup, and Diagnostics event log). A broad nested-scroll rewrite would change focus/interaction ownership without evidence, so it is out of scope.

## Measurement definition

Each session records native-window synthetic wheel angle-delta injection, the first observed `contentY` change on the target page root, the first `QQuickWindow` frame swap correlated with that wheel, and frame intervals through a 225 ms post-wheel settle window.

The driver samples `contentY` on an 8 ms precise timer only while an opt-in qualification scroll session is active. Production page components gain no timer and no per-wheel disk write. The probe retains at most 192 sessions and 64 wheel records per session in memory, exports once at shutdown, and reports any dropped record/session count.

Qt's window-system injection API does not expose whether a QML item accepted a wheel event. Consequently the JSON labels delivery as `not observable through QTest window-system injection`; zero accepted and unaccepted counters are not treated as a dropped-wheel claim. Content movement, first visible movement latency, and frame correlation are the observable evidence.

Patterns are slow (5 events at 100 ms), normal (10 at 35 ms), rapid (16 at 16 ms), and reverse (16 at 16 ms with direction change midway). The matrix uses normal 1320x840 and compact 900x650 for every included page, plus large 1600x1000 for Devices, Adaptive Response, and Diagnostics.

## Rejected qualification attempts and failure classification

Two early instrumented idle runs are rejected, not reported as product results. The first lacked continuous motion sampling; the second used direct `QCoreApplication::sendEvent` delivery and did not deliver rapid wheel cadence through the native window path. The final driver uses Qt Test's window-system wheel injection instead.

| Classification | Evidence-backed disposition |
| --- | --- |
| Common page-root scroll defect | Not found in the accepted idle matrix; no product scroll-policy change. |
| Automation fixture | Not scrollable in the normal/compact fixture (two idle records); correctly recorded rather than forced. |
| Axes and Buttons under CPU/combined fixture | The fixture reported no scrollable content at normal/compact size (four records per hostile run). This is a fixture limitation, not proof that physical-device content cannot scroll. |
| Child-control interaction | Under near-saturation CPU, the Adaptive slider and Profiles search control remained available; one wheel interaction changed neither control value/text nor the viewport. |
| Inherited source contract debt | `ui_release_contract_tests` is 32 pass / 2 fail on both the Phase 5 baseline and this candidate; the failures expect an obsolete minimized snapshot call and `interval: 33`. Phase 6 did not modify either area. |
| Focused QML warnings | Dark/Light fixtures exit 0 but emit existing undefined bool/color binding warnings from Devices/Dialog Header. They are recorded warnings, not a passing native-owner visual review. |

## Baseline page matrix — accepted idle native-window result

Evidence: `idle-window-system-wheel\\idle-probe.json`. There were 86 sessions: 84 scrollable sessions moved content, two Automation fixture sessions were explicitly non-scrollable, zero sessions/wheel records were dropped, and no driver failures occurred.

| Pattern | Scrollable sessions | Worst session movement p95 | Worst wheel-to-frame p95 | Worst active-scroll frame p95 |
| --- | ---: | ---: | ---: | ---: |
| Slow | 21 | 35.46 ms | 39.40 ms | 20.03 ms |
| Normal | 21 | 17.51 ms | 32.00 ms | 19.11 ms |
| Rapid | 21 | 32.97 ms | 48.10 ms | 18.97 ms |
| Reverse | 21 | 31.71 ms | 35.93 ms | 18.95 ms |

The worst movement observation was Diagnostics compact/slow (35.46 ms). The worst wheel-to-frame observation was Overview normal/rapid (48.10 ms). The largest individual active-scroll interval was 73.82 ms on Devices compact/slow; it was isolated, while the corresponding session p95 remained within the table above.

## Compact and hostile results

The compact page class is included in every row above. Its idle worst was Diagnostics compact/slow at 35.46 ms movement p95. Under near-saturation CPU (96--100%, 98.9% average), all 84 scrollable sessions moved content and the worst compact slow session was 44.85 ms movement p95 / 49.86 ms wheel-to-frame p95. Its worst active-scroll p95 was 32.86 ms.

The separately calibrated CPU-only run used 16 bounded stress threads and averaged 94.1% CPU (76--100% instantaneous samples). It completed with no driver failures, 76 scrollable sessions moving content, zero dropped sessions/wheel records, and worst values of 56.09 ms movement p95, 59.06 ms wheel-to-frame p95, and 50.17 ms active-scroll p95. The missing Axes/Buttons movement is the fixture limitation described above.

The final CPU-plus-disk run (`final-combined-90s`) used a 128 MiB bounded disk worker, ran 90 seconds of continued native-window wheel activity, and completed before its 300-second deadline:

| Metric | Result |
| --- | ---: |
| Observed CPU | 99--100% (100% average) |
| Native qualification failures | 0 |
| Scroll sessions / moving sessions | 80 / 76 |
| Dropped sessions / wheel records | 0 / 0 |
| Worst movement p95 | 61.79 ms, Diagnostics large/rapid |
| Worst wheel-to-frame p95 | 65.38 ms, Diagnostics normal/slow |
| Worst active-scroll frame p95 | 52.54 ms, Diagnostics normal/slow |
| Largest individual active-scroll interval | 107.85 ms, Devices compact/slow |
| Input p99 / maximum | 91.46 ms / 326.01 ms |
| Frame p99 / maximum | 184.69 ms / 693.89 ms |
| Event-loop delay p99 / maximum | 30.45 ms / 914.28 ms |
| Input/frame/event-loop samples over one second | 0 / 0 / 0 |

The global histograms are bounded at 4,096 retained samples and report dropped historical samples (29,906 input, 3,927 frame, and 4,899 event-loop in the combined run). The per-session scroll records above are the correct measure for scroll pacing; neither those generic histograms nor the synthetic driver substitute for owner acceptance.

## Whole-app comparison and persistence

The final combined result is materially more hostile than the requested CPU-plus-disk scenario: CPU was pinned at 99--100%, the Phase 4 controller transitioned twice to `severe`, decorative motion was disabled, background poll multiplier was four, and the GUI scheduler policy remained selected. No scheduler/controller threshold was changed.

The isolated 100-edit persistence burst remained asynchronous and safe for the GUI: 201 requests, durable generation 201, 48 coalesced writes, and zero failures. GUI snapshot capture was p99 0.0812 ms (maximum 0.1584 ms) and GUI enqueue p99 0.0005 ms (maximum 0.0006 ms). Under the deliberately saturated combined run, worker persistence is the remaining observed bottleneck: worker-total p95 8,546.68 ms and maximum 14,321.75 ms, including worker serialization/sync. Those values did not block the measured GUI snapshot or enqueue path and do not justify a persistence redesign in Phase 6.

## Chosen implementation

Phase 6 changes only the opt-in native qualification and probe model:

- `ResponsivenessProbe` owns bounded scroll-session aggregation and JSON export, including wheel-to-movement, wheel-to-frame, and active-scroll frame distributions.
- `NativeQualificationDriver` navigates the allowed pages, injects window-system wheel angle deltas, drives normal/compact/large cases, records precise fixture limitations, and keeps torture activity on actual page surfaces.
- Unit coverage verifies the disabled path and aggregation behavior.

There is no production scroll delegate, timer, common scroll component, QML wheel handler, physics change, mapping change, or persistence write on a movement event. All page roots, `StopAtBounds` choices, nested controls, and Phase 5 Profiles virtualization are deliberately unchanged.

## Verification

| Check | Result |
| --- | --- |
| Release build of changed targets | passed; subsequent build reported `ninja: no work to do` |
| `responsiveness_probe_disabled` and `responsiveness_probe_aggregation` | 2 / 2 passed |
| Mapping hot-path benchmark | three completed runs; every reported `hot_path_allocations=0` |
| Focused native QML visual fixture, Dark | exit 0 |
| Focused native QML visual fixture, Light | exit 0 |
| UI release contract | inherited 32 pass / 2 fail on both Phase 5 and Phase 6; no Phase 6 regression evidence |
| Final combined hostile qualification | exit 0, no deadline timeout, 90 seconds recorded, zero driver failures |

The QML fixtures are isolated/offscreen tests. They do not prove native owner pointer feel, typography, touchpad behavior, physical hardware input, or owner acceptance.

## Phase 7 recommendation

Do not begin Phase 7 in this change. The next phase should first tighten the stress harness itself: hold a demonstrably stable 90--95% CPU band, provide a representative Axes/Buttons content fixture under load, and reproduce the physical native pointer/touchpad and owner-acceptance review. If worker persistence tail latency remains relevant after that controlled qualification, investigate it separately without changing the GUI snapshot/enqueue boundary. No broad scroll redesign is recommended from the Phase 6 evidence.
