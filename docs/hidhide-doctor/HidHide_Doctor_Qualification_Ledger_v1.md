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
| current owner machine | Client 1.5.230.0, loaded package/driver 1.4.181.0, pending-restart, degraded-enumeration, and stale-configuration evidence remained distinct in the fresh read-only report. | Qualified | Read-only owner-machine observation; no repair was authorized. |
| Idle staged process/startup/scan baseline | Three exact-stage process startup-smokes took 473/464/476 ms. Three complete read-only process-launch-to-report samples took 305/359/260 ms, all with zero NUL bytes. The interactive Doctor working set was 286,765,056 bytes at one idle snapshot. | Qualified | Process-level baseline only; not user-visible latency, CPU contention, cold-boot, or memory-pressure qualification. |
| many HID devices / CPU contention / low resources | Not performed. | Not Qualified |
| bounded hostile input campaign | Seed `0x5AFEF00D`; 64 malformed inputs each for integration contexts/results, journals, helper frames, package metadata, and Safe-to-Share report/evidence composition. Domain suite passed in 24.41 seconds with no crash, hang, or accepted malformed corpus item. | FixtureQualified | Bounded deterministic parser coverage, not sustained fuzzing or a resource-pressure campaign. |

## Repair qualification ledger

| Recipe | Risk | Current qualification | Phase 5 evidence | Decision |
|---|---|---|---|---|
| R1 exact configuration delta | R1 | LabQualified | Deterministic plan, sealed helper protocol, rollback/reconciliation tests. | Remain LabQualified; no real owner R1 execution recorded here. |
| R2 component repair | R2 | LabQualified | Guarded helper and fixture/revalidation coverage. | Remain LabQualified. |
| R3 approved package repair | R3 | LabQualified | Owner scan identifies the mismatch; no final authorization, UAC, installer, reboot, or post-reboot proof occurred. | Remain LabQualified and unavailable to normal users. |
| R4 approved upgrade | R4 | Not Qualified | No newer approved package record. | No production R4. |
| R5 recovery | R5 | Not Qualified | No independently verified package rollback asset. | No production R5. |

## Release blocker classification

| Item or remaining gap | Classification | Current safe restriction |
|---|---|---|
| Owner-machine Safe-to-Share bundle inspection | Qualified, narrow | The exact owner artifact passed its stated structure and scoped privacy checks; this does not remove the other privacy, platform, or release gates. |
| Clean/healthy/partial HidHide and Windows 10/alternate-Windows-11 evidence | RELEASE BLOCKER | Fixture results remain FixtureQualified; no cross-machine claim. |
| Native DPI, accessibility, five-theme, and full practical HOTAS review | RELEASE BLOCKER | Do not substitute offscreen/QML fixtures for owner acceptance. |
| Performance, CPU contention, low-resource, and broad hardware matrix | RELEASE BLOCKER | No acceptable performance or contention budget is yet recorded. |
| Installer, update atomicity, and uninstall | RELEASE BLOCKER | No release decision until paired-component and uninstall safety are exercised sufficiently. |
| External tester run | RELEASE BLOCKER | May become Deferred Post-RC only through an explicit governing release decision. |
| R1–R3 live/UAC/reboot evidence | FIELD-REPAIR BLOCKER | Remain LabQualified and unavailable in normal production mode. |
| ARM64 repair, R4 package, R5 rollback, and app-binary signing | RELEASE-CANDIDATE LIMITATION | No repair promotion or invented signing; kernel/package signature validation remains required. |

The owner-machine diagnosis itself is not a repair authorization. No unresolved
code safety defect was identified by the focused tests, but missing evidence is
not treated as a pass.
