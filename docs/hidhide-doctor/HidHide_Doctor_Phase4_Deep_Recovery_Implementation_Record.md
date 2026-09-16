# HidHide Doctor — Phase 4 Deep Repair and Recovery Implementation Record

## Candidate provenance and scope

- Accepted Phase 3 base: `a8e384c831952ceac1f65613243bd79de7d0d900`
- Phase 4 branch: `codex/hidhide-doctor-phase4-deep-recovery`
- Isolated worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase4-deep-recovery`
- Final candidate commit: recorded in the delivery closeout after the commit is created; a commit cannot contain its own resulting SHA.
- Contract revision: deep-repair engine `4.0`; helper IPC protocol `2`; journal schema `3`; report schema `5`.

This candidate extends Phase 3's explicit pipeline—diagnosis, planning,
authorization, backup, journal, fresh precondition check, narrow helper,
independent read-back, verification, and recovery. It is deliberately a
LabQualified candidate. It is not a release, a production repair claim, a
driver-signing waiver, or authority to mutate the review machine.

## Risk classes, recipes, and gates

The recipe registry remains versioned and typed. R1 remains unchanged. The
new R2–R5 recipes are all `LabQualified`; normal mode surfaces their evidence
and remains read-only.

| Risk | Recipe | Entry condition | Bounded scope |
| --- | --- | --- | --- |
| R2 | `HD-R2-REPAIR-HIDHIDE-SERVICE` | Confirmed exact HidHide service-registration inconsistency | HidHide service registration only. |
| R2 | `HD-R2-REPAIR-HIDHIDE-FILTER` | Confirmed exact HidHide filter-registration inconsistency | HidHide filter registration only; preserve unrelated order. |
| R3 | `HD-R3-COMPLETE-DRIVER-REPLACEMENT` | Confirmed installed-versus-loaded replacement discontinuity | One pinned approved HidHide package, one bounded restart, read-back. |
| R3 | `HD-R3-REPAIR-INSTALLATION` | Confirmed partial install or missing control endpoint | One pinned approved HidHide package, one bounded restart, read-back. |
| R4 | `HD-R4-UPGRADE-APPROVED-PACKAGE` | Explicit owner upgrade request plus confirmed mismatch | Exact approved package only, with an allowed measured source version. |
| R5 | `HD-R5-RECOVER-APPROVED-PACKAGE` | Confirmed failed/damaged repair state | Separate recovery plan and authorization; exact inactive package only. |

The planner refuses contradictory evidence, an unknown/incompatible helper
architecture, an incomplete GET-only configuration backup, an unqualified
Windows build, an unapproved package, unpinned source/hash/signer/version, an
upgrade outside the catalog migration matrix, or reboot counts above the
recipe/package bound. Newer version evidence alone never causes an upgrade.

## Approved-package boundary

`ApprovedPackage` records package identity, provider, version, native
architecture, Windows-build range, source/provenance class, pinned SHA-256,
signature policy and signer identity, upgrade/downgrade matrix, maximum
reboots, qualification, and provenance text. `ApprovedPackageCatalog::validate`
requires each observed identity value to match exactly.

The type system reserves distinct source kinds for installed validated cache,
known official signed release, HOTAS-qualified signed provider, and
deterministic fixture test. It accepts no arbitrary URL, local path, command
line, latest-channel selector, or generic installer operation.

Only two deterministic, `fixture://` records are supplied in this candidate.
They exist to prove catalog/plan/helper/reboot behavior and are visibly marked
**Deterministic test fixture only**. No public HidHide MSI hash, signer, or
download provenance was represented as production-approved without a reviewed
source record. Consequently, a real-machine R3–R5 diagnosis with an ordinary
Nefarius installation is safely blocked at the catalog gate rather than
downloading or installing anything.

## Package, component, restart, and recovery model

The typed operation allow-list adds approved-package validation/staging,
exact HidHide service/filter repair, exact inactive-package removal, approved
package installation, a persisted Windows-restart boundary, and configuration
reconciliation. Package targets must be `HD-PKG-*` identifiers and deep plan
payloads reject path or argument fields. The helper protocol independently
validates the sealed payload after its own fresh observation; protocol v2 keeps
the normal Doctor/helper build and nonce binding.

Before a deep plan can advance, the transaction captures the full HidHide
configuration snapshot, Driver Store digest, package/loaded-version evidence,
plan digest, provider/build/architecture and expected restart count. Journal
schema 3 persists deep-recovery data and continuation state atomically under
the existing owner/System-only directory policy. A reboot continuation is
observe-first: it performs fresh package/driver/API/configuration evidence
collection, does not replay installation, does not silently restore data, and
enters `RecoveryRequired` on a missing package, old driver, failed API check,
configuration mismatch, or exhausted restart bound. Recovery is separately
planned and separately authorized.

R5 may name one exact inactive HidHide package only after both target and
rollback eligibility are established. It never enumerates or removes an
arbitrary Driver Store package. The model also retains deliberate handling for
service/filter drift, driver-store/cache drift, same-version reinstall,
client-only, driver-only, failed install, interrupted replacement, deferred
restart, repeated restart, legacy configuration, conflicting configuration,
wrong architecture, unknown build, and constrained staging space.

The Phase 3 native R1 mutator is intentionally still its R1 IOCTL allow-list.
This candidate does **not** claim a production package installer, service
writer, filter writer, Driver Store remover, or reboot issuer. An attempted
deep execution therefore stops safely rather than falling back to generic
process launch or arbitrary system mutation. Completing those system-specific
helpers requires separately reviewed actual source records and Lab execution
evidence; it is not substituted by fixture success.

## UI, CLI, reports, fixtures, and tests

The shared User Action/Command Center view model now renders R1–R5 risk,
package identity/provenance, pinned hash and signer, source type, build and
architecture qualification, typed operation list, configuration backup and
rollback/recovery guidance, restart maximum/continuation status, qualification,
authorization, and blocked reasons. The QML review card adds an Approved
Package section without turning a normal scan into an elevation path.

Reports use schema 5 and add a redaction-aware approved-package catalog and
the deep plan/continuation fields. `--headless --plan-repair --dry-run-repair`
is explicitly read-only: it can create a durable dry-run plan/journal but
cannot invoke UAC, the helper, package staging, installation, service/filter
repair, Driver Store mutation, or restart. `--approved-upgrade` is fixture
only and does not authorize a normal-mode upgrade.

The development fixture matrix is expanded beyond forty named cases. Focused
tests cover exact catalog identity rejection, separate R2/R3/R4/R5 plan
selection, typed helper plan validation and path-field tamper rejection,
observe-first bounded reboot reconciliation, schema-5 catalog reporting, and
fixture uniqueness. Existing domain, standalone startup, and QML layout tests
continue to exercise the non-mutating Doctor surface.

## Verification, machine boundary, and handoff

The following candidate-local checks are required before owner review:

1. `hidhide_doctor_domain_tests`
2. `hidhide_doctor_deep_repair_tests`
3. `hidhide_doctor_standalone_startup_smoke`
4. `hidhide_doctor_layout_tests`
5. Mapping core/readiness/startup and synthetic hot-path checks, recorded in the delivery closeout.

On the Phase 4 candidate build, all four Doctor checks passed: domain (8.12 s),
deep repair (0.45 s), standalone startup (0.38 s), and native QML layout
(4.90 s). The independent mapping core, controller-readiness, backend-startup,
and synthetic mapping hot-path tests also passed. The synthetic benchmark
reported zero tracked allocations in all reported linear, adaptive,
profile-control, and automation cases; that result explicitly excludes live
DirectInput polling and the vJoy driver call.

Real-machine validation is limited to a non-elevated, read-only diagnosis and
optional dry-run. It must record the actual incomplete-replacement/broken-
enumeration evidence, any permission-limited data, the blocked package reason,
and the absence of an authorized plan. No owner authorization button, UAC
prompt, installer, restart, Driver Store change, HidHide configuration SET, or
automatic repair is part of this candidate handoff. Leave the native plan open
for owner review and do not merge, tag, release, or start Phase 5.

The final Phase 4 real-machine headless dry-run exited `0` and wrote a
schema-5 report. It diagnosed `HD-DIAG-INCOMPLETE-REPLACEMENT` at Very High
confidence (97) and `HD-DIAG-DEVICE-ENUMERATION` at High confidence (86),
with three warnings and one pending-restart record. The safe proposal state was
`REPAIR BLOCKED`: the measured helper/native-architecture capability was not
compatible. The scan additionally recorded `RegOpenKeyExW: Access is denied.`
as an operational limit. Thus no package catalog, helper, installation,
service/filter, Driver Store, HidHide SET, or restart path was entered.
