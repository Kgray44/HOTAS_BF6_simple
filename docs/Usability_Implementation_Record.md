# Pass A usability implementation record

## Boundary and comparison baseline

- Governing material: `HOTAS_BF6_Usability_Implementation_Map_v1.0.md`, `HOTAS_BF6_Pass_A_Codex_Prompt.md`, and the owner correction addendum dated 2026-09-16.
- Candidate: `codex/usability-setup-guidance` in `C:\Users\kkids\Documents\HOTAS_BF6-usability-setup-guidance`.
- Comparison baseline: accepted `origin/main` at `bdedc52848ec18ddc4f6a387776526dcde1c7ce5` (`v2.6.6`). This candidate was reconciled with that baseline by merge commit `ffd87a4`.
- Reconciliation conflicts: `qml/FlightDeckOverview.qml`, `qml/Standard.qml`, and `tests/app_qml_startup_tests.cpp`. The resolutions retained accepted v2.6.6 theme/pointer behavior and this pass's non-activating editor selection. No unresolved ownership conflict remains.
- Scope: Pass A only. No Pass B/C work, main merge, release/tag, installer change, physical driver change, or machine-wide configuration change is authorized or performed.

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
