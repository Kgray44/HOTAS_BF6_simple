# HidHide Doctor

## Invariant Sheet

> **Status:** Design Freeze v1.1. Non-negotiable implementation rules companion to the Master Design Specification.

| **Document** | HidHide Doctor |
|---|---|
| **Status** | Implementation law |
| **Version** | Design Freeze v1.1 |
| **Scope** | Standalone HidHide diagnostics, recovery, repair, and HOTAS BF6 integration |

**Core product thesis**

> **HidHide Doctor must know what it is doing, show what it is doing, prove what it found, change only what it is authorized and qualified to change, and verify the result.**

# How to Use This Sheet

Keep this sheet visible during every implementation phase.

A requirement is not satisfied by visually imitating the rule while violating its backend meaning.

If a proposed shortcut violates an invariant, stop and fix the design or explicitly amend the governing documents.

# Product and Ownership

### HD-I-01 — Separate utility

HidHide Doctor is a separate executable/window, not a modal or giant embedded HOTAS BF6 page.

### HD-I-02 — Standalone operation

The Doctor must be independently launchable and usable when HOTAS BF6 is not running or cannot start.

### HD-I-03 — Embedded launch, not embedded ownership

HOTAS BF6 may launch/deep-link the Doctor, but does not own the Doctor’s process lifetime or diagnostic state.

### HD-I-04 — No fork dependency in v1

The initial Doctor must work against official HidHide installations. A future fork is a separate project.

### HD-I-05 — One Doctor Session model

Focus View and Command Center View render one shared session model. There is never layout-specific diagnostic state.

# Transparency

### HD-I-06 — No silent significant work

Every significant diagnostic or repair operation has visible current-phase/current-step state.

### HD-I-07 — No unexplained waiting

Any material wait must say what is being awaited, elapsed time, timeout/condition where relevant, and whether user action is required.

### HD-I-08 — Real progress only

Progress represents completed planned work, not timers, animations, or fictional percentages.

### HD-I-09 — Overall + current-step progress

Where meaningful, both are exposed.

### HD-I-10 — Findings appear as evidence arrives

The UI does not hide all findings until the final screen.

### HD-I-11 — User action is always explicit

The session always exposes either a required action or the valid state “Nothing required.”

### HD-I-12 — Disabled actions explain why

No important action is merely grayed out with no explanation.

### HD-I-13 — No opaque “Repairing…” state

Repair shows the actual plan step and exact operation category.

# UI Structure

### HD-I-14 — Four primary surfaces always exist

Diagnostic Plan, Current Step, Findings, and User Action remain first-class throughout diagnosis and repair.

### HD-I-15 — Command Center is genuinely side-by-side

The large-screen layout keeps all four primary surfaces visible simultaneously.

### HD-I-16 — Independent pane scrolling

A long Findings list does not scroll Plan/Current/User Action out of view.

### HD-I-17 — User-resizable Command Center panes

Pane widths may change without affecting session state.

### HD-I-18 — Pane maximization is temporary presentation

Maximizing Findings or another pane never creates a different workflow.

### HD-I-19 — Focus View is complete

Focus View is not a crippled/simple mode. It can access all information and actions.

### HD-I-20 — Live layout switching

The user may switch Focus/Command Center during any safe diagnostic/repair state without restarting work.

### HD-I-21 — Density is presentation-only

Comfortable/Compact/Dense do not alter checks, evidence, repair logic, or results.

### HD-I-22 — Current operation remains visible

A user buried in technical evidence can still determine whether the Doctor is running and what it is doing.

# Evidence and Diagnosis

### HD-I-23 — Evidence is not diagnosis

Observed facts are preserved separately from interpreted causes.

### HD-I-24 — Diagnosis is not repair

Knowing the cause does not imply an automatic repair is safe or available.

### HD-I-25 — Evidence provenance is retained

Every important result knows where it came from.

### HD-I-26 — Native error codes are preserved

Do not collapse `ERROR_INVALID_PARAMETER`, access denied, timeout, missing device, etc. into “HidHide error.”

### HD-I-27 — Failed probe isolation

One failed HidHide operation does not abort unrelated diagnostic checks.

### HD-I-28 — No all-or-nothing HidHide Boolean

Installation, driver, protocol, configuration, devices, and isolation health are separate dimensions.

### HD-I-29 — Broken device cannot crash Doctor

Device enumeration is fault-isolated and per-device where practical.

### HD-I-30 — Confidence is evidence-driven

Confidence may not be marketing text or an AI-vibe percentage.

### HD-I-31 — Contradicting evidence matters

A diagnosis rule must be able to lower/reject itself when contradiction appears.

### HD-I-32 — “Unknown” is valid

When evidence is insufficient, the Doctor says so instead of guessing.

# HidHide Integration

### HD-I-33 — Direct protocol client

The Doctor must not depend exclusively on HidHideClient.exe or HidHideCLI.exe for core diagnosis.

### HD-I-34 — Independent operation probes

Active, inverse, whitelist, blacklist, session capability, and control-device access are tested independently when supported.

### HD-I-35 — Installed executable version is not driver capability

Actual control-interface behavior wins over assumptions from a version string.

### HD-I-36 — Preserve unrelated HidHide configuration

HOTAS BF6 is not the sole owner of a user’s HidHide configuration.

### HD-I-37 — Read-only before mutation

If a question can be answered read-only, do not mutate the system to answer it.

# Privilege and Security

### HD-I-38 — Least privilege UI

Primary Doctor process is non-elevated by default.

### HD-I-39 — Narrow elevated helper

Privileged repairs use a separate typed-operation helper.

### HD-I-40 — No elevated arbitrary shell

The repair helper cannot be used as a generic command executor.

### HD-I-41 — Plan integrity at elevation

The helper executes the authorized plan, not whatever the UI asks later.

### HD-I-42 — No security-disable “repair”

The Doctor does not disable Secure Boot, driver signature enforcement, Memory Integrity, or enable test-signing as an ordinary repair strategy.

### HD-I-43 — Approved package provenance

Any automatic reinstall/upgrade uses pinned, verified, approved artifacts only.

### HD-I-44 — Never auto-install arbitrary upstream master

Development head is not a repair channel.

# Repair Safety

### HD-I-45 — Diagnosis precedes invasive repair

R2+ repairs require a completed diagnosis/preflight unless a governing recovery exception explicitly defines otherwise.

### HD-I-46 — Repair plan shown before material changes

The user can see what will change.

### HD-I-47 — Typed repair operations

Repairs are structured operations with preconditions/results, not ad hoc scripts hidden behind buttons.

### HD-I-48 — Revalidate before commit

If system state changes between diagnosis and repair, repair preconditions are rechecked.

### HD-I-49 — Snapshot before material repair

Configuration/package/system evidence needed for rollback is captured first.

### HD-I-50 — Rollback is designed, not improvised

Repairs with rollback requirements define rollback before execution.

### HD-I-51 — Never expand scope silently

If repair discovers a second problem requiring more invasive action, pause and generate a new/extended plan.

### HD-I-52 — Stop on unexpected collateral change

If an operation affects unrelated device/filter/software state outside the approved plan, stop safely.

### HD-I-53 — No blind registry cleaner behavior

Delete only proven HidHide-owned stale state under a qualified remediation.

### HD-I-54 — No blind driver-store purge

Package removal is identity/version/provider targeted and safety checked.

### HD-I-55 — Installer exit code is not verification

Success is proven by post-repair checks.

### HD-I-56 — Reboot-required means incomplete

A staged replacement is not called “fixed” before post-reboot verification.

### HD-I-57 — Repair can end with “not available”

The Doctor may diagnose a problem perfectly and intentionally make no change.

# Reboot and Durability

### HD-I-58 — Repair transaction journal is durable

Repair state survives application restart/reboot.

### HD-I-59 — Resume identifies prior session

The user sees that repair is continuing, not mysteriously starting over.

### HD-I-60 — Reboot continuation is idempotent

Repeated startup/resume cannot repeatedly apply the same destructive step.

### HD-I-61 — Interrupted repair is detectable

Doctor can distinguish clean completion from abandoned/incomplete transaction.

# Reports and Privacy

### HD-I-62 — Reports separate fact from inference

Evidence, findings, diagnoses, and repair history remain distinguishable.

### HD-I-63 — Export is sanitized

Do not casually export unrelated personal paths, app inventory, registry data, or event logs.

### HD-I-64 — Export contents are previewable

User can inspect categories/items before sharing.

### HD-I-65 — Stable IDs

Checks, findings, diagnoses, repair operations, and knowledge-base signatures use stable identifiers suitable for logs/tests/support.

# Performance and Reliability

### HD-I-66 — UI never blocks on long system calls

Blocking probes run off the UI thread.

### HD-I-67 — Every external operation is bounded

Timeout/cancellation semantics exist where possible.

### HD-I-68 — Diagnostic failure cannot freeze the interface

A wedged probe becomes a timed-out result, not a frozen application.

### HD-I-69 — Live evidence is rate-limited

The interface does not melt because Windows emitted 9,000 interesting things.

### HD-I-70 — Idle means idle

No decorative animation or polling storm should consume meaningful resources while waiting.

# Testing and Qualification

### HD-I-71 — Read-only brain before repair brain

Deep diagnostics must be proven before automatic repair is enabled.

### HD-I-72 — Fault injection is mandatory

Access denied, timeouts, malformed data, missing files, version mismatch, broken devices, and failed repair steps are tested intentionally.

### HD-I-73 — UI transparency is testable

Tests verify that active step, progress, findings, and user action correspond to session state.

### HD-I-74 — Layout equivalence is testable

Switching Focus/Command Center cannot alter session output.

### HD-I-75 — Real-machine repair qualification required

A destructive/system repair is not “qualified” solely because a mock passed.

### HD-I-76 — Known issue is not automatically a known repair

Issue signatures require explicit remediation qualification.

# Stop Conditions

**STOP implementation/release if any of these occur:**

- primary GUI requires elevation for ordinary diagnosis;
- Focus View and Command Center have different diagnostic state;
- progress is timer-faked;
- a failed whitelist query aborts all diagnostics;
- one malformed HID device can terminate the Doctor;
- automatic repair is triggered solely from a screenshot/version string;
- repair modifies unrelated HidHide user configuration unnecessarily;
- arbitrary upstream master can be installed automatically;
- elevated helper can run arbitrary commands supplied by UI;
- repair proceeds after preconditions changed without revalidation;
- reboot is treated as completion without resumed verification;
- package success is accepted without health verification;
- Doctor disables Windows security to force compatibility;
- a material repair has no rollback/recovery story;
- long waits can occur with no visible reason;
- an enabled critical action can fail silently;
- Doctor claims “healthy” merely because it could not finish a check.

# Phase Exit Checklist

Every phase must confirm:

- separate executable boundary preserved;
- least-privilege model preserved;
- session model remains single source of truth;
- both layouts render same state;
- progress remains real;
- no silent work/wait;
- evidence remains fact, diagnosis remains inference;
- stable IDs added for new checks/rules/repairs;
- unrelated HidHide configuration remains protected;
- no new arbitrary elevated capability;
- tests include failure cases, not only success;
- no repair capability is exposed before its safety/verification contract exists.

# One-Sentence Product Test

> **A beginner should be able to watch HidHide Doctor calmly diagnose and repair their system without guessing what to do, while an expert should be able to expand the exact same session and see enough preserved evidence to explain every conclusion and every system change.**


# v1.1 Portability Invariants

**I-P01 — The owner's PC is never the product specification.**  
No implementation may assume the development machine's paths, user name, drive layout, controller inventory, HidHide install state, Windows build, DPI, or architecture.

**I-P02 — Supported Windows machines are discovered, not guessed.**  
OS version/build/edition, CPU architecture, process architecture, package architecture, and protocol capability must be measured before architecture- or build-sensitive work.

**I-P03 — HOTAS BF6 is optional.**  
The Doctor must remain independently launchable and useful without HOTAS BF6 installed or running.

**I-P04 — x64 and ARM64 are distinct qualification targets.**  
No cross-architecture package installation. Every architecture-sensitive binary or package is verified before use.

**I-P05 — Unknown environments fail safe.**  
Read-only diagnosis may continue on newer/unqualified Windows builds when technically compatible; invasive repair may not silently assume qualification.

**I-P06 — One bad device cannot kill the session.**  
Per-device faults are isolated and recorded.

**I-P07 — Locale cannot determine correctness.**  
Never depend on English command text when a structured/native result exists.

**I-P08 — Clean-machine tests are mandatory.**  
Production readiness requires qualification on clean machines/VMs other than the developer workstation.

**I-P09 — Fresh-user state is mandatory.**  
The Doctor cannot require preexisting caches, HOTAS BF6 settings, or developer-created registry state.

**I-P10 — Platform support is layered.**  
Diagnosis support and repair support are separate qualification claims.

**I-P11 — Dynamic capabilities beat version assumptions.**  
Probe actual HidHide and Windows capabilities rather than inferring everything from a version string.

**I-P12 — Any machine-specific constant is presumed a defect until proven a stable platform constant.**

**I-P13 — Reports contain reproducibility evidence, not personal identity.**

**I-P14 — UI must survive real Windows display diversity.**  
High DPI, laptop-scale windows, and differing monitor scales are qualification cases.

**I-P15 — A release that passes only on the owner's computer fails the release gate.**

## Additional STOP conditions

STOP if:
- a developer-machine path, GUID, controller identity, drive letter, or account name appears in production logic;
- an x64 package can reach an ARM64 repair path or vice versa;
- a new Windows build automatically receives invasive repair authorization merely because diagnostics ran;
- one malformed HID device terminates the overall session;
- localized command output is parsed as the sole source of a critical diagnostic fact;
- automated tests cannot create a clean-machine/fresh-user fixture;
- the Doctor cannot launch independently of HOTAS BF6.
