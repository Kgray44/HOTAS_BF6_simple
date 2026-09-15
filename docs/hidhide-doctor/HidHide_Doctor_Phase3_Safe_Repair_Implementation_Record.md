# HidHide Doctor — Phase 3 Safe Repair Implementation Record

## Provenance and candidate scope

- Accepted Phase 2 base: `920a58196bb2de952beef6cfc0de7ba2bfad3532`
- Phase 3 branch: `codex/hidhide-doctor-phase3-safe-repair`
- Isolated worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase3-safe-repair`
- Final candidate commit: recorded by the Phase 3 closeout after the commit is created (a Git commit cannot contain its own resulting SHA without changing that SHA).
- Engine contract revision: `R1 / helper IPC protocol 1 / journal schema 2 / report schema 4`

Phase 3 is limited to R1 configuration work.  No component, service,
filter-registration, Driver Store, package, upgrade, reboot-continuation, or
recovery operation is implemented or accepted by the helper.

## R1 registry, evidence, and ownership

The first-class, versioned `RepairRecipeRegistry` contains these LabQualified
`1.0` recipes:

| Recipe | Diagnosis mapping | Exact scope |
| --- | --- | --- |
| `HD-R1-ADD-HOTAS-WHITELIST` | `HD-DIAG-MISSING-HOTAS-EXEMPTION` | Add one independently identified HOTAS executable exemption. |
| `HD-R1-REPLACE-STALE-HOTAS-WHITELIST` | `HD-DIAG-STALE-CONFIG` | Remove one proven stale HOTAS exemption and add the verified replacement. |
| `HD-R1-UNHIDE-HOTAS-VIRTUAL-OUTPUT` | `HD-DIAG-VIRTUAL-HIDDEN` | Remove one verified HOTAS virtual output from the hidden-device list. |

All are LabQualified, never FieldQualified. Normal Doctor behavior creates a
read-only plan and explains that it is not field qualified. A plan requires a
High-or-stronger, non-contradictory diagnosis; complete GET evidence for
whitelist, blacklist, active, and inverse state; the direct protocol and
helper-architecture capability; and HOTAS-supplied, independently observed
intent. Standalone Doctor never invents HOTAS intent.

The ownership classifier distinguishes HOTAS-owned, Doctor-owned,
user/unrelated, and unknown entries. Delta planning compares semantic Windows
path/device identities but retains each unrelated entry verbatim and in its
existing order. The delta engine emits exact additions, removals, results, and
unchanged collateral; it does not normalize-write collateral data.

## Plan, authorization, and preview

`RepairPlan` contains stable plan/session/recipe IDs, recipe version, risk and
qualification, addressed diagnoses, typed operations, material
preconditions, stable pre-state and post-state JSON, precondition/post-state
fingerprints, collateral, elevation/restart requirements, and a SHA-256 plan
digest. Altering an operation or its state binding invalidates the digest.

The Focus and Command Center User Action surfaces render the same native
Doctor-styled review card: problem, R1 risk, Lab qualification, exact
operations, before/after-bound collateral, backup/rollback, elevation,
restart, estimated time, and user action. Normal mode is visibly read-only;
it neither invokes UAC nor can it obtain a mutable provider. An explicit
`--development-fixture ... --lab-repair-mode` path alone labels the card
`LAB REPAIR MODE — DEVELOPMENT FIXTURE`, displays live transaction status,
and offers separate final actions for a no-SET helper connectivity test and
for owner/lab authorization of that exact plan. The normal executable never
sets this mode.

`--headless` remains non-mutating. Diagnosis/report generation includes an R1
plan only when evidence and HOTAS context support it. The in-memory execution
fixture exercises plan, dry-run journal, stale-plan rejection, apply, and
read-back without a Windows provider.

## Backup, journal, lock, and recovery posture

Before any execution, the coordinator performs a fresh configuration read and
requires its fingerprint to match the plan. It then captures whitelist,
blacklist, cloak, inverse, provider, timestamp, R1 target scope, HOTAS repair
intent, direct-protocol/helper capability evidence, Windows build,
architecture, privacy classification, and SHA-256 backup manifest. A journal
write failure prevents execution.

`RepairJournalStore` uses the dynamically resolved per-user
`AppLocalDataLocation/repair-transactions` directory, an owner/System-only
protected DACL (object/container inheritance), QSaveFile atomic replacement,
a versioned schema, and a checksum verified on read. Journals are sensitive
local diagnostic data and are not emitted to console or uploaded. Failure to
create or harden the directory fails the transaction before mutation.

The state model persists `Planned`, `AwaitingAuthorization`, `Authorized`,
`CapturingBackup`, `Revalidating`, `AwaitingElevation`, `Executing`,
`Verifying`, `RollingBack`, `Completed`, `FailedSafely`, `RecoveryRequired`,
`Cancelled`, and `StalePlan` boundaries. Authorization uses the same
transaction ID delivered to the helper and binds the recipe/version, typed
targets, qualification, fingerprints, and digest. A cross-process `QLockFile`
prevents two repair transactions from running simultaneously. A stale
fingerprint stops before mutation. Corrupt journals fail closed. Restart
reconciliation is intentionally read-back-only: it classifies the current
configuration after the normal Doctor's complete GET scan, records the
result, and never replays, completes, or rolls back an interrupted
transaction. A `RecoveryRequired` result appears in the User Action rail as
`REPAIR RECOVERY REVIEW REQUIRED`, carrying its transaction ID and the
read-only reconciliation result.

## Elevated helper and mutation boundary

`HidHideDoctorRepair.exe` is a short-lived, no-UI helper. The normal Doctor
target does not link its mutator. The helper admits a single native named-pipe
request guarded by owner/System pipe ACL entries, same interactive-session
checking, a 64 KiB frame limit, one-time uppercase nonce, short expiry,
protocol version, Doctor/helper build identity, transaction/plan IDs, and
request/plan digests. It rejects malformed, oversized, stale, mismatched, or
out-of-scope requests before opening the mutation path.

The helper accepts no shell command, command line, registry path, file path
operation, service operation, or package operation. The Doctor launches only
the paired helper with `runas`, a per-launch pipe name and nonce; it serializes
exactly one sealed request and treats UAC `ERROR_CANCELLED` as `Authorization
cancelled — no changes made`. The helper validates the plan’s
exact bound before/after delta and permits only:

- `SetHidHideActive`
- `SetHidHideInverse`
- `AddWhitelistEntry` / `RemoveWhitelistEntry`
- `AddBlacklistEntry` / `RemoveBlacklistEntry`

The safe connectivity request carries the same sealed authorization and
requires helper-side independent GET revalidation, but returns before any SET
operation. The separate `HidHideConfigurationMutator` uses the documented HidHide WDM
control device and only persistent configuration IOCTL functions 2049, 2051,
2053, and 2055 after the helper’s independent GET revalidation. Lists are
rebuilt as bounded `MULTI_SZ` buffers from an exact in-memory delta. A SET
result is not completion: the coordinator reads configuration back and checks
the authorized post-state fingerprint, while a second fresh read-only provider
observation verifies provider health/capability, exact configuration and
collateral, and that the plan's affected diagnosis no longer remains before
`Completed`.

If an operation fails, dependent work stops and the exact native error is
journaled. If post-state differs, the transaction becomes
`RecoveryRequired`; it does not overwrite a possible external change. If the
configuration read-back is exact but an independent affected-check verifier
does not pass, rollback first re-reads and requires the exact authorized
post-state. Only then does it restore the captured exact pre-state with a
journal update around every restore operation. Any changed state, persistence
failure, or restore failure becomes `ROLLBACK PAUSED` / `RecoveryRequired` for
manual review. A successful rollback ends `FailedSafely` with an explicit
original-state-restored result.

## Verification and test evidence

Focused domain coverage proves:

- normal plan versus explicit owner/lab qualification;
- exact whitelist/blacklist collateral preservation and deterministic deltas;
- dry-run journal persistence, stale-plan no-mutation, and in-memory
  apply/read-back completion;
- independent postcondition failure, exact guarded rollback, and preserved
  original state;
- restart reconciliation that reads an incomplete journal and current state
  without a retry, rollback, or mutation;
- corrupt journal rejection;
- helper frame round-trip and rejection of wrong nonce, expired request,
  R2 operation, altered target/delta, altered connectivity-only flag, and
  oversized payload;
- the Phase 1 Windows provider contains no SET IOCTL, registry-write,
  service-control, or process-launch mutation surface.

The standalone `Missing HOTAS Exemption` fixture completed a schema-4
`--plan-repair --dry-run-repair` pass with exactly one LabQualified
`HD-R1-ADD-HOTAS-WHITELIST` operation and a durable dry-run journal. The
helper’s non-mutating `--protocol-version` connectivity probe exited zero.
The lifecycle build also loaded the Lab-only QML surface with
`--development-fixture "Missing HOTAS Exemption" --lab-repair-mode
--startup-smoke`; no action button was clicked, no UAC request was made, and
no HidHide configuration mutation was attempted.

The candidate also completed a fresh non-elevated, real-machine, read-only
schema-4 diagnosis: `Incomplete HidHide driver replacement` and `Broken HID
device enumeration`, with no R1 plan. That is the expected deep-repair result;
it does not manufacture a configuration repair. No live HidHide configuration
was changed during this work. The current-machine owner review candidate must
remain open after the final handoff.

The dedicated Release synthetic mapper hot-path benchmark completed with zero
tracked allocations in every reported linear, adaptive, profile-control, and
automation scenario. Its measured scope intentionally excludes physical
DirectInput polling and the vJoy driver call; Phase 3 adds no code to that
path.

## Deferred Phase 4 entry recommendation

Do not promote an R1 recipe to FieldQualified from this candidate. Phase 4
may consider component/package/recovery work only after the Phase 3 owner
reviews the Lab-qualified helper flow and a broader real-machine evidence
matrix establishes the relevant provider, Windows build, architecture,
rollback, helper-crash, cancellation, restart-reconciliation, and explicit
ACL packaging gates. Existing HOTAS setup/repair paths remain independent.
