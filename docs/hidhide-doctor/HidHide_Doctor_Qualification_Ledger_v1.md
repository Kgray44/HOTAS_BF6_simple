# HidHide Doctor qualification ledger v1

Status terms: **Executed** is evidence from this Phase 5 candidate;
**Fixture** is deterministic simulated coverage only; **Not qualified** means
no claim is made. FieldQualified promotion requires evidence beyond this
ledger's fixture work.

## Platform and product matrix

| Area | Evidence | Status | Limitation |
|---|---|---|---|
| Windows x64 owner machine | Staged `HidHide Doctor.exe --headless --report` exited 0. Windows build 26200, x64; 238 checks, 3 findings, 2 diagnoses. | Executed, read-only | This is one machine, not Windows-wide qualification. |
| Windows 10 supported build | No clean Windows 10 VM was available. | Not qualified | Do not infer from host edition labels. |
| Windows 11 current/alternate | No VM or second machine was available. | Not qualified | Required before RC readiness. |
| ARM64 / wrong architecture | No ARM64 or wrong-architecture fixture was run. | Not qualified | x64 build only. |
| non-elevated diagnosis | Staged headless scan completed without UAC. | Executed | Does not qualify elevated mutation. |
| UAC denial / elevated helper | Not exercised in Phase 5. | Not qualified | Owner authorization remains required. |
| English / non-English locale | English host only. | Partially qualified | Locale matrix remains open. |
| 100–200% DPI / small window | Existing headless layout suite passed. | Fixture/headless | Native DPI and screen-reader review remain open. |
| Command Center reporting and layout | Native-QML fixture test exercised Safe-to-Share report composition, local bundle writing, a 10-second released-divider stability hold, shared Activity/Inspector dock, and density transition. | Executed, native-QML fixture | This is not an owner visual review on a normal desktop, nor a full accessibility qualification. |
| Staged HOTAS Doctor entry points | Owner manually confirmed that **Devices**, **Diagnostics/App Health**, and **Flight Deck → Diagnostics** each opened or focused the paired staged Doctor. The Devices launch was independently observed to start `HidHide Doctor.exe` from the same stage with a bounded `--integration-context`; the paired staged process count remained one. | Executed, native owner route check | Covers the three deliberate launch surfaces and single-instance behavior only. It is not broad visual, DPI, screen-reader, hardware, or repair qualification. |

## Environment and hardware matrix

| Scenario | Evidence | Status |
|---|---|---|
| No HidHide, healthy HidHide, partial install | Existing deterministic diagnostic fixture coverage. | Fixture |
| `GET_WHITELIST` 0x57 isolation | Existing deterministic diagnostic fixture coverage. | Fixture |
| zero/multiple controllers, vJoy present/absent, malformed HID | Existing deterministic provider tests and prior catalog coverage. | Fixture |
| current owner machine | Client 1.5.230.0, loaded package/driver 1.4.181.0, pending restart evidence, and degraded HID enumeration remained distinct in the fresh read-only report. | Executed, read-only |
| many HID devices / CPU contention / low resources | Not performed. | Not qualified |

## Repair qualification ledger

| Recipe | Risk | Current qualification | Phase 5 evidence | Decision |
|---|---|---|---|---|
| R1 exact configuration delta | R1 | LabQualified | Deterministic plan, sealed helper protocol, rollback/reconciliation tests. | Remain LabQualified; no real owner R1 execution recorded here. |
| R2 component repair | R2 | LabQualified | Guarded helper and fixture/revalidation coverage. | Remain LabQualified. |
| R3 approved package repair | R3 | LabQualified | Owner scan identifies the mismatch; no final authorization, UAC, installer, reboot, or post-reboot proof occurred. | Remain LabQualified and unavailable to normal users. |
| R4 approved upgrade | R4 | Unavailable | No newer approved package record. | No production R4. |
| R5 recovery | R5 | Unavailable | No independently verified package rollback asset. | No production R5. |

## Release blocker classification

BLOCKER evidence is still missing for cross-machine qualification, real repair
execution/reboot reconciliation, native owner visual/DPI/accessibility review
beyond the exercised entry points, CPU contention, installer/update/uninstall,
full HOTAS regression, and
external tester use. The owner-machine diagnosis itself is not a repair
authorization. No unresolved code safety defect was identified by the focused
tests, but missing evidence is not treated as a pass.
