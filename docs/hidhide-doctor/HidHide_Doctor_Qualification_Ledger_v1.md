# HidHide Doctor qualification ledger v1

Status terms are deliberately narrow. **Qualified** applies only to the
specific real-machine behavior described in its evidence cell;
**FixtureQualified** is deterministic simulated coverage only;
**LabQualified** never enables normal production repair; **Not Qualified**
means no claim is made. FieldQualified promotion requires evidence beyond this
ledger's fixture work.

## Platform and product matrix

| Area | Evidence | Status | Limitation |
|---|---|---|---|
| Windows x64 owner machine | Exact staged candidate `--headless --report` exited 0. Windows build 26200, x64; 238 checks, 4 findings, 3 diagnoses; no NUL bytes; client 1.5.230.0 and driver 1.4.181.0 remained distinct. | Qualified | One machine and read-only diagnosis only; it is not Windows-wide qualification. |
| Windows 10 supported build | No clean Windows 10 VM was available. | Not Qualified | Do not infer from host edition labels. |
| Windows 11 current/alternate | No VM or second machine was available. | Not Qualified | Required before RC readiness. |
| ARM64 / wrong architecture | Deterministic Windows 11 ARM64 and wrong-package-architecture fixtures passed through the current domain suite. | FixtureQualified | x64 is the only real-machine architecture exercised; no ARM64 field-repair claim. |
| non-elevated diagnosis | Exact staged headless scan completed without UAC or mutation. | Qualified | Does not qualify elevated mutation. |
| UAC denial / elevated helper | Not exercised in Phase 5. | Not Qualified | A field-repair qualification gap; owner authorization remains required. |
| English / non-English locale | English host only; deterministic `de-DE` fresh-user and non-English-Windows fixtures passed. | FixtureQualified | Locale matrix remains open. |
| 100–200% DPI / small window | Existing native-QML layout suite passed. | FixtureQualified | Native DPI and screen-reader review remain open. |
| Command Center reporting and layout | Native-QML fixture test exercised Safe-to-Share report composition, local bundle writing, a 10-second released-divider stability hold, shared Activity/Inspector dock, and density transition. | FixtureQualified | This is not an owner visual review on a normal desktop or a full accessibility qualification. |
| Flight Deck navigation | The focused native-QML fixture visited all 11 Flight Deck pages in Dark and Light. The recorded maxima were 108 ms (Dark) and 70 ms (Light). | FixtureQualified | This is a deterministic QML fixture result, not a native-pointer, five-theme, or owner visual acceptance result. |
| Local report export completion | The owner used the native file picker to create and reopen Local / Unredacted `test_report2.md` in Downloads; the typed-URL event-loop/domain contract also passed. | Qualified | Establishes the local destination/commit/reopen route only, not Safe-to-Share privacy. |
| Safe-to-Share owner bundle | On the exact `0846edf` staged candidate, the owner selected Downloads as the parent destination and Doctor created `HidHideDoctor-Diagnostic-Bundle-20260917-140145111Z`. It contained exactly `evidence.json`, `manifest.json`, `report.json`, `report.md`, and `timeline.json`; all JSON parsed, no artifact was empty or contained NUL, and the bundle manifest named the four report artifacts. The Safe-to-Share report manifest recorded 12 included, 714 redacted, and 2 excluded entries. A scoped content check found no current username, machine name, profile, temp, or app-data path; the report composer declares automatic network upload not performed. | Qualified | One owner-created artifact and a scoped local-identifier check only. This is not a universal data-classification/privacy audit, proof of external delivery behavior, or a release decision. |
| Production build hygiene | Staged binary fixture mode is reachable only through explicit `--development-fixture`; Lab repair further requires `--lab-repair-mode`. The package passed fixture-gate startup smoke; normal-mode QML states it is read-only and cannot execute LabQualified repairs. | Qualified | The switch remains compiled for controlled fixture qualification, not a normal-mode capability. |
| Updater component pairing and rollback | Stage validation requires mapper, Doctor, and helper versions to agree. `launcher_core_tests` exercises failed installer, interrupted replacement, missing/invalid package, and candidate-startup failures; each restores the complete prior fixture package and preserves an external configuration sentinel. | FixtureQualified | No installed-product update or recovery/retry run has occurred on a clean test machine. |
| Staged HOTAS Doctor entry points | Owner manually confirmed that **Devices**, **Diagnostics/App Health**, and **Flight Deck → Diagnostics** each opened or focused the paired staged Doctor. The Devices launch was independently observed to start `HidHide Doctor.exe` from the same stage with a bounded `--integration-context`; the paired staged process count remained one. | Qualified | Covers the three deliberate launch surfaces and single-instance behavior only. It is not broad visual, DPI, screen-reader, hardware, or repair qualification. |

## Environment and hardware matrix

| Scenario | Evidence | Status |
|---|---|---|
| No HidHide, healthy HidHide, partial install | Current deterministic diagnostic fixture coverage. | FixtureQualified |
| `GET_WHITELIST` 0x57 isolation | Current deterministic diagnostic fixture coverage. | FixtureQualified |
| zero/multiple controllers, vJoy present/absent, malformed HID | Current deterministic provider tests and catalog coverage. | FixtureQualified |
| current owner machine | Client 1.5.230.0, loaded package/driver 1.4.181.0, pending-restart, degraded-enumeration, and stale-configuration evidence remained distinct in the fresh read-only report. A non-identifying PnP inventory observed 26 working HID-class entries, two vJoy-named entries, a running vJoy service, and a running HidHide service. | Qualified, narrow | Read-only owner-machine observation and inventory only; no physical-input behavior or repair was authorized. |
| Exact-stage process/startup/scan baseline | On the `0846edf` stage, 15 `--startup-smoke` children had a 511.855 ms median (462.328–574.361 ms); 15 headless report serializations had a 297.279 ms median (265.116–403.066 ms). One 10-second idle observation of the protected interactive Doctor measured 0.01% CPU across 16 logical processors, 163,299,328 B working set, and 149,700,608 B private memory. | Qualified, process-level | The harness does not flush the Windows file cache, measure first visible progress, native input response, or screen-reader output. |
| controlled CPU contention | Five real read-only `--scan-smoke` samples per level all exited 0 without a timeout: idle median 1451.325 ms (1415.557–1518.654), moderate synthetic CPU median 1375.562 ms (1365.568–1477.844), and heavy synthetic CPU median 1758.906 ms (1665.841–2108.035). The heavy child-process peaks were 122,605,568 B working set and 132,354,048 B private memory. The protected interactive Doctor remained running. | Qualified, process-level | Bounded synthetic CPU load and process timing only; it is not owner-perceived interaction latency or an input-overload verdict. |
| low-resource and faulted-write behavior | The report/bundle writer has atomic per-file writes and an 8 MiB cap; direct domain and deep-repair fixture suites exited 0. No constrained-memory, full-volume, or intentionally failed user export was run on the owner machine. | FixtureQualified / Not Qualified | A destructive or capacity-changing resource experiment is intentionally absent; this remains an RC evidence gap. |
| bounded hostile input campaign | Fixed seed `0x5AFEF00D`; 64 malformed inputs each for integration contexts/results, journals, helper frames, package metadata, and Safe-to-Share report/evidence composition. The direct domain suite exited 0 in this closure run. | FixtureQualified | Bounded deterministic parser coverage, not sustained fuzzing, installed-package abuse, or a resource-pressure campaign. |
| final local CTest rerun | A fresh sequential rerun passed targets 1–14, then `app_backend_startup_tests` remained active for 339 seconds with no configured CTest timeout or error output. Only the agent-launched CTest/test child tree was stopped; the live mapper and Doctor were preserved. | NO NEW LOCAL CTEST VERDICT | The earlier sequential 17/17 result on the unchanged product head is historical evidence only. This rerun does not create a new full-suite pass or a product-failure verdict. |

## Repair qualification ledger

| Recipe | Risk | Current qualification | Phase 5 evidence | Decision |
|---|---|---|---|---|
| R1 exact configuration delta | R1 | LabQualified | Deterministic plan, sealed helper protocol, rollback/reconciliation tests. | Remain LabQualified; no real owner R1 execution recorded here. |
| R2 component repair | R2 | LabQualified | Guarded helper and fixture/revalidation coverage. | Remain LabQualified. |
| R3 approved package repair | R3 | LabQualified | Owner scan identifies the mismatch; no final authorization, UAC, installer, reboot, or post-reboot proof occurred. | Remain LabQualified and unavailable to normal users. |
| R4 approved upgrade | R4 | Not Qualified | No newer approved package record. | No production R4. |
| R5 recovery | R5 | Not Qualified | No independently verified package rollback asset. | No production R5. |

## Release blocker classification

### Phase 5 closure execution classes

This table separates work that can be closed on the present owner machine from
work that needs another environment.  A fixture result never moves an item out
of its stated class, and a repair-field gap never blocks normal read-only
diagnosis by itself.

| Gate | Closure class | Current state | Evidence needed to close or hand off |
|---|---|---|---|
| User-visible Doctor performance, controlled CPU contention, low-resource/write failures, and memory/idle CPU | A. LOCAL RC BLOCKER | Partially Qualified | Exact-stage process measurements cover idle/moderate/heavy CPU contention and idle memory/CPU. First visible progress, owner interaction latency, and low-resource/write-failure evidence remain required. |
| Real Windows scaling, keyboard navigation, accessibility names/roles, and in-app control-style review | A. LOCAL RC BLOCKER | Not Qualified | Native owner-machine pass at available scaling values; screen-reader remains explicitly unqualified unless actually exercised. |
| Five-theme HOTAS entry points, Flight Deck non-regression, practical HOTAS pass, and owner-available HID/vJoy cases | A. LOCAL RC BLOCKER | Not Qualified | Deliberate native owner-machine evidence, with real and fixture hardware kept separate. |
| Updater pairing, staged install/update/uninstall safety, hostile-path handling, bounded fuzzing, helper-security review, production hygiene, immutable stage, and final owner check | A. LOCAL RC BLOCKER | Partially FixtureQualified | Bounded fuzz and helper fixtures passed; isolated install/update/uninstall and hostile-path evidence, immutable-final-stage validation, and final owner checks remain required. No mutable repair is authorized. |
| Windows 10, alternate Windows 11, non-English Windows, and ARM64 | B. EXTERNAL RC BLOCKER | External Qualification Required | A separate real or provisioned environment must run the defined launch/scan/export/basic-UI checklist. |
| Trusted external tester campaign | B. EXTERNAL RC BLOCKER | External Qualification Required | A self-contained staged kit and a returned tester result; it may become post-RC only through an explicit governing release decision. |
| R1–R3 live UAC/reboot/rollback evidence | C. REPAIR FIELD-QUALIFICATION BLOCKER | LabQualified | Explicit owner authorization plus real recipe evidence; this does not authorize normal-mode repair. |
| R4 approved-package repair and R5 recovery | D. ACCEPTABLE RC LIMITATION | Not Qualified | No approved newer package or independently verified rollback package exists; normal Doctor remains read-only. |
| Unsigned application binaries | D. ACCEPTABLE RC LIMITATION | NotSigned | No signing capability has been evidenced or invented; any release-policy decision must treat this explicitly. |
| Additional broad external tester coverage after an explicit RC policy decision | E. POST-RC QUALIFICATION | Deferred Post-RC only if governed | Do not silently reclassify the current external matrix as post-RC. |

| Item or remaining gap | Classification | Current safe restriction |
|---|---|---|
| Owner-machine Safe-to-Share bundle inspection | Qualified, narrow | The exact owner artifact passed its stated structure and scoped privacy checks; this does not remove the other privacy, platform, or release gates. |
| Clean/healthy/partial HidHide and Windows 10/alternate-Windows-11 evidence | RELEASE BLOCKER | Fixture results remain FixtureQualified; no cross-machine claim. |
| Native DPI, accessibility, five-theme, and full practical HOTAS review | RELEASE BLOCKER | Do not substitute offscreen/QML fixtures for owner acceptance. |
| User-visible performance, low-resource/write behavior, and broad hardware matrix | RELEASE BLOCKER | Process-level CPU/contention data exists, but no first-visible/native-interaction budget, low-resource test, or broad hardware proof is recorded. |
| Installer, update atomicity, and uninstall | RELEASE BLOCKER | No release decision until paired-component and uninstall safety are exercised sufficiently. |
| External tester run | RELEASE BLOCKER | May become Deferred Post-RC only through an explicit governing release decision. |
| R1–R3 live/UAC/reboot evidence | FIELD-REPAIR BLOCKER | Remain LabQualified and unavailable in normal production mode. |
| ARM64 repair, R4 package, R5 rollback, and app-binary signing | RELEASE-CANDIDATE LIMITATION | No repair promotion or invented signing; kernel/package signature validation remains required. |

The owner-machine diagnosis itself is not a repair authorization. No unresolved
code safety defect was identified by the focused tests, but missing evidence is
not treated as a pass.
