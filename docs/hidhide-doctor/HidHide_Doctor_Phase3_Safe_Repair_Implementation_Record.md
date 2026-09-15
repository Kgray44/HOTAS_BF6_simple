# HidHide Doctor — Phase 3 Safe Repair Implementation Record

## Provenance and candidate scope

- Accepted Phase 2 base: `920a58196bb2de952beef6cfc0de7ba2bfad3532`
- Phase 3 branch: `codex/hidhide-doctor-phase3-safe-repair`
- Isolated worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase3-safe-repair`
- Final candidate commit: recorded by the Phase 3 closeout after the commit is created (a Git commit cannot contain its own resulting SHA without changing that SHA).
- Engine contract revision: `R1 / helper IPC protocol 1 / journal schema 1 / report schema 4`

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
it neither invokes UAC nor can it obtain a mutable provider.

`--headless` remains non-mutating. Diagnosis/report generation includes an R1
plan only when evidence and HOTAS context support it. The in-memory execution
fixture exercises plan, dry-run journal, stale-plan rejection, apply, and
read-back without a Windows provider.

## Backup, journal, lock, and recovery posture

Before any execution, the coordinator performs a fresh configuration read and
requires its fingerprint to match the plan. It then captures whitelist,
blacklist, cloak, inverse, provider, timestamp, scope, privacy classification,
and SHA-256 backup manifest. A journal write failure prevents execution.

`RepairJournalStore` uses the dynamically resolved per-user
`AppLocalDataLocation/repair-transactions` directory, QSaveFile atomic
replacement, a versioned schema, and a checksum verified on read. Journals are
sensitive local diagnostic data and are not emitted to console or uploaded.
The directory relies on the normal per-user Windows application-data ACL; an
installer-level explicit ACL hardening policy remains a release packaging
decision rather than an unverified claim in this candidate.

The state model is `Planned`, authorization, backup, revalidation, elevation,
execution, verification, rollback, completion, safe-failure, recovery,
cancellation, and stale-plan states. A cross-process `QLockFile` prevents two
repair transactions from running simultaneously. A stale fingerprint stops
before mutation. Corrupt journals fail closed. Restart reconciliation is
intentionally read-back-first: no journal causes an automatic replay.

## Elevated helper and mutation boundary

`HidHideDoctorRepair.exe` is a short-lived, no-UI helper. The normal Doctor
target does not link its mutator. The helper admits a single native named-pipe
request guarded by owner/System pipe ACL entries, same interactive-session
checking, a 64 KiB frame limit, one-time uppercase nonce, short expiry,
protocol version, Doctor/helper build identity, transaction/plan IDs, and
request/plan digests. It rejects malformed, oversized, stale, mismatched, or
out-of-scope requests before opening the mutation path.

The helper accepts no shell command, command line, registry path, file path
operation, service operation, or package operation. It validates the plan’s
exact bound before/after delta and permits only:

- `SetHidHideActive`
- `SetHidHideInverse`
- `AddWhitelistEntry` / `RemoveWhitelistEntry`
- `AddBlacklistEntry` / `RemoveBlacklistEntry`

The separate `HidHideConfigurationMutator` uses the documented HidHide WDM
control device and only persistent configuration IOCTL functions 2049, 2051,
2053, and 2055 after the helper’s independent GET revalidation. Lists are
rebuilt as bounded `MULTI_SZ` buffers from an exact in-memory delta. A SET
result is not completion: the coordinator reads configuration back and checks
the authorized post-state fingerprint before `Completed`.

If an operation fails, dependent work stops and the exact native error is
journaled. If post-state differs, the transaction becomes
`RecoveryRequired`; it does not overwrite a possible external change. The
current rollback posture is deliberately conservative: the exact captured
pre-state is retained, and automatic rollback is withheld unless a future
reviewed policy can prove the current state is still Doctor’s expected state.

## Verification and test evidence

Focused domain coverage proves:

- normal plan versus explicit owner/lab qualification;
- exact whitelist/blacklist collateral preservation and deterministic deltas;
- dry-run journal persistence, stale-plan no-mutation, and in-memory
  apply/read-back completion;
- corrupt journal rejection;
- helper frame round-trip and rejection of wrong nonce, expired request,
  R2 operation, altered target/delta, and oversized payload;
- the Phase 1 Windows provider contains no SET IOCTL, registry-write,
  service-control, or process-launch mutation surface.

The standalone `Missing HOTAS Exemption` fixture completed a schema-4
`--plan-repair --dry-run-repair` pass with exactly one LabQualified
`HD-R1-ADD-HOTAS-WHITELIST` operation and a durable dry-run journal. The
helper’s non-mutating `--protocol-version` connectivity probe exited zero.

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
