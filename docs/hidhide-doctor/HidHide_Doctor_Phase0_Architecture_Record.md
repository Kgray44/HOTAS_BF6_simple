# HidHide Doctor Phase 0 — Architecture and Evidence Record

**Status:** Phase 0 candidate: diagnostic architecture and fixture-only standalone shell. This is not a repair tool, a HidHide installation, or an authorization for Phase 1.

## Baseline, documents, and upstream reference

| Item | Recorded value |
| --- | --- |
| Upstream baseline | `origin/main` `fda74156b5c3f48d1d8c1038c77b7f9f188bf930` |
| Branch | `codex/hidhide-doctor-phase0-architecture` |
| Isolated worktree | `C:\\Users\\kkids\\Documents\\HOTAS_BF6-hidhide-doctor-phase0-architecture` |
| Candidate | separate `HidHide Doctor.exe` Qt target |
| Provider | deterministic fixture data only |
| Real HidHide/HOTAS access | none |
| Repair execution/elevation | not implemented |

The four v1.1 governing documents were imported unchanged. `SHA256SUMS.txt` records their SHA-256 values; no source checksum manifest was supplied with the documents.

The inspected `nefarius/HidHide` master was `2b950fd9393e1644b4199f6eb4999e1720f0c6e9`. The examined public stable release was `v1.5.230.0` (2024-05-11), tagged commit `722d997ce75db58f5aa36e40ca920f99022c020a`. Future adapter design references `DEVELOPER.md`, `INSTALL_LAYOUT.md`, `HidHide/src/Logic.h`, and `Shared/HidHideIoctlContract.h` at the inspected master revision. No upstream source was forked, built, installed, or modified.

Master contains an ARM64 distribution-definition file, while the examined public release describes x64 Intel/AMD support. This is evidence rather than a support promise; unqualified platforms remain diagnosis-only.

## Existing integration audit

The mapper's `controller_readiness` discovers HidHide with install-root `HidHideCLI.exe` candidates and SCM service presence, then parses CLI output for cloak state, mapper allow-listing, and device visibility. Its existing HOTAS-specific repair path can mutate CLI configuration (`--app-reg`, `--app-unreg`, `--dev-hide`, `--dev-unhide`, `--cloak-on`, `--cloak-off`) with staged readback/rollback. It is unchanged.

`setup_repair_helper` relaunches the legacy mapper transaction and reads a JSON shape containing a program and arguments. That broad command form is explicitly not reused for a Doctor helper. Existing mapper UI and installer integration stay separate from the Doctor entry point.

## Process, privilege, and ownership

```text
Normal non-elevated Doctor
  fixture or future read-only providers
    -> canonical DoctorSession -> Focus View / Command Center
    -> evidence, finding, diagnosis, proposed typed RepairPlan

Future narrow elevated helper (not built in Phase 0)
  sealed allow-listed typed RepairOperation only
    -> revalidate -> journal -> verify -> rollback on failure
```

Diagnosis is normally unelevated. Future elevation requires an approved plan, explicit user authorization, helper-side revalidation, platform/architecture match, and integrity validation. The Doctor contract contains no arbitrary executable, arguments, shell string, or open-ended command payload.

`src/hidhide_doctor/` owns diagnosis vocabulary, environment/provider seams, catalogue, session/progress, repair contracts, and presentation adaptation. It does not own DirectInput, Raw Input, MappingWorker, vJoy output, profiles, or legacy setup repair. No mapping code was changed.

## Canonical session, plan, and presentation

Strong IDs identify sessions, checks, steps, evidence, findings, diagnoses, knowledge signatures, operations, plans, and transactions. Core records preserve native error domain/code/symbol, provenance, sensitivity, timestamp, source, evidence linkage, confidence, severity, repairability, user action state, and operation state.

`DoctorSession` is the sole mutable diagnosis state. It owns a frozen `DiagnosticPlan`, evidence, findings, diagnoses, user action, and current operation. Its transition guard models preparation, diagnosis, analysis, decision, repair readiness, elevation/reboot/resume, verification, completion, degraded completion, cancellation, and safe failure. Focus View and Command Center share one `DoctorSessionViewModel`; toggling layout cannot mutate the session.

Plan membership freezes before execution. A single explicit bounded extension is available for extended investigation. Work is weighted, not count-based: partial current work contributes fractional weight, Not Applicable leaves the denominator, and cancellation cannot appear complete.

## Evidence, catalog, diagnosis, and export

Evidence differentiates direct, derived, fixture, and imported-launch provenance. Native numeric errors are retained separately from display text, so `ERROR_INVALID_PARAMETER` (`0x57`) cannot become a guessed diagnosis. A check failure differs from execution failure: permission limitation, block, timeout, cancellation, unknown, or inconclusive result.

`DoctorCatalog` parses the imported v1.1 catalogue resource, permits only valid `HD-*` IDs, rejects duplicates and unknown prerequisites, and reports defined, registered, implementation, qualification, repair-link, unsupported, and unregistered coverage. The full-manifest test asserts at least 238 checks: 208 baseline plus 30 portability checks. Definition, implementation, and qualification are separate states.

Diagnosis carries a knowledge signature, confidence, severity, repairability, and explicit supporting/contradicting evidence IDs. Future rules must supply direct or declared derived evidence or remain Unknown/Inconclusive.

The Phase 0 report/export schema is: session identity + immutable build provenance + platform fingerprint + plan/check results + evidence/finding/diagnosis IDs + native errors + user action + repair-journal references. Evidence sensitivity is classified before export. Device IDs, paths, user names, and raw registry/configuration values are not safe by default.

## Repair, helper, and journal contracts

The contract enumerates repair kinds and targets: HidHide active/inverse state, whitelist/blacklist entries, exact service/filter repair, approved package, and snapshot restoration. `RepairPlan` holds risk, qualification, authorization, diagnoses, preconditions, operations, integrity seal, verification, and rollback definition. `RepairHelperContract::validate` rejects unknown or undefined operations/targets, unqualified plans, missing authorization, architecture mismatch, and a tampered seal. It has no program/arguments field.

`RepairTransactionJournalEntry` reserves transaction/plan/operation IDs, prior/requested/post values, state, and native error. Future helpers must record before/after/verify/rollback evidence and have recovery/resumption. Phase 0 performs no repair and writes no journal.

## Portability fixtures and qualification

The provider seam is fixture-only. Fixtures cover Windows 11 x64 on two builds, Windows 10 x64, Windows 11 ARM64, unknown future build, HidHide absent, client/driver mismatch, wrong package architecture, fresh non-English user without HOTAS BF6, clean HidHide install, zero controllers, and multiple arbitrary-vendor controllers with a malformed observation. Fixture protocol responses independently cover a failed call, a healthy unrelated call, and an absent capability.

Qualification binds native architecture and a build range to an evidence reference. Unknown OS/build/architecture keeps diagnosis support but grants no repair capability. A future live adapter must map real platform, component, protocol, privilege, timeout, and cancellation observations into the same records.

## Candidate, tests, and evidence limits

`HidHideDoctor` has its own QML module and deploy step. Normal launch says it renders fixture-only data; `--startup-smoke` loads it then exits. It has no mapper setup/repair call. The Release candidate remains running for owner review.

Domain tests cover IDs/transitions, status distinction, native-error preservation, weighted progress, cancellation/freeze/bounded extension, catalogue duplicate/prerequisite/manifest coverage, fixture families, protocol fault isolation, typed-helper rejection, and one-session presentation. CTest prepends the Qt runtime directory so `Qt6Test.dll` resolves outside a developer shell. Final setup/hot-path regression results are appended after the build.

No real HidHide driver/service/device/configuration, controller, mapper state, or Windows configuration was read or changed by the Doctor. No UAC, real repair, installation, reboot/resume, servicing, merge, tag, or release was performed. This environment proved process launch and QML startup smoke but could not offer a targetable native desktop surface for visual or pointer acceptance; owner native screening remains required.

## Final regression evidence

The Release build used Qt 6.8.3 with MSVC 2022 and `BUILD_TESTING=ON` in `C:\\hotas-builds\\hidhide-doctor-phase0`. The selected CTest command was:

```powershell
ctest --test-dir C:\hotas-builds\hidhide-doctor-phase0 -R '^(hidhide_doctor_domain_tests|hidhide_doctor_standalone_startup_smoke|controller_readiness_tests|app_backend_startup_tests|mapping_core_tests|mapping_hot_path_benchmark)$' --output-on-failure -C Release
```

All six selected tests passed: Doctor domain (`0.07 s`), Doctor standalone startup smoke (`0.18 s`), mapping core (`42.72 s`), controller readiness (`0.76 s`), app-backend startup (`31.26 s`), and mapping hot-path benchmark (`13.26 s`). The backend steady-state check reported `ui_stalls_over_250ms=0` and passed. The benchmark is synthetic and deliberately excludes DirectInput/vJoy driver calls; all reported scenarios retained `hot_path_allocations=0`.

The first Doctor test launch exposed a missing `Qt6Test.dll` in the CTest environment, matching the captured Windows dialog. The target source was healthy: adding `PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>` to the CTest environment made the same domain suite pass. This changes test runtime discovery only, not Doctor behavior or deployment policy.

Isolation was also checked by review of the changed-file set: the addition is limited to the standalone Doctor target, its QML shell, documentation, and Doctor domain tests; DirectInput-to-MappingWorker-to-vJoy routing sources are not changed.

## Phase 1 entry gate

Phase 1 needs explicit new authorization and a reviewed plan naming the target provider/API capability, qualification evidence, redaction policy, repair allow-list, helper authentication/integrity boundary, user-authorization UX, rollback/recovery, bounded fixtures, and physical-device validation. It must preserve every v1.1 invariant and mapper HOTPATH isolation.
