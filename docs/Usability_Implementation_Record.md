# Pass A, Pass B, and Pass C usability implementation record

## Boundary and comparison baseline

- Governing material: `HOTAS_BF6_Usability_Implementation_Map_v1.0.md`, `HOTAS_BF6_Pass_A_Codex_Prompt.md`, the owner correction addendum dated 2026-09-16, and the Rig Details and Banner addendum dated 2026-09-17.
- Candidate: `codex/usability-setup-guidance` in `C:\Users\kkids\Documents\HOTAS_BF6-usability-setup-guidance`.
- Comparison baseline: accepted `origin/main` at `bdedc52848ec18ddc4f6a387776526dcde1c7ce5` (`v2.6.6`). This candidate was reconciled with that baseline by merge commit `ffd87a4`.
- Reconciliation conflicts: `qml/FlightDeckOverview.qml`, `qml/Standard.qml`, and `tests/app_qml_startup_tests.cpp`. The resolutions retained accepted v2.6.6 theme/pointer behavior and this pass's non-activating editor selection. No unresolved ownership conflict remains.
- Scope: accepted Pass A/Pass B corrections plus the directed Pass C presentation policy. No merge, release/tag, installer change, physical driver change, owner-data reset, or machine-wide configuration change is authorized or performed.

## Ownership retained and extended

| Concern | Owner and Pass A result |
| --- | --- |
| Profile/editor/runtime context | `AppBackend` selected, active, effective, and effective-source projections. Flight Deck now compares canonical IDs, preserves editing-only selectors, and displays Rig/Input/Output context. |
| Setup journey | Devices and Profiles retain their canonical creation, association, resolver, and output ownership. A newly created Rig now presents blank/copy/existing-profile next actions in the same journey. |
| Overview | `FlightDeckOverview.qml` now presents Current Setup, one prioritized Attention task with disclosure, expandable connection evidence, and Live Controls with physical and mapped states. |
| Input inspection | An active Rig member uses its exact published member snapshot. An unassigned but discovered controller uses an owner-requested, bounded nonexclusive DirectInput read-only probe on a control-plane thread. No probe changes configuration, activation, drivers, vJoy, or mapping state. |
| Mapper hot path | No report-loop allocation, persistence, QML, driver, or UI work was added. The new DirectInput probe is outside `MappingWorker::run()` and does not create an output session. |

## Pass A status

| Item | Status | Evidence / limit |
| --- | --- | --- |
| A-01 | Implemented and test-covered | Fixture coverage begins with a distinct existing controller and added-controller journey. Native hardware is not implied. |
| A-02 | Implemented and test-covered | Canonical active/editing/effective IDs, source-aware override display, responsive context layout, and no first-profile selector fallback. |
| A-03–A-05 | Implemented and test-covered | Devices creates a Rig, immediately offers Profile workflow, preserves rig-owned output and mapping-on/off choice, and retains explicit resolver paths. |
| A-06 | Implemented and test-covered | Operational four-area Overview replaces the seven-card System Health grid; actions carry exact Rig/controller context and details are disclosed rather than dominant. |
| A-07 | Implemented; final evidence recorded with an explicit full-suite limit | All non-QML registered tests passed after their executables were built, and the focused native-QML journey passed. A later all-tests attempt could not produce a valid 15-test verdict because the QML test remained responsive but exceeded the 15-minute observation threshold; it was ended and is not reported as a pass. |
| A-08 | Implemented and test-covered, hardware review pending | Exact active-member axes/buttons/POVs and input-only probe state are shown. Fixture proves another member cannot advance the requested member's activity. Real access failures remain explicitly reported by DirectInput. |

## Evidence boundaries

- `--isolated-presentation` is presentation-only: it does not prove physical discovery, DirectInput acquisition, vJoy output, or HID/HidHide behavior.
- Fixture and synthetic benchmark results do not prove a connected physical controller, actual mapped output, driver behavior, or owner acceptance.
- The safe review candidate must be run without enabling mapping or changing machine configuration. Owner hardware review should: select the existing HOTAS arrangement; open Devices; inspect a newly connected pedals controller with **Test Input**; confirm axes/buttons/POVs belong only to that controller; create a pedals Rig; choose an explicit Profile path; assign a control; and return to the HOTAS arrangement unchanged.
- Final compiler, CTest, QML lifecycle, synthetic baseline/candidate benchmark, and native review evidence are appended only after their respective commands complete successfully.

## Build and test evidence (2026-09-16)

- The corrected production target built in an isolated output directory with Visual Studio 2022 Build Tools, Qt 6.8.3, `BUILD_TESTING=ON`, and `HOTAS_BUILD_PERFORMANCE_BENCHMARK=ON`.
  - Candidate executable: `C:\hotas-builds\usability-setup-guidance-review\Release\HOTAS BF6.exe`.
  - Linked size: 14,147,584 bytes.
  - It was launched with `--isolated-presentation` and remained a responding process from that exact path. This verifies a safe candidate launch only; the desktop-capture surface did not expose a native window for visual inspection.
- Production-path focused coverage passed before the final test sweep:
  - `app_backend_startup_tests` (including the exact-member read-only input inspection fixture): passed.
  - `ui_release_contract_tests`: passed.
  - `app_qml_startup_tests`: passed in 204.71 seconds. Its journey drives the actual QML Device Rig creation and Blank Profile route by pointer input, then verifies that it did not activate the new Rig or change the active runtime Profile.
- All registered test executables were then built individually without rebuilding the locked product executable. The following CTest invocation passed all 14 selected tests:

  ```text
  ctest --test-dir C:\hotas-builds\usability-setup-guidance -C Release --output-on-failure -E ^app_qml_startup_tests$
  ```

  It included `mapping_core_tests`, `automation_engine_tests`, `launcher_core_tests`, `crash_diagnostics_tests`, `controller_readiness_tests`, `hidhide_health_tests`, `ui_release_contract_tests`, `widgets_startup_contract_tests`, both source contracts, `font_resolution_contract`, `app_backend_startup_tests`, `theme_manager_tests`, and `mapping_hot_path_benchmark`.
- **15-test full-suite limit:** the first all-tests CTest attempt initially found several unbuilt executables. Once the executable precondition was corrected, the QML test was observed again while other product instances were already running. It stayed responsive but had no terminal result after 15 minutes (its focused run was 204.71 seconds), so the self-started CTest/QML processes were ended. This is **NO FULL 15-TEST VERDICT**, not a QML pass or a product failure claim. The focused QML pass and the 14-test pass remain valid, narrower evidence.

## Targeted review correction (2026-09-17; local evidence before new CI)

- Scope remains Pass A only. The correction changes no driver, installer, release, mapper-loop allocation behavior, or mapping semantics.
- Active-Rig input inspection now enumerates exact canonical DirectInput slots from the discovered/saved capability bitmap instead of treating a count as slots `0..N-1`. The read-only fixture covers X/Y/Rz, Rz-only, and Slider1-only controllers, including disabled/unmapped axes and a nonzero Rz value in slot 5. It also retains the existing proof that a different Rig member cannot alter the inspected member's report.
- The Flight Deck Mapping control keeps **Stop Mapping** enabled while a request is outstanding even after virtual-output readiness is lost. A no-request invalid output still cannot start Mapping.
- The physical-input dialog now formats POV raw hundredths-of-a-degree values as degrees, preserves raw values and native ranges in a collapsed Technical Details disclosure, distinguishes `CENTER` (`-1`) from invalid directions, and uses per-section empty states when a current buttons-only or POV-only report is valid.
- Overview review now passes a re-resolved structured `AppIssue` target. Device, output, Rig, Profile, and Signal Flow targets use their canonical IDs; an unavailable target shows an explicit current-review explanation rather than falling back to an unrelated controller. No review action approves a driver change.
- Local build: the cached Release `app_backend_startup_tests` and `app_qml_startup_tests` targets compiled successfully with VS Build Tools and Qt 6.8.3.
- Fresh isolated review artifact: `C:\hotas-builds\usability-setup-guidance-review-targeted\Release\HOTAS BF6.exe` built at 14,171,648 bytes. It stayed responsive for a five-second `--isolated-presentation` launch check; that self-started candidate process was then ended. This proves launchability only, not native visual review, physical hardware, or driver behavior.
- Local focused results: `HOTAS_STARTUP_FOCUSED_TEST=multi-controller` and `HOTAS_STARTUP_FOCUSED_TEST=read-only-input` both passed under the CTest-equivalent offscreen/plugin environment.
- `ui_release_contract_tests` also passed with the Qt runtime environment. Its first direct invocation exited before the test body because the Qt runtime path was absent; that runner setup result is not recorded as a contract-test failure.
- Local QML limit: the single Flight Deck visual/lifecycle path was launched after the successful QML compile but stayed idle without a terminal result and was ended. This is **NO NEW LOCAL QML VERDICT**, not a pass or product-failure claim. It is separate from the older 15-test no-verdict above. The prior head's GitHub CI remains historical evidence only; a new head requires a new CI result.
- New-head CI: commit `0940adc9c86f71438a07438298af7f04e98befab` passed GitHub **HOTAS BF6 CI** run `35184815806` and its paired **Documentation Check** run `35184815810` on 2026-09-17. The CI completed qualified-Qt QML lint, Release configuration, compilation of the mapper and all tests, and its mapping/automation/launcher/readiness/benchmark suite. This CI result covers the corrected source; it does not convert either local QML no-verdict into a local pass or prove native visual review, physical hardware, or driver behavior.

## Rig Details and context-banner presentation correction (2026-09-17)

- Scope remains Pass A only. The correction is limited to the Flight Deck shared context strip, the Flight Deck header's scaled height, Rig Details composition, its shared dialog body budget, focused QML lifecycle coverage, and this implementation record. It does not change mapping, resolver, driver, installer, release, or persistence semantics.
- The passive context strip now uses plain-language editing, active, and temporary-profile descriptions; keeps active Rig, viewed-controller, and active-output labels visible as text reflows; and places canonical profile, Rig, controller, and output IDs in an explicit Technical Details disclosure. It does not issue selection, activation, or mapping commands.
- Rig Details now presents name/status, a concise setup-impact message only when attention is warranted, Required/Optional controller rows, default profile, Virtual Output summaries, profile-reference count, and a fixed footer first. Rename, member routing/verification, add-controller, output administration, raw identities, and destructive Rig actions are explicit on-demand disclosures. `Use this rig` remains the existing explicit resolver-backed command; merely opening, resizing, changing text size/theme, or cancelling Rename is presentation-only.
- The shared dialog body budget reserves footer height when a footer exists, preventing the fixed action row from being hidden by long, scaled content while preserving the body budget of other dialogs.
- Focused test coverage drives duplicate-name and same-ID runtime-override context presentation, empty profile context, technical disclosure, Rename cancel, member/output/technical/Rig Action disclosures, deletion confirmation without deletion, and Small/Medium/Large/Extra Large type changes plus a real ThemeManager appearance change with Rig Details open.
- The focused `app_qml_startup_tests` Flight Deck visual matrix passed for Dark and Light in 69.76 seconds on 2026-09-17. It produced summary, expanded-member, and Extra Large 900×650 captures in `C:\hotas-builds\usability-setup-guidance-evidence\rig-details-20260917-final`. The captures were inspected for composition, disclosure, and footer containment. They are offscreen structural evidence only; the offscreen font fallback means they do not establish native typography, native pointer acceptance, hardware behavior, or owner acceptance.
- Fresh isolated owner-review candidate: `C:\hotas-builds\usability-setup-guidance-rig-details-review\Release\HOTAS BF6.exe` (SHA-256 `05C3FD32A8CB96EE752320B8EAAD5B992EF2294BEC8893B9E0CD6FCC40B0900B`). It was copied from the clean Release build, launched with `--isolated-presentation`, and remained responsive after five seconds. It remains open for owner review; this establishes only safe candidate launchability, not native visual acceptance, physical input, output, or driver behavior.

## Approved final Pass A presentation checkpoint (2026-09-17)

- Owner direction retained the readable, scale-aware context semantics and summary-first Rig Details workflow, then narrowed the final Pass A work to two visual corrections before Pass B: the persistent context must be a compact toolbar and Rig Details must carry the dialog shell's rounded lower silhouette.
- `FlightDeckContextStrip.qml` now keeps editing/active/effective profile distinctions, an optional active-rig summary, and labeled Details access in a compact wrapping toolbar. The full active-rig/viewed-controller/output scope, contextual help, and canonical identifiers live in an Escape-closeable, focus-restoring Details popover rather than permanent header rows. This is passive presentation only.
- Rig Details' stationary footer now uses the qualified Qt 6.7 per-corner `Rectangle` properties: a straight body/footer seam, rounded lower fill, and the shared dialog's single outer perimeter. No action target, activation behavior, or scroll budget changed.
- The focused `app_qml_startup_tests` Flight Deck path passed in 53.48 seconds after the corrections. It covers the compact default strip, duplicate/effective/missing-profile context, pointer-opened Details and focus return, all supported type sizes, Dark/Light dialogs, expanded member content, and lower footer corner geometry. This remains automated structural evidence; native typography and owner review are separate.

## Pass B setup journey candidate (2026-09-17)

- Foundation B is the distinct local Pass A checkpoint `27efc111a8882dfd76e9e18e78ee6370f1b0141b` (`Polish final Pass A context and dialog shell`), whose parent is preserved candidate `bf4bed1`. The Pass B work starts after that checkpoint and does not reopen its approved presentation direction.
- `FlightDeckSetupAssistant.qml` is the single Flight Deck dialog host. It presents the four conceptual stages **Controllers**, **How you'll use them**, **Prepare connection**, and **Configure / test** using the existing theme and dialog shell. Overview, Devices (including an empty/new-device route), Profiles, a prioritized setup issue, and the compact conditional **Return to setup** context action all route to that host.
- `AppBackend` persists only a bounded, versioned setup-task journal in `QSettings`: intent, stable object IDs, stage, selected output/category, and operation references. It stores no mapping data, repair command, driver consent, or replacement runtime. A corrupt record is discarded as guidance; stale IDs become an explicit review requirement; one saved task may be resumed, and a replacement is permitted only before any committed operation reference exists.
- Next/back and saved choices are non-mutating. The named commit paths delegate to the established Device Rig/Profile/member APIs, keep a partial Rig commit visible if Profile creation cannot complete, show existing Profile impact before shared membership is added, and do not activate mapping. The final **Use this setup** action alone delegates to the existing resolver-backed activation transaction; it does not claim physical input, game visibility, or hardware qualification.
- Opening Axes or Buttons selects the exact Profile/Rig/device editor context only. Returning to the host retains the task journal; it does not create a second header or a second setup/repair authority. Connection evidence is rendered through the existing scoped Setup Health service and `ControllerReadinessPanel`.
- Release build targets `HOTASMapper`, `app_qml_startup_tests`, and `mapping_hot_path_benchmark` completed successfully. The focused `setup-task-coordinator` backend fixture passed, as did the dedicated native/offscreen `app_qml_startup_tests` run (174.25 seconds), `ui_release_contract_tests` (0.16 seconds), and `mapping_hot_path_benchmark` (7.98 seconds). The final registered Release CTest run passed **15/15** in 221.04 seconds; its native QML path passed in 168.25 seconds and its hot-path benchmark passed in 7.94 seconds.
- Those checks prove the bounded coordinator, Flight Deck entry interaction, generated QML, existing control-plane contracts, and unchanged synthetic mapper benchmark path. They do not claim physical-controller qualification, native typography/pointer review on the owner display, game visibility, signed release status, or owner acceptance.

## Pass B owner-review correction candidate (2026-09-17; unmerged)

- The single persistent Flight Deck top-bar action now says **Guided setup** without a task and **Continue setup** with one. Overview, Devices, and Profiles no longer duplicate generic entry buttons. A different setup path changes the active uncommitted task in place; committed work requires an explicit separate task and is never reinterpreted.
- The setup task journals bounded drafts (names, exact controller/Rig/output/category/Profile/copy selections, membership requirement, return page, operation references, test states, and repair scope) with `QSettings::status()` checked before a claimed save. Invalid saved selections remain unselected; no first-list fallback is used.
- Device Rig/Profile commits journal stable planned IDs before canonical effects, reconcile an observed post-crash operation, and retry a partial Profile failure against the original Rig. Profile-for-Rig passes the selected Rig ID explicitly. Add-to-Rig accepts a discovered controller through the existing canonical identity route, records the resulting saved controller ID, lists attached Profiles, and can retain one exact Profile editor target.
- Task revalidation fingerprints controller identity/capability, Rig membership and output, output descriptor, Profile/Rig/category relationship, and copy source. An unrelated stage navigation cannot clear a stale relationship warning. Scoped Check and Retry use the task target; hosted repair has its own explicit confirmation and records that scope without activating a Rig or Profile.
- Configure/Test records physical and mapped results as `not tested`, `started`, `passed`, or `unavailable`; it does not invent a successful hardware claim. **Use** shows a persistent result summary, and **Finish** alone clears the journal.
- `FlightDeckDialogFooter.qml` is the shared lower-edge primitive for the setup host and Rig Details: the opaque footer has qualified Qt 6.7 lower-corner radii and a single straight seam. The setup ComboBox also owns its popup and delegate theme instead of mixing a themed field with a platform-default popup.
- Local focused evidence: cached Release `app_backend_startup_tests` passed after covering in-task Step 2, durable drafts, exact Profile-for-Rig target, stale reference protection, and partial-commit retry reuse. The Dark Flight Deck visual/pointer-keyboard route passed with `HOTAS_QML_FLIGHT_DECK_VISUAL_ONLY=1`; it opens the one top-bar action, chooses Step 1 by keyboard, advances by pointer, and verifies Step 2 retained its task ID. This is isolated fixture evidence only, not physical input, mapped-output, repair, driver, or owner acceptance proof.
- The final isolated Release `mapping_hot_path_benchmark` passed with zero reported hot-path allocations and zero stationary writes per report across its listed linear/adaptive, profile-control, and automation scenarios. It is synthetic report processing only; it is not physical HOTAS/vJoy evidence.
- The current review executable is launched as `C:\hotas-builds\usability-setup-guidance\Release\HOTAS BF6.exe --isolated-presentation`. It is a single native review candidate; it neither enables mapping nor changes machine configuration. A live hardware/driver/owner verdict remains **not established**.

## Pass B Guided Setup checkbox and contrast correction (2026-09-17; unmerged)

- Scope is limited to the two Guided Setup draft checkboxes, the copy-source selector, and the Rig/Profile name placeholders. It changes no setup stage, task ID policy, canonical object mutation, activation, mapping, repair, output, or driver behavior.
- `FlightDeckSetupAssistant.qml` now uses one styled Qt Quick `CheckBox` component for both **Start from an existing profile.** and the existing required-controller option. The component owns a compact rounded Flight Deck indicator, vector check mark, readable label, hover/press/keyboard-focus/disabled treatments, and keeps the base control's accessible checkbox and keyboard semantics. The text and indicator share one control; no extra click handler or persistence path was added.
- The two name placeholders use explicit `textSecondary` contrast. The dependent source-profile selector now has a readable empty prompt, arrow, popup/delegate hover treatment, and does not select a source profile merely to satisfy presentation.
- Cached Release `app_qml_startup_tests` compiled successfully. Its focused `HOTAS_QML_FLIGHT_DECK_VISUAL_ONLY=1` route passed independently for Dark and Light. It drives the existing Step 1 keyboard and Step 2 pointer journey; clicks each checkbox's indicator and label; uses Space after keyboard focus; proves task/stage identity and draft persistence; selects/resumes a source profile; verifies disabled controls do not toggle; and checks Small/Medium/Large/Extra Large at the 900×650 app minimum with the footer contained.
- Actual offscreen captures are retained at `C:\hotas-builds\usability-setup-guidance-pass-b-owner-review\evidence\checkbox` for Dark/Light and unchecked/checked states. They confirm the Flight Deck surfaces and vector mark in the test renderer. Offscreen font fallback means those images do not qualify native typography or owner acceptance.
- A side-by-side Release candidate was linked from the existing cache without touching the running review executable: `C:\hotas-builds\usability-setup-guidance-pass-b-owner-review\Release\HOTAS BF6 Pass B Checkbox Review.exe` (14,498,816 bytes; SHA-256 `911161A3E7867C02D4C3BD3C61847F433588F1AA2944657BC6DE441CF2825102`). It will be launched only with `--isolated-presentation`; this is launchability/presentation evidence, not hardware, driver, mapping, or owner-acceptance proof.

## Pass C guidance-level candidate (2026-09-17; unmerged)

### C-01 through C-05 implementation

- `ThemeManager` owns two stable presentation values, **Guided** and **Full**, separately from theme, experience, appearance, and text-size preferences. It exposes one dedicated policy notification and never reaches `AppBackend`, `persistAndApply`, activation, repair, discovery, or the mapper worker.
- Flight Deck Settings exposes **Guidance level** with themed, keyboard-focusable Guided/Full controls and a truthful explanation: it changes help/default disclosure only, never mappings. The setting applies in place without replacing the presentation Loader.
- A genuine empty installation offers a small themed choice. **Skip for now** persists Guided and records the onboarding version. Existing configuration, profile, setup-task, and UI markers choose the Full-compatible path without an onboarding interruption; connected-device count, empty inventory, and offline Rigs are never consulted as first-use evidence. First-use guidance also waits for crash recovery to close.
- The shared policy keeps **unset**, **explicitly open**, and **explicitly closed** section states distinct. Full expands untouched Diagnostics, Adaptive Response, and Curve details; Guided keeps the same actions behind visible, keyboard-accessible disclosures. An explicit local choice survives restart and wins over a later level change.
- Guidance changes preserve the current page object, task ID/stage/drafts, mapping-request state, active/editing context, and repair confirmation. The accepted rounded dialog/footer, input, selector, and checkbox implementations are reused unchanged.

### C-04 capability and page coverage

| Surface | Guided treatment | Full treatment | Capability boundary |
| --- | --- | --- | --- |
| Overview, Devices, Axes, Buttons, Profiles, Automation, Signal Flow | Existing concise task-first hierarchy remains; all existing actions and deep links remain visible. | Same destinations, ordering, editors, graph, and actions. | No duplicate page tree, identity remapping, activation, or topology change. |
| Curve Editor | Curve details remain reachable through its existing named disclosure. | Untouched Curve details open by default. | Existing curve math, undo, snapshots, and canonical editor commands remain authoritative. |
| Adaptive Response | Advanced tuning, traces, and Test Lab remain named disclosures. | Untouched engineering detail opens by default. | Existing context, presets, simulation, and runtime configuration are unchanged. |
| Diagnostics | Input/output/isolation/event/technical detail is compact but explicitly reachable. | Untouched diagnostics detail opens by default. | Warnings, evidence, repair scope, and deep links remain equally truthful. |
| Settings and dialogs | Explains what the preference changes and preserves ordinary controls. | Same Settings and dialog actions. | Theme, text size, appearance, task/repair confirmation, and canonical settings remain independent. |
| Legacy, Standard, Top Gun, and Day Ops hosts | Inspected and intentionally unchanged where no appropriate Flight Deck disclosure exists. | Same established host behavior. | Flight Deck chrome is not forced onto another presentation. |

### C-06 focused evidence

- `theme_manager_tests` covers fresh Guided and Full selection, skip-to-Guided, restart persistence, invalid-value recovery, existing-user/no-hardware-safe upgrade behavior, explicit disclosure precedence across Guided/Full/restart, repeated same-level no-op, mapper-payload preservation, and write failure with no falsely changed visible selection.
- The Release `app_qml_startup_tests` lifecycle journey drives the actual Settings controls, observes meaningful Guided/Full defaults on Diagnostics and Adaptive Response, and compares the canonical Flight Deck configuration snapshot plus setup task before/after. It also changes guidance while the accepted Step 2 assistant is open with checkbox drafts, and while the existing repair confirmation is open; task, drafts, active Rig/Profile, mapping request, and configuration stay unchanged.
- The same journey retains the accepted pointer/keyboard Step 2, exact controller, profile-copy/required-member checkbox, task resume, repair confirmation, editor, deep-link, Dark/Light, text-size, minimum-window, and responsiveness fixtures. Its complex profile/device/Automation/Adaptive/Signal Flow fixtures remain the canonical semantic source; no guidance setter reads or writes their configuration.

### C-07 qualification and T01–T22 matrix

| IDs | Status on this candidate | Evidence boundary |
| --- | --- | --- |
| T01, T12–T14 | Covered | Existing configuration/task markers preserve upgrade and interrupted-task behavior; offscreen fixtures are not hardware proof. |
| T02–T08, T17 | Retained and exercised by existing Rig/Profile/editor/deep-link fixtures | Canonical Rig/Profile ownership and stable IDs are unchanged by guidance. |
| T09–T11, T16 | Retained and exercised by existing readiness/repair/read-only fixtures | No driver, UAC, or real-device claim is made from isolated runs. |
| T15, T18 | Retained with Guided advanced disclosures and existing sparse-input/complex-profile fixtures | Guided does not remove mappings, controls, or processing. |
| T19 | Covered directly | Both ThemeManager and production Settings changes preserve task, repair dialog, configuration, and mapping-request state. |
| T20 | Covered by the existing Dark/Light, text-size, minimum-window, keyboard, dialog/footer, and focus fixtures | Offscreen glyph replacement remains a typography limit. |
| T21 | Covered by the existing controlled navigation/slider workload | Final offscreen slider burst reports p95/p99 values in the final test log; it is not native-display or arbitrary-OS-starvation proof. |
| T22 | Covered: the final integrated cached Release test-target build completed; full CTest passed 18/18 in 76.72 seconds | The pre-existing normal-output executable was locked by its owner review process, so the product was linked as a side-by-side review executable. The existing benchmark gate passed; no new allocation claim is inferred beyond that gate. |

### Evidence limits

- The final focused QML journey completed with the offscreen platform and disabled external setup inspection. It proves generated QML, pointer/keyboard routing, policy behavior, and fixture invariants only.
- Offscreen captures with replacement-box glyphs are structural evidence only. They do **not** establish native Windows typography, native pointer acceptance, physical controller acquisition, vJoy/HidHide behavior, UAC, game visibility, or owner acceptance.
- The final owner-review candidate is staged separately from the owner’s existing review process at `C:\hotas-builds\usability-setup-guidance\Release\HOTAS BF6 Pass C Integrated Review.exe` (SHA-256 `3ABEC99E3079E2B36EC362C06F570E435A2A65B7976E81AC0D5F9CC7931CB18D`). It must use isolated settings and a normal Windows launch; no owner configuration, mapping, driver state, or running executable is replaced.

## Matched synthetic mapper benchmark

The comparison used the same Release benchmark executable and command shape against accepted `v2.6.6` baseline `bdedc52848ec18ddc4f6a387776526dcde1c7ce5` and this candidate. It is synthetic: no DirectInput device, vJoy driver, HID/HidHide state, or UI interaction participates.

| Scenario | Baseline p95 (us) | Candidate p95 (us) | Candidate hot-path allocations |
| --- | ---: | ---: | ---: |
| Linear | 0.8 | 0.8 | 0 |
| Adaptive, all 8 axes | 1.6 | 1.6 | 0 |
| Profile hold activation | 2.2 | 2.2 | 0 |
| Profile hold release | 2.9 | 2.8 | 0 |
| Automation, 64 rules | 1.8 | 1.3 | 0 |

The captured candidate sample did not exceed the baseline p95 for these selected paths. This is a local single-run comparison, so throughput and worst-case values are not treated as a release-performance guarantee. The later CTest benchmark independently reported zero hot-path allocations for every listed scenario.
