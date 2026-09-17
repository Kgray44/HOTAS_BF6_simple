# Whole-App Responsiveness Phase 5 — Profiles Construction and Virtualization

## Provenance and boundaries

This is an unreleased, unversioned Phase 5 candidate on codex/whole-app-responsiveness-phase5, based exactly on Phase 4 commit c193a10b4b4e492ceaae2a62b2d93906ec05a7cb. At branch creation and final remote fetch, origin/main was bdedc52848ec18ddc4f6a387776526dcde1c7ce5. This stacked candidate was not rebased, merged, tagged, released, or used to alter version, installer, or updater metadata.

The change is limited to the Profiles presentation construction path and its qualification seams. It preserves Phase 3's Normal process / GUI +1 / render-inherited +1 / persistence -1 scheduler relationship, Phase 4's controller and thresholds, asynchronous persistence, and the DirectInput -> MappingWorker -> vJoy hot path. No MappingWorker dependency was added. Signal Flow was not navigated, profiled, benchmarked, or changed.

The native results use the existing isolated-storage, timer-driven QQuickWindow qualification driver. They are synthetic native-window input, not physical HOTAS, vJoy-output, native-pointer/typography, or owner-acceptance evidence. Owner acceptance remains deferred. Local evidence is under C:\hotas-builds\responsiveness-phase5\evidence-phase5.

## Construction baseline and dominant cost

The Phase 4 final 90-second Combined run was fully hostile (100% CPU plus a bounded 128 MiB disk workload) and recorded a 4004.7 ms Profiles navigation maximum. Its trace identified several 3–4 second GUI-user-CPU intervals during Profiles loader activation.

Source inspection before this change confirmed two eager sources:

1. The visible library used a nested category Repeater and every-profile Repeater, so all rows were created on activation.
2. The historical card library plus category and profile detail trees were instantiated behind visible: false; hidden QML still constructs its objects and bindings.

Qualification-only counters now split loader/object timing (existing native probe), root creation, Component.onCompleted, restorePresentationState, refreshRunningApplications, library-model preparation, row/delegate creation, detail construction, selected-detail resolution, layout stabilization, and first frame.

| Final Combined page-local stage | Worst observed |
| --- | ---: |
| Root creation marker | 44 ms |
| refreshRunningApplications | 51 ms |
| Library model preparation | 94 ms |
| Selected-detail resolution | 6 ms |
| Layout stabilized | 104 ms |
| First presented frame marker | 110 ms |
| Live library profile delegates | 5 |

Across 26 final hostile Profiles navigations, request-to-loader/object-ready peaked at 91.7 ms and request-to-first-frame peaked at 96.9 ms. The page-local first-frame marker can arrive after a rapid unload, so it is retained as a separate upper observation rather than combined with the navigation statistic.

## Before and after architecture

FlightDeckProfiles now builds page-local category/profile ID indexes and membership arrays only at actual Profiles model, category model, filter, search, or compatible-Rig boundaries. It creates flattened libraryRows of category and lightweight profile-summary rows, displayed by a clipped ListView with a 180 px cache buffer and delegate reuse.

The existing row keeps category indentation, selection/active state, profile drag source, category drop target, New Category drop target, and all established backend command calls. The historical library is retained only as an inactive Loader source component; category and profile detail views also use real loaders, so their large trees do not exist while the library is displayed.

The small immediate library detail remains deliberate (four summary tiles). selectedDetail is refreshed only for the selected profile and relevant profile-model changes. No profile command, activation, category mutation, Rig association, automation relationship, or Adaptive Response route changed.

## Deterministic Profiles fixtures and delegate evidence

The focused native-QML presentation pass uses presentation overrides only; it does not write owner profiles or activate a runtime profile. It covers first load, ALL/ACTIVE/GAME filtering, search, category selection, profile selection, detail opening, activation button state, state restoration, existing drag/drop contracts, and hierarchy rendering.

| Fixture | Flattened rows | Live category + profile delegates | Reading |
| --- | ---: | ---: | --- |
| Small: 2 categories, 4 profiles | 6 | 2 + 4 = 6 | All rows fit in the viewport. |
| Medium: 6 categories, 24 profiles | 30 | 4 + 14 = 18 | 12 rows are not instantiated. |
| Large: 12 categories, 96 profiles | 108 | 3 + 20 = 23 | 73 profile/category rows are not instantiated. |

The large fixture scrolls to the final profile and asserts that the live count remains below 96 while that row is rendered. This proves viewport-plus-cache construction rather than an eager 96-row library. Both Dark and Light focused Flight Deck passes completed. The pass also restores a saved library view, selected category/profile, filter/search, and content position.

## Idle performance

The automatic Idle driver completed with the controller Normal, zero transitions, zero driver failures, and durable persistence generation 201. Its three Profiles navigations had a 50.3 ms maximum request-to-first-frame time. The small live model recorded one category delegate, three profile delegates, and four detail tiles; no full hidden category/profile tree was constructed. Synthetic input p99/max were 884.6 / 884.7 ms; this is not a physical pointer judgment.

## Severe performance and final torture comparison

The final run reused the same qualification driver, page sequence, persistence burst, automatic Phase 4 controller, GUI scheduling policy, CPU target, and 128 MiB disk workload. The final load record reports 98.8% average CPU, 100% maximum CPU, no timeout, app exit code 0, 90 torture seconds, 757 driver actions, and zero driver failures. The controller entered Pressure at 991 ms and Severe at 1824 ms, ending Severe with established 83 ms presentation intervals, 4x background polling, and decoration disabled.

| Metric | Phase 4 final | Phase 5 final |
| --- | ---: | ---: |
| Profiles navigation max | 4004.7 ms | 96.9 ms |
| Profiles cold first-frame | Not separately captured | 50.3 ms Idle max |
| Profiles repeated reconstruction | 3–4 s GUI construction observed | 26 hostile navigations, 96.9 ms max |
| Instantiated profile/category delegates | Eager category + profile hierarchy | 23 for 12 categories / 96 profiles |
| Input p99 | 3454.5 ms | 822.7 ms |
| Input max | 4005.0 ms | 922.5 ms |
| Input >1 s | 92 | 0 |
| Input >5 s | 0 | 0 |
| Event-loop max | 6546.0 ms | 1579.4 ms |
| Frame max | 6571.6 ms | 1851.0 ms |

The Profiles navigation target is met without weakening the hostile load. The retained event-loop/frame tails are no longer Profiles construction stalls; they remain desktop-contention observations for a later measured phase.

## Visual and behavior contracts

- Category hierarchy, indentation, fonts, spacing, selected state, active badges, library detail pane, and Flight Deck visual language are retained.
- ALL, ACTIVE, GAME, search text, compatible-Rig ordering, library/category/profile views, and lightweight unload/reload state remain intact.
- LibraryRow retains DragHandler, profile drag source, category DropArea, New Category drop area, and established backend mutation calls.
- Virtualized rows contain summary data only; full profile detail is requested only for the selected profile.
- The Dark and Light focused passes found no blank rows, stale row names after the large-model scroll, or selected/detail-state regression.

## Mapping benchmark and persistence

Three standalone mapping_hot_path_benchmark samples completed successfully. Every mapping, profile-control, and automation scenario reported hot_path_allocations=0. This established synthetic benchmark has no DirectInput device or vJoy driver call and does not claim physical mapping proof.

The final hostile run preserved asynchronous persistence: 201 requests, 104 coalesced writes, durable generation 201, zero failures, no GUI-thread saves, GUI snapshot-capture p99 0.0444 ms, and GUI enqueue p99 0.0004 ms. Worker I/O remained off the GUI under the intentionally hostile workload.

## Release build and regression gates

- Release HOTASMapper build: passed.
- Focused app_qml_startup_tests --isolated-presentation Flight Deck visual pass: passed for Dark and Light with Phase 5 fixtures.
- ui_release_contract_tests: passed.
- Three mapping benchmark samples: passed with zero allocations.

The broad local app_qml_startup_tests CTest entry was started before source changes but did not produce a terminal local result, so it is deliberately not presented as a passing full-suite verdict. The focused native-QML passes above are the completed applicable regression evidence.

## Remaining bottlenecks and Phase 6 recommendation

Phase 5 removes the measured multi-second Profiles construction bottleneck. Do not expand this branch into a general QML or scrolling rewrite. If Phase 6 is authorized, first remeasure whole-app wheel-to-frame pacing under the same load: the 1579.4 ms event-loop maximum and 1851.0 ms frame maximum are the remaining measured tail. Target a specific page loader or narrow notifier fanout only if new evidence identifies it as comparable to the resolved Profiles cost.

Owner acceptance, protected-main integration, merging, versioning, tagging, and release remain explicitly deferred.
