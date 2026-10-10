# HidHide Doctor

## Repair Safety Model

> **Status:** Design Freeze v1.1. Governing safety, authorization, backup, rollback, and verification rules for HidHide Doctor repairs.

| **Document** | HidHide Doctor Repair Safety Model |
|---|---|
| **Version** | Design Freeze v1.1 |
| **Purpose** | Define when the Doctor may change a system and how it remains recoverable |
| **Primary rule** | If the Doctor cannot prove a repair is appropriate and safely bounded, it does not perform it automatically |

# 1. Safety Thesis

> **The safest failed repair is the one the Doctor refuses to start. The second safest is a fully planned, narrowly scoped, reversible repair whose result is independently verified.**

HidHide Doctor is allowed to be extremely capable. It is not allowed to be reckless.

This document intentionally makes the repair engine more conservative than the diagnostic engine.

# 2. Separation of Responsibilities

A Doctor workflow has four stages:

```text
OBSERVE
  ↓
DIAGNOSE
  ↓
PLAN
  ↓
REPAIR
  ↓
VERIFY
```

Observation and diagnosis are read-only.

Planning is read-only.

Only Repair mutates state.

Verification is read-only whenever practical.

No stage may silently collapse into the next.

# 3. Repair Risk Classes

| Class | Name | Typical scope | Default authorization |
|---|---|---|---|
| R0 | Observe | No changes | Automatic |
| R1 | Configuration Repair | HidHide config + HOTAS-specific entries | User authorizes plan; usually no reboot |
| R2 | Component Repair | Service/filter/component consistency | Explicit authorization + elevation |
| R3 | Package Repair | Reinstall approved current package | Explicit authorization + elevation; reboot likely |
| R4 | Approved Upgrade | Install a specifically qualified newer package | Explicit authorization + elevation; reboot likely |
| R5 | Recovery | Remove damaged install state, install known-good package, restore config | Strong confirmation + rollback/recovery prerequisites |

The class is determined by the most invasive operation in the plan.

# 4. Universal Preconditions

Before any R1+ repair:

- diagnosis exists;
- evidence supporting the diagnosis exists;
- repair is mapped to that diagnosis;
- required evidence confidence threshold is met;
- repair is marked qualified for the detected provider/version/OS context;
- contradictory evidence has been evaluated;
- current state is revalidated immediately before commit;
- user can see the plan;
- required backup/rollback prerequisites are satisfied;
- operation-specific timeout/failure handling exists.

If any precondition fails, repair does not start.

# 5. Repair Qualification Levels

Every repair recipe has a qualification status:

- `Experimental` — developer/test only, never automatic for ordinary users.
- `LabQualified` — automated/fault-injection coverage, not yet enough real-machine evidence.
- `FieldQualified` — qualified on supported real-machine matrix and eligible for user-facing automatic repair.
- `Retired` — no longer offered; retained for historical diagnosis/rollback compatibility.

Only `FieldQualified` repairs may be offered as ordinary automatic repairs.

# 6. Evidence Thresholds

Suggested minimum policy:

## R1 Configuration

- diagnosis confidence: High or stronger;
- exact targeted entry/state known;
- collateral ownership known;
- backup captured.

## R2 Component

- diagnosis confidence: Very High or Confirmed;
- component identity exact;
- current state verified immediately before change;
- rollback path tested.

## R3/R4 Package

- diagnosis confidence: Very High or Confirmed;
- provider/package identity exact;
- artifact is pinned/approved;
- signatures/hashes validated;
- install/remove/reboot semantics qualified.

## R5 Recovery

- diagnosis: Confirmed where practical;
- less invasive repairs either inapplicable or failed safely;
- all preserved configuration captured;
- known-good package locally available or fetched through a trusted non-elevated path then verified;
- rollback/recovery plan validated;
- user receives strong explicit summary.

# 7. Ownership and Collateral-Change Rules

HidHide is a shared system component.

The Doctor must not assume HOTAS BF6 owns the entire whitelist, blacklist, or all HidHide settings.

## 7.1 HOTAS-owned vs user/unrelated state

Repairs should classify entries as:

- HOTAS BF6 owned/expected;
- Doctor owned/transient;
- user/unrelated;
- unknown ownership.

Automatic cleanup may modify HOTAS-owned items when justified.

Unknown/unrelated entries are preserved unless the diagnosis specifically proves they are corrupted and a qualified recovery explicitly requires intervention.

## 7.2 Additive preference

Where possible, prefer additive/narrow edits:

- add missing HOTAS exemption;
- remove exact stale duplicate HOTAS path;
- hide exact required physical device;
- unhide exact accidentally-hidden virtual output.

Do not replace the entire whitelist/blacklist merely because rewriting the whole list is easier.

# 8. Backup Model

A repair backup is **not** “we wrote a log saying what we intended.”

It is enough state to restore the relevant prior configuration.

## 8.1 R1 backup

Capture at minimum:

- whitelist;
- blacklist;
- active state;
- inverse state;
- relevant HOTAS expected configuration;
- exact evidence timestamp.

## 8.2 R2 backup

R1 plus relevant:

- service configuration;
- filter registration;
- component file/version identity;
- device/PnP state snapshot;
- relevant registry values.

## 8.3 R3-R5 backup

R2 plus:

- installed package identities;
- active Driver Store package;
- relevant installer metadata;
- file hashes/versions;
- package artifacts/references needed for rollback when lawful/practical;
- restart state.

# 9. Repair Transaction Journal

Every R1+ repair receives a durable `RepairTransaction`.

Minimum fields:

```text
transactionId
sessionId
planId
riskClass
provider
osBuild
startedAt
state
preconditions[]
backupManifest
operations[]
currentOperation
rebootBoundary
verificationPlan
rollbackPlan
finalStatus
```

Each operation stores:

```text
operationId
type
target
preValue
requestedValue
actualPostValue
startedAt
completedAt
result
nativeError
rollbackStatus
```

# 10. Typed Repair Operations

The elevated helper accepts only typed operations.

Examples:

- `SetHidHideActive`
- `SetHidHideInverse`
- `AddWhitelistEntry`
- `RemoveWhitelistEntry`
- `AddBlacklistEntry`
- `RemoveBlacklistEntry`
- `RepairExactServiceConfiguration`
- `RepairExactFilterRegistration`
- `InstallApprovedPackage`
- `RemoveExactApproved/CorruptPackage`
- `StageRebootContinuation`

There is no:

```text
RunPowerShell("whatever string the UI supplied")
```

There is no general elevated file delete.

There is no unrestricted registry write.

# 11. Configuration Repairs (R1)

Candidate R1 repairs may include:

- add missing HOTAS BF6 application exemption;
- repair moved/stale HOTAS executable path;
- remove duplicate equivalent HOTAS entries;
- enable/disable cloak when diagnosis and user intent make this unambiguous;
- correct inverse setting only when expected semantics are known and user confirms impact;
- add/remove exact intended physical-device blacklist entries;
- remove exact accidentally hidden HOTAS virtual output;
- remove exact stale duplicate entry proven to be HOTAS-owned;
- preserve all unrelated entries.

R1 changes must be immediately re-read from the driver/API and compared to intended result.

# 12. Component Repairs (R2)

R2 may address:

- missing/inconsistent HidHide service registration;
- proven orphan filter registration;
- exact install component missing while package identity remains valid;
- control-device registration issue with qualified repair;
- exact provider/version-specific setup consistency failures.

R2 may not “normalize” a machine by rewriting every HID filter.

If repair would alter unrelated device-class filters, it is no longer an ordinary R2 automatic repair.

# 13. Package Repair (R3)

R3 reinstalls/repairs the **approved currently intended HidHide package**.

Requirements:

- immutable package identity;
- known source;
- hash/signature validation;
- architecture match;
- space/preflight;
- preserved configuration;
- explicit reboot handling;
- post-reboot loaded-driver verification;
- no declaration of success before verification.

The Doctor should prefer official provider installation semantics over hand-copying kernel binaries.

# 14. Approved Upgrade (R4)

R4 installs a specifically qualified newer package.

It is NOT:

> install newest upstream branch.

A package qualifies only if:

- version/commit/build is pinned;
- artifact provenance is trusted;
- signing is valid for target Windows;
- Doctor compatibility tests passed;
- supported OS matrix is declared;
- rollback path exists;
- migration/configuration behavior is known.

A future HOTAS HidHide fork could become an R4 provider, but that is outside Doctor v1.

# 15. Recovery (R5)

R5 exists for systems whose installation state is too damaged for narrower repair.

Potential sequence:

```text
freeze current evidence
↓
capture complete HidHide recovery backup
↓
validate known-good package available
↓
disable further automatic mutations
↓
remove exact damaged HidHide package/components
↓
reboot if required
↓
install known-good approved package
↓
reboot if required
↓
restore preserved configuration conservatively
↓
verify protocol
↓
verify device enumeration
↓
verify HOTAS isolation
↓
complete
```

R5 must never become a generic “clean all controller drivers” button.

# 16. Package Acquisition Safety

If a package must be downloaded:

- download occurs in non-elevated process;
- source is allowlisted/pinned by approved metadata;
- TLS/network error is handled;
- artifact hash is checked;
- code signature is checked;
- version/provider/architecture are checked;
- artifact is staged read-only;
- elevated helper receives exact verified artifact identity/path.

Never ask elevated helper to download code from the internet.

# 17. Revalidation Before Commit

A repair plan is based on evidence at time T1.

Immediately before mutation at T2, revalidate all material preconditions:

- same provider/package target;
- same active HidHide installation;
- same relevant configuration revision/value;
- same relevant device identity;
- no conflicting repair already running;
- artifact still matches approved hash/signature;
- reboot state has not invalidated assumptions.

If preconditions changed, pause and re-plan.

# 18. Failure Containment

If an operation fails:

1. stop dependent operations;
2. record exact failure;
3. determine whether current partial state is safe;
4. rollback if defined and safer;
5. otherwise enter `FailedSafely` / `RecoveryRequired`;
6. do not continue performing unrelated “maybe this helps” operations.

# 19. Rollback Semantics

Rollback must be operation-aware.

Examples:

- Configuration edit rollback restores exact prior list/state, but only if preconditions prove no newer external edit would be overwritten.
- Package rollback uses approved package semantics, not raw file copying into a live kernel-driver path.
- Reboot-boundary rollback may itself require reboot.
- If safe rollback cannot be guaranteed, the plan must say so **before** repair begins.

# 20. External Changes and Conflict Safety

HidHide Doctor is not the only application that can modify HidHide.

Before restoring/undoing configuration:

- compare current state to transaction’s expected state;
- if another application/user changed the same field/list, do not blindly overwrite;
- show conflict and generate a reconciliation plan.

This is particularly important for whitelist/blacklist lists.

# 21. Reboot Safety

A repair can enter `AwaitingReboot`.

Before restart:

- transaction journal flushed;
- continuation registration is narrow and self-identifying;
- next expected phase recorded;
- user knows why reboot is needed;
- no “repair complete” label displayed.

After restart:

- verify OS boot is the expected continuation context;
- reload transaction;
- revalidate installed/loaded state;
- continue idempotently;
- remove continuation mechanism after terminal state.

# 22. User Authorization UX

The confirmation UI must summarize:

```text
Problem being repaired
Evidence/confidence
Changes planned
Risk class
Elevation needed?
Restart needed?
Backup/rollback availability
Expected time
What the user must do
```

Do not show a terrifying legal essay for R1, nor a casual one-line confirmation for R5.

# 23. Safe Cancellation

Read-only diagnosis can generally cancel immediately.

Repair cancellation rules depend on operation boundary:

- before mutation: safe cancel;
- between atomic operations: cancel if system is in valid state;
- during package/driver operation: may need to wait for bounded operation completion;
- after reboot staged: cancellation may require rollback/finish rather than abandon.

UI must explain why Cancel is temporarily unavailable.

# 24. Hard Prohibitions

HidHide Doctor shall not automatically:

- disable Secure Boot;
- disable driver-signature enforcement;
- enable Windows test-signing;
- disable Memory Integrity/HVCI merely to load a package;
- remove unrelated HID/vendor drivers;
- delete arbitrary Driver Store packages;
- purge broad registry trees;
- install unsigned/untrusted kernel drivers for ordinary users;
- install arbitrary branch/nightly builds;
- run user-supplied elevated scripts;
- bypass UAC;
- claim a crash/hang cause without evidence;
- overwrite new external configuration changes during rollback;
- call a system “fixed” because an installer exited zero.

# 25. Verification Model

Every repair has a specific verification plan.

Minimum post-repair verification may include:

- intended configuration re-read;
- package version;
- loaded driver version;
- control-device open;
- relevant independent API calls;
- device enumeration;
- HOTAS-specific whitelist/hide expectations;
- virtual output visibility expectations;
- no new critical Windows setup/driver errors;
- reboot requirement cleared where expected.

Verification results become new evidence items.

# 26. Automatic Repair Eligibility Matrix

| Condition | Auto-repair eligibility |
|---|---|
| Diagnosis uncertain | No invasive repair |
| Diagnosis high, R1 exact HOTAS-owned entry | Eligible if FieldQualified |
| Diagnosis confirmed, R2 recipe LabQualified only | Developer/test only |
| Package source/version not pinned | No |
| Artifact signature/hash invalid | No |
| Rollback required but backup failed | No |
| External state changed after plan | Pause/replan |
| Reboot needed | Allowed only with durable continuation |
| Known issue but no qualified remedy | Diagnose/report only |
| Repair already failed once with unexpected collateral state | Stop; no escalating roulette |

# 27. Repair Knowledge-Base Contract

A repair knowledge entry contains:

```text
repairId
title
addressesDiagnosisIds[]
riskClass
qualification
applicableProviderVersions
applicableOsBuilds
requiredEvidence[]
contradictions[]
operations[]
backupRequirements[]
rollback[]
verification[]
knownFailureModes[]
```

Repair rules are versioned.

Changing a repair recipe after field release creates a new recipe version.

# 28. HOTAS BF6 Integration Repairs

When launched from HOTAS BF6, the Doctor may have trusted local context describing:

- expected HOTAS executable path;
- device rig physical members;
- expected virtual outputs;
- intended isolation semantics.

This context is a **hint**, not proof.

Doctor independently validates relevant device/application identities before modifying HidHide.

# 29. Repair Status Language

Use precise terminal language:

### Repair Complete

All planned operations succeeded and verification passed.

### Repair Applied - Restart Pending

Changes staged; verification incomplete until restart.

### Repair Partially Applied - Recovery Needed

Some changes occurred; plan did not complete; recovery/rollback guidance active.

### Repair Failed Safely

No material system change remains or rollback succeeded.

### Diagnosis Complete - No Qualified Repair

Nothing changed.

Never use “Done!” for an unfinished reboot-boundary operation. Small word, enormous capacity for betrayal.

# 30. Qualification Tests

Every repair recipe must test:

- expected healthy case;
- target failure case;
- precondition absent;
- permission denied;
- operation timeout;
- operation returns reboot required;
- reboot continuation;
- verification failure;
- rollback;
- external concurrent configuration change;
- repeat invocation/idempotence;
- cancellation boundary;
- malformed/unknown provider version;
- logging/report evidence.

R2+ also requires real-machine qualification on an explicit Windows/HidHide matrix.

# 31. Release Gates

Automatic repair cannot ship if:

- elevated helper has general command execution;
- package source can float;
- rollback requirements are undefined;
- repair can overwrite unrelated user configuration;
- preconditions are not revalidated;
- reboot continuation is not durable/idempotent;
- verification is installer-return-code only;
- a destructive operation lacks fault-injection tests;
- user cannot see what will change;
- Doctor can escalate to a more invasive repair without a new plan.

# 32. One-Sentence Safety Test

> **HidHide Doctor may be extremely powerful, but every automatic change must be narrower than the diagnosis, authorized before execution, recoverable when required, and independently verified afterward.**


# v1.1 Repair Safety Amendment — Platform Qualification

## RS-PORT-001 — No repair by platform assumption

A repair may not be authorized merely because it worked on the development machine or on another Windows build.

Every environment-sensitive repair declares supported:
- Windows version/build range;
- architecture;
- HidHide generation/capability range;
- package type;
- required privilege level.

## RS-PORT-002 — Architecture mismatch is a hard block

Before any package/component/driver mutation:
- native machine architecture is measured;
- repair-helper architecture is verified;
- replacement-package architecture is verified;
- driver architecture compatibility is verified.

Any mismatch blocks the repair before state mutation.

## RS-PORT-003 — Diagnosis support precedes repair support

A platform can be certified for read-only diagnosis without being certified for repair.

Repair capability tiers are granted independently:
1. Read-only diagnosis
2. Configuration repair
3. Component repair
4. Package repair
5. Upgrade repair
6. Recovery repair

The UI must state the current machine's qualified tier.

## RS-PORT-004 — Unknown/new Windows builds default conservative

On a newer unqualified Windows build:
- read-only diagnostics continue where compatible;
- low-risk configuration repairs may run only if their contracts are proven OS-independent;
- driver/package/filter-chain repairs are blocked until qualified unless an explicit emergency/research mode is being used by a developer.

No ordinary user receives experimental kernel-driver surgery by accident.

## RS-PORT-005 — No hard-coded paths in repair plans

Every path used by a repair plan must be discovered and revalidated at execution time.

Repair code may not assume:
- `C:\Program Files\...`;
- a specific Windows drive;
- a particular user profile;
- a particular HidHide installation layout.

## RS-PORT-006 — Other users' HidHide state is not owned by HOTAS BF6

On any machine, preserve unrelated applications and device configuration unless a repair specifically proves that an entry is invalid and the repair policy authorizes changing it.

The Doctor must remain conservative in shared HidHide installations.

## RS-PORT-007 — Display and UI portability cannot affect repair truth

A repair's progress/session model is independent of Focus vs Command Center layout, DPI, monitor count, and window geometry.

Resizing or moving the UI cannot alter repair execution.

## RS-PORT-008 — Clean-machine repair qualification

Every invasive repair class must be validated on clean-machine or disposable VM fixtures before field qualification.

Required fixtures include, as applicable:
- clean x64 Windows install;
- clean ARM64 Windows install once ARM64 repair is production-supported;
- HidHide absent;
- clean install;
- client-only/driver-only partial states;
- version mismatch;
- stale Driver Store package;
- broken configuration;
- reboot-pending replacement;
- simulated API failure;
- malformed device enumeration.

## RS-PORT-009 — Rollback is platform-specific evidence

A repair is not portable merely because forward mutation succeeds.

Rollback must also be qualified on each supported repair platform.

## RS-PORT-010 — Production claim requires matrix evidence

The release closeout must list the exact OS/build/architecture matrix exercised and the repair tiers qualified on each.

"Works on my machine" is explicitly not an acceptable completion criterion.
