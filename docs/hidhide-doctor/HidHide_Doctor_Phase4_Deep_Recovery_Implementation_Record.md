# HidHide Doctor — Phase 4 Deep Repair and Recovery Implementation Record

## Final status

**PHASE 4 COMPLETE: YES**

Phase 4 is complete as a **LabQualified implementation and documentation candidate**: the bounded R2–R5 design, execution boundaries, package validation, recovery contract, focused tests, CI, and read-only machine proof are recorded here. This is not a claim that a machine is fixed or that invasive repair is FieldQualified.

No Phase 5 work was started. No real repair, UAC elevation, helper invocation, package installation, service/filter write, Driver Store change, HidHide SET, reboot, merge, tag, or release was performed for this closeout.

## Candidate provenance

| Item | Recorded value |
| --- | --- |
| Phase 3 base | a8e384c831952ceac1f65613243bd79de7d0d900 |
| Branch | codex/hidhide-doctor-phase4-deep-recovery |
| Final implementation candidate | b2ff8d418c06f927cf0cbabf727d12738e085010 |
| Pull request | [#65](https://github.com/Kgray44/HOTAS_BF6_simple/pull/65) |
| Contract revisions | deep-repair engine 4.0; helper IPC protocol 2; journal schema 3; report schema 5 |

The candidate is a reviewable implementation. This record does not authorize merge, tag, release, ordinary-user activation, or a live repair.

## Exact approved official package

The catalog contains one real, immutable R3 package. It is not a floating latest selector, a caller-provided URL/path, or a fixture.

| Field | Exact approved value |
| --- | --- |
| Package ID | HD-PKG-NEFARIUS-HIDHIDE-1.5.230.0-X64 |
| Provider / channel | Nefarius / stable official signed release |
| Artifact | HidHide_1.5.230_x64.exe |
| Source | [Nefarius HidHide v1.5.230.0 release](https://github.com/nefarius/HidHide/releases/tag/v1.5.230.0), catalogued asset: https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe |
| SHA-256 | f4bbbcB82e6258641b887c74bc81c4c5f66e4aa811808dfc304347687b7605f6 |
| Expected size | 8,078,016 bytes |
| Authenticode signer | Nefarius Software Solutions e.U. |
| Artifact version / architecture | 1.5.230 / x64 |
| Qualified Windows builds | 19041 through 26200, inclusive |
| Maximum restarts / qualification | 1 / LabQualified |
| Upgrade sources / rollback asset | none / none |

The final source review independently obtained the exact asset without running it and confirmed the stated byte count, SHA-256, valid Authenticode result, signer, and file/product version 1.5.230. That proves the pinned artifact identity, not a driver installation or a field repair.

ApprovedPackageRuntime accepts only the catalogued GitHub HTTPS release and approved GitHub asset redirect hosts. It stores the artifact and exact metadata in the application local-data approved-packages cache with owner-and-LocalSystem-only protected ACLs. Before use, and again immediately before installation, it requires canonical filename/path, matching metadata and ACLs, exact bounded file size, SHA-256, WinVerifyTrust Authenticode validation, signer, PE architecture, and file version. A stale cache, failed signature, redirect escape, identity drift, or failed revalidation stops before the installer.

## Architecture qualification correction

The previous record was wrong to retain REPAIR BLOCKED for architecture. The provider had measured native architecture and the Doctor process but had not measured the paired helper binary into the capability decision. It could therefore report an incompatible helper despite compatible paired binaries.

The final candidate measures native system, Doctor binary/process, paired helper binary, and WOW64 state before accepting direct helper pairing. Focused regression coverage asserts the x64 native/process/Doctor/helper evidence and rejects drift in sealed helper requests.

| Final read-only evidence | Value |
| --- | --- |
| Native / Doctor / helper architecture | x64 / x64 / x64 |
| WOW64 | false |
| Helper architecture compatible | true |
| Direct helper protocol | true |
| Highest qualified repair tier | Recovery Supported |

The corrected proposal is **REPAIR IDENTIFIED — NOT FIELD QUALIFIED**. Architecture compatibility removes that planning defect only; it does not bypass Lab qualification, owner authorization, fresh evidence, or field gates.

## Implemented R2–R5 boundary

Normal Doctor use remains read-only. A deep plan is sealed, separately authorized, journalled, and freshly revalidated. The executor accepts typed R2–R5 operations only: there is no generic command/script runner, arbitrary URL/path, arbitrary service or registry target, or generic Driver Store removal.

### R2 — exact component repair

R2 repairs only the exact HidHide kernel-driver service contract: demand start, normal error control, and %SystemRoot%\System32\drivers\HidHide.sys. It creates that service only when that exact driver file exists and otherwise fails safely; unrelated service settings are preserved.

Before writing any filter value, R2 preflights all three classes. It appends only a missing HidHide UpperFilters entry and preserves all existing entries and ordering:

- HID: {745A17A0-74D3-11D0-B6FE-00C04FB3EFC0}
- XNA composite: {D61CA365-5AF4-4486-998B-9DB4734C6CA3}
- Xbox composite: {05F5CFE2-4733-4950-A6BB-07AAD01A3A84}

This narrow scope is not permission to normalize unrelated filter chains.

### R3 — exact package replacement

R3 implements the bounded approved-package route: validate; acquire/stage from the pinned source; independently revalidate; invoke only the exact catalog-derived installer executable with no caller arguments; persist a continuation boundary; then observe afresh after reboot before reconciliation. The installer has a bounded 30-minute wait and accepts only success or reboot-required codes. The current catalog permits one reboot.

The final real-machine dry run produced an R3 plan with exactly five typed operations: validate approved package, stage approved package, install approved HidHide package, request the restart boundary, and reconcile HidHide configuration. No operation ran.

### R4 and R5 — safe availability

R4 plan architecture and sealed-plan validation are implemented and fixture-covered, but no newer qualified package or migration source is catalogued. A real R4 upgrade is therefore unavailable; version drift cannot silently become a latest upgrade.

R5 requires a precisely identified inactive HidHide package and a separately verified rollback asset. Deterministic fixtures cover the typed recovery path and the arbitrary-Driver-Store-removal prohibition. Production has no verified rollback package, so R5 safely returns ROLLBACK_PACKAGE_UNAVAILABLE; no real inactive package was removed.

## Helper scope and reboot continuation

The helper protocol independently validates sealed plan/package identity, allowed operation/target identity, nonce/build binding, expiry, and fresh preconditions. Tests demonstrate rejection of catalog or operation drift, arbitrary executable paths and arguments, unrelated service targets, and expired requests. Elevated mutation is confined to the R2 service/filter and R3 approved-package scopes above.

Journal schema 3 persists transaction, configuration snapshot, package/loaded-version evidence, Driver Store digest, plan digest, architecture/build evidence, restart count, and continuation state under the existing owner/System-only policy. Continuation is one current-user RunOnce entry for the paired Doctor executable and transaction ID. Scheduling does not reboot: restart-now is an isolated explicit owner action, and restart-later is supported.

After reboot, Doctor shows RESUMING REPAIR, runs a new read-only scan, and never replays installation. It removes the continuation only at a terminal state. Reconciliation is observe-first and reports preserved, migration-preserved, restoration-required, incompatible-legacy-entry, external-conflict, or unreadable; it does not overwrite external changes. Missing package, old driver, failed API/configuration evidence, or exhausted restart bound enters RecoveryRequired for owner review.

## Final real-machine read-only dry run

The final non-elevated invocation was:

~~~text
HidHide Doctor.exe --headless --plan-repair --dry-run-repair --report C:\hotas-builds\hidhide-doctor-phase4-evidence\phase4-real-machine-readonly-after-architecture-fix.json
~~~

It exited 0, wrote a schema-5 report, and created only a durable dry-run plan/journal.

| Observed evidence | Final value |
| --- | --- |
| Client version | 1.5.230.0 |
| Driver / Driver Store version | 1.4.181.0 / 1.4.181.0 |
| Diagnoses | HD-DIAG-INCOMPLETE-REPLACEMENT — Very High 97; HD-DIAG-DEVICE-ENUMERATION — High 86 |
| Pending restart evidence | present (1 record) |
| Proposal | REPAIR IDENTIFIED — NOT FIELD QUALIFIED |
| Candidate package / qualification | HD-PKG-NEFARIUS-HIDHIDE-1.5.230.0-X64 / review-only LabQualified |
| Restart / rollback | one restart allowed / no rollback asset |

The run also recorded the permission-limited RegOpenKeyExW: Access is denied. observation. It did **not** request UAC; launch the helper; download, stage, or install a package; change a service, filter, or Driver Store entry; send a HidHide SET; restart; alter controller visibility; or operate a physical HOTAS. It is planning and diagnostic evidence only.

## Verification evidence

Focused local candidate verification completed before this documentation closeout:

- hidhide_doctor_deep_repair_tests: 11 passed.
- hidhide_doctor_domain_tests: 29 passed.
- hidhide_doctor_layout_tests: 9 passed. Existing non-fatal QML mock and geometry warnings do not constitute native pointer or typography qualification.
- Isolated Release targets HidHideDoctor and HidHideDoctorRepair built.

For implementation candidate b2ff8d418c06f927cf0cbabf727d12738e085010, GitHub reported both terminal pull-request workflows successful:

| Workflow | Run | Result |
| --- | --- | --- |
| [HOTAS BF6 CI](https://github.com/Kgray44/HOTAS_BF6_simple/actions/runs/35110126399) | 35110126399 / validate | completed / success |
| [Documentation Check](https://github.com/Kgray44/HOTAS_BF6_simple/actions/runs/35110126418) | 35110126418 / verify | completed / success |

These results cover the implementation candidate. They do not replace an owner-authorized elevated repair test, physical-device qualification, or a FieldQualified release gate.

## Qualification decision and Phase 5 limits

| Question | Decision | Reason |
| --- | --- | --- |
| Is Phase 4 implementation complete? | **Yes** | R2/R3 execution boundaries, secure package acquisition, typed helper validation, durable continuation, R4/R5 safe availability rules, reports, fixtures, focused tests, CI, and a final read-only dry run are recorded. |
| Is this a FieldQualified repair? | **No** | The package and recipes are LabQualified; no live repair was authorized or performed. |
| Is R4 ready for a real upgrade? | **No** | No separately qualified newer package/migration source exists. |
| Is R5 ready for production rollback? | **No** | No independently verified rollback asset exists; production removal is blocked. |
| Has native/physical controller behavior been qualified? | **No** | No physical HOTAS, DirectInput/vJoy runtime, or owner acceptance was exercised. |

If separately authorized, Phase 5 must qualify a real-machine matrix: disposable/clean-machine R2/R3 execution and failures across the stated Windows range; explicit restart/reconciliation outcomes; a reviewed newer package and migration matrix before R4; a verified rollback asset and recovery exercise before production R5; native UI/pointer review; physical HidHide/HOTAS behavior; and owner acceptance. Those are intentional limits, not permission to expand this candidate or perform repair now.

## Handoff

Keep the deep plan available only for Lab/owner review. Do not merge, tag, release, start Phase 5, or execute a real repair from this closeout.
