# HidHide Doctor

## Master Design Specification

> **Status:** Design Freeze v1.1. Governing specification for the HidHide Doctor project.

| **Document** | HidHide Doctor |
|---|---|
| **Status** | Governing design document - implementation requires phase-specific authorization |
| **Version** | Design Freeze v1.1 |
| **Target host** | Windows 10/11, x64 first; architecture must not preclude ARM64 |
| **Product relationship** | Separate utility distributed with HOTAS BF6 Simple; launchable from HOTAS BF6 and independently |
| **Primary mission** | Exhaustive HidHide diagnosis, deterministic failure classification, safe automated repair, transparent user guidance, and post-repair verification |
| **Companion documents** | Invariant Sheet, Diagnostic Catalog, Repair Safety Model |

**Core product thesis**

> **HidHide Doctor must make HidHide failures understandable and repairable without requiring the user to become a Windows kernel-driver expert. It shall collect unusually deep evidence, explain exactly what it is doing at all times, automatically repair every failure for which a tested deterministic remediation exists, and refuse to guess when evidence is insufficient.**

# Document Map

| # | Section |
|---:|---|
| 1 | Purpose, status, and governance |
| 2 | Product position and scope boundaries |
| 3 | Product architecture |
| 4 | Relationship to HOTAS BF6 |
| 5 | Application/process and privilege architecture |
| 6 | Doctor session model |
| 7 | Diagnostic-plan and progress contract |
| 8 | User-interface information architecture |
| 9 | Focus View |
| 10 | Command Center View |
| 11 | Responsive layout, density, and persistence |
| 12 | Operation transparency and user-action contract |
| 13 | Evidence model |
| 14 | Diagnostic engine architecture |
| 15 | HidHide protocol probing |
| 16 | Installation/package discovery |
| 17 | Driver and Windows evidence |
| 18 | Device ecosystem diagnostics |
| 19 | Isolation verification |
| 20 | Diagnosis/classification engine |
| 21 | Knowledge base |
| 22 | Repair planning architecture |
| 23 | Repair execution and elevated helper |
| 24 | Backup, rollback, and transaction journal |
| 25 | Reboot continuation |
| 26 | Provider/version management |
| 27 | Reports, export, privacy, and support bundles |
| 28 | Failure handling and degraded operation |
| 29 | Performance and responsiveness |
| 30 | Accessibility and interaction quality |
| 31 | Security model |
| 32 | Testing and qualification |
| 33 | Implementation phases |
| 34 | Explicitly deferred scope |
| 35 | Completion criteria |
| A | Representative workflows |
| B | Technical baseline and upstream assumptions |
| C | Requirement index |
| D | Glossary |

# 1. Purpose, Status, and Governance

This document freezes the intended product behavior and architectural boundaries of HidHide Doctor before implementation. It exists specifically to prevent the project from degenerating into an opaque “Repair” button, an unstructured pile of PowerShell commands, or a dangerous administrator-mode GUI that makes undocumented system changes.

## 1.1 Governing rule

If implementation behavior conflicts with this specification or the HidHide Doctor Invariant Sheet, the implementation must change or the governing documents must be intentionally amended.

Silent scope drift is not acceptable.

## 1.2 Design priorities

In order:

1. **Correct diagnosis**
2. **User and system safety**
3. **Transparency**
4. **Recoverability**
5. **Repair capability**
6. **Verification**
7. **Ease of use**
8. **Technical depth**
9. **Visual polish**

Visual quality matters, but the Doctor is not a theme-design project. It should look excellent because clear diagnostic information deserves excellent presentation, not because the tool needs decorative aerospace furniture.

## 1.3 Evidence before action

The Doctor shall distinguish:

- **Observation:** what was directly measured.
- **Diagnosis:** what the evidence means.
- **Repair:** what tested action can address the diagnosis.
- **Verification:** what proves the repair succeeded.

These are separate concepts and shall not be collapsed into one “health” Boolean.

# 2. Product Position and Scope Boundaries

HidHide Doctor is a **specialist utility**.

It is distributed as part of the HOTAS BF6 product experience, but it is not merely another HOTAS BF6 page.

## 2.1 First-class standalone utility

HidHide Doctor shall:

- have its own executable;
- open in its own native window;
- have its own application identity and visual language;
- be independently launchable even if HOTAS BF6 cannot start;
- be launchable from HOTAS BF6 with context;
- continue running if HOTAS BF6 exits;
- never depend on the main HOTAS BF6 QML page being alive.

Recommended working names:

- `HidHideDoctor.exe` - normal non-elevated UI and diagnostic process;
- `HidHideDoctorRepair.exe` - narrowly scoped elevated helper for authorized privileged operations.

Names may change during implementation; responsibility boundaries may not.

## 2.2 Primary capabilities

HidHide Doctor is responsible for:

- deep read-only discovery;
- independent HidHide control-interface probing;
- HidHide installation/package inspection;
- loaded-driver and driver-store analysis;
- configuration integrity analysis;
- gaming-device/HID enumeration analysis;
- HOTAS/feeder isolation analysis;
- Windows event/crash/setup evidence correlation;
- deterministic diagnosis;
- confidence/evidence presentation;
- repair-plan generation;
- safe configuration repair;
- safe installation/package recovery;
- reboot-aware continuation;
- rollback;
- full post-repair requalification;
- support/diagnostic bundle generation.

## 2.3 Non-goals for v1.1

The first implementation shall not:

- fork HidHide;
- ship a custom HidHide kernel driver;
- auto-install arbitrary upstream `master`;
- scrape random third-party download sites;
- disable Windows security controls to make a driver load;
- enable test-signing mode on an ordinary user system;
- bypass driver-signature enforcement;
- silently remove unrelated third-party device software;
- take exclusive ownership of a user’s entire HidHide configuration;
- mutate configuration merely to “see what happens” when a read-only check can answer the question;
- advertise an automatic repair where only an unproven experiment exists.

# 3. Product Architecture

The required conceptual architecture is:

```text
┌─────────────────────────────────────────────────────────────┐
│ HidHideDoctor.exe                                           │
│                                                             │
│  UI / Session Presenter                                     │
│           │                                                 │
│           ▼                                                 │
│  Doctor Session Engine                                      │
│  ├─ Diagnostic Plan                                        │
│  ├─ Progress Model                                         │
│  ├─ Evidence Store                                         │
│  ├─ Findings                                               │
│  ├─ Diagnosis Engine                                       │
│  ├─ Knowledge Base                                         │
│  ├─ Repair Planner                                         │
│  └─ Report Generator                                       │
│           │                                                 │
│           ├────────── Read-only Windows/HidHide probes      │
│           │                                                 │
│           └────────── Authorized repair plan ───────────┐   │
└─────────────────────────────────────────────────────────┼───┘
                                                          │ IPC
                                                          ▼
                                           ┌──────────────────────────┐
                                           │ HidHideDoctorRepair.exe  │
                                           │ elevated narrow helper   │
                                           └──────────────────────────┘
```

The session model is the single source of truth for both UI layouts. Focus View and Command Center View are renderings of the same session; they must never maintain separate diagnostic state.

# 4. Relationship to HOTAS BF6

## 4.1 Launch surfaces

HOTAS BF6 should expose **Open HidHide Doctor** from at least:

- Devices / setup;
- Diagnostics;
- App Health / Setup Assistant when a HidHide-related issue exists.

The exact visual placement can vary by theme.

## 4.2 Context-aware launch

HOTAS BF6 may launch the Doctor with context such as:

```text
--source hotas-bf6
--focus configuration-api
--issue-id <stable-app-issue-id>
```

The Doctor shall display why it was opened, but it must independently verify the state instead of trusting HOTAS BF6’s prior diagnosis as fact.

## 4.3 Result handoff

At completion the Doctor may expose a small structured result to HOTAS BF6:

- session ID;
- final health class;
- whether changes were made;
- whether reboot remains required;
- completion timestamp;
- optional finding IDs;
- optional “rerun setup verification” hint.

HOTAS BF6 remains responsible for re-running its own setup verification. The Doctor does not silently rewrite HOTAS BF6 application state to make its own report look successful.

# 5. Application/Process and Privilege Architecture

## 5.1 Least-privilege UI

`HidHideDoctor.exe` should run non-elevated by default.

Read-only diagnostics shall remain non-elevated wherever Windows permits.

A diagnostic operation must never demand administrator privileges merely because some later repair might need them.

## 5.2 Narrow elevated helper

Privileged changes shall be performed by a separate narrow helper after an explicit repair plan is authorized.

The helper must:

- receive a signed/validated structured repair request;
- execute only operations included in that plan;
- reject unknown or expanded operations;
- report structured progress/results;
- never render the primary UI;
- never browse the web;
- never become a general-purpose arbitrary command runner.

## 5.3 UAC messaging

Before elevation, the user must see:

- why elevation is needed;
- exactly which repair phase needs it;
- whether the diagnosis itself is already complete;
- what operations are planned.

# 6. Doctor Session Model

Every diagnostic or repair run is a durable **Doctor Session**.

A session should have at least:

```text
DoctorSession
  sessionId
  createdAt
  source
  mode
  plan
  overallState
  overallProgress
  currentPhase
  currentStep
  stepProgress
  evidence[]
  findings[]
  diagnoses[]
  userAction
  repairPlan
  repairTransaction
  rebootContinuation
  finalVerification
  report
```

## 6.1 Session states

Minimum high-level states:

- Preparing
- Diagnosing
- Analyzing
- DiagnosisComplete
- AwaitingUser
- RepairReady
- Repairing
- AwaitingElevation
- AwaitingReboot
- ResumingAfterReboot
- Verifying
- RepairComplete
- DegradedComplete
- Cancelled
- FailedSafely

## 6.2 Resume semantics

A user may close the Doctor during an ordinary read-only diagnosis and start a new session later.

A repair transaction, however, must have explicit resume/rollback semantics and shall never be silently abandoned if system state may be mid-transition.

# 7. Diagnostic Plan and Progress Contract

This is a flagship UX requirement.

At all times during work the user must be able to answer:

1. What is the Doctor doing?
2. What exact phase/step is active?
3. How far through the full operation is it?
4. How far through the current step is it?
5. What has been found so far?
6. Is user action required?

## 7.1 Real progress only

The Doctor shall not display fake time-based progress.

Before work begins it constructs an **applicable diagnostic plan** from the environment and catalog.

Progress is calculated from completed weighted work:

```text
overallProgress =
    completedApplicableWeight /
    totalApplicableWeight
```

Weights represent deterministic work units, not guessed wall-clock time.

## 7.2 Applicable plan freeze

Once the main plan is established, progress should not move backward.

If genuinely new work becomes necessary, it must be represented as an explicit **Extended Investigation** phase with clear messaging rather than quietly doubling the denominator and making 72% become 39%.

## 7.3 Two progress levels

UI shall expose:

- overall percentage;
- current step percentage where the step is meaningfully subdividable.

Also expose discrete counts where useful:

```text
47 / 124 checks complete
```

## 7.4 Exact step identity

The current-step area must always show a human-readable operation and stable check/operation ID in Technical Details.

# 8. User-Interface Information Architecture

The Doctor has four primary information surfaces:

1. **Diagnostic Plan**
2. **Current Step**
3. **Findings**
4. **User Action**

These exist throughout diagnosis and repair.

A persistent top status area shows:

- overall state;
- overall progress;
- current phase and step;
- current step progress;
- elapsed time;
- user-action requirement.

A persistent bottom mini-dashboard may show:

- checks complete / total;
- healthy/info/warning/failure counts;
- evidence item count;
- rollback readiness during repair;
- reboot state.

The user may switch layouts at any point.

# 9. Focus View

Focus View is optimized for:

- narrow windows;
- first-time users;
- long explanations;
- accessibility;
- concentrated review.

Typical flow:

```text
Status
↓
Current Step
↓
Diagnostic Plan
↓
Findings
↓
User Action
```

Sections can collapse, but current status and required user action must remain easy to reach.

Focus View shall not hide any evidence or capability available in Command Center View.

# 10. Command Center View

Command Center View is optimized for larger displays and monitoring.

Default conceptual layout:

```text
┌──────────────┬──────────────────┬──────────────────────┬───────────────┐
│ PLAN         │ CURRENT STEP     │ FINDINGS             │ USER ACTION   │
│              │                  │                      │               │
│ independent  │ independent      │ independent          │ independent   │
│ scrolling    │ scrolling        │ scrolling            │ scrolling     │
└──────────────┴──────────────────┴──────────────────────┴───────────────┘
```

Recommended initial width ratios:

- Plan: 22%
- Current Step: 27%
- Findings: 36%
- User Action: 15%

These are defaults, not hard-coded laws.

## 10.1 Pane behavior

- Headers remain pinned.
- Panes scroll independently.
- Dividers are user-resizable.
- Double-clicking a divider may restore default widths.
- A pane may temporarily maximize for detailed inspection.
- Escape/close returns to the prior arrangement.
- User Action may gain stronger visual emphasis when action becomes mandatory.
- Layout changes never affect the running Doctor Session.

## 10.2 Live Evidence

An optional expert/tester view may show structured live evidence such as:

```text
20:43:17.194  Open control device           PASS
20:43:17.201  Query active state            PASS
20:43:17.209  Query whitelist               FAIL 0x57
```

This is not raw console spam. Rows are structured, filterable, and inspectable.

# 11. Responsive Layout, Density, and Persistence

## 11.1 Layout choice

The user can switch instantly between:

- Focus View
- Command Center View

No diagnostic or repair operation may restart, cancel, or change because of a view switch.

## 11.2 Density

Support presentation densities:

- Comfortable
- Compact
- Dense

Density changes only presentation.

## 11.3 Window adaptation

On insufficient width the Doctor may:

- preserve Command Center with reduced pane set;
- offer/suggest Focus View;
- move User Action to a full-width lower strip.

It shall not continuously reshuffle panes during minor resize changes.

## 11.4 Persistence

Persist presentation preferences independently of diagnostic state:

- layout mode;
- density;
- pane widths;
- expanded technical sections;
- Live Evidence preference;
- window size/position where appropriate.

# 12. Operation Transparency and User-Action Contract

## 12.1 No silent work

No significant diagnostic or repair operation may run without a visible operation state.

Any wait likely to exceed roughly one second must identify what the Doctor is waiting on, for example:

```text
Waiting for Windows to finish removing the driver package
Elapsed: 8.4 s
Timeout: 30 s
No user action required
```

## 12.2 User-action object

The Doctor has a first-class `UserAction` model:

```text
UserAction
  required
  severity
  title
  explanation
  why
  instructions[]
  availableActions[]
  timeout/expiry if applicable
```

“No action required” is itself a visible valid state.

## 12.3 Disabled actions explain why

A disabled repair or operation must expose the reason.

Example:

```text
Automatic repair unavailable.
The failure is isolated, but no qualified deterministic repair exists for this signature.
Nothing has been changed.
```

# 13. Evidence Model

Evidence is the factual foundation of the Doctor.

Each evidence item should contain:

```text
EvidenceItem
  evidenceId
  checkId
  timestamp
  source
  category
  title
  humanSummary
  technicalSummary
  rawValue / structuredPayload
  errorDomain
  nativeErrorCode
  nativeErrorText
  provenance
  sensitivityClass
```

## 13.1 Evidence layers

The UI shall present evidence in layers:

1. Human explanation
2. Technical summary
3. Raw/doctorate-level detail

## 13.2 Provenance

Evidence must distinguish sources such as:

- HidHide control device;
- registry;
- file metadata;
- Authenticode;
- Driver Store;
- Service Control Manager;
- SetupAPI/ConfigMgr;
- PnP;
- Windows Event Log;
- WER;
- SetupAPI logs;
- HOTAS BF6 context;
- Doctor’s own verification probes.

## 13.3 Evidence immutability

Evidence produced during a session should be append-only. Later analysis can supersede a diagnosis, but must not retroactively rewrite what was observed.

# 14. Diagnostic Engine Architecture

The diagnostic engine executes the catalog as a dependency-aware plan.

Required top-level phases:

1. System Environment
2. HidHide Installation Discovery
3. Package Integrity
4. Kernel Driver State
5. Protocol / API Health
6. Configuration Integrity
7. Device Ecosystem
8. Isolation Verification
9. Windows Evidence
10. Consistency Analysis
11. Diagnosis
12. Repair Recommendation

The exact catalog is governed by the Diagnostic Catalog document.

## 14.1 Failure isolation

One failed probe must not abort unrelated probes.

A broken whitelist query must not prevent active-state, blacklist, driver-version, package, device-enumeration, or Windows-evidence checks from running when they can safely run independently.

## 14.2 Timeouts

Every external/system operation must use a defined bounded timeout or cancellation strategy.

No probe may hang the entire UI indefinitely.

# 15. HidHide Protocol Probing

Current upstream HidHide exposes a WDM control device under `\\.\HidHide` and also an interface GUID. Its developer documentation defines independent IOCTL operations for persistent application/device lists, active state, inverse state, and newer process-lifetime session blacklist features.

The Doctor should build an **independent protocol client** rather than depend exclusively on `HidHideCLI.exe`.

## 15.1 Probe independently

At minimum, probe independently when supported:

- open control device;
- Get Active;
- Get Inverse;
- Get Whitelist;
- Get Blacklist;
- Set operations only during authorized repair;
- Add/Clear Session Blacklist capability detection where available.

## 15.2 Capability detection

Do not assume a feature exists based solely on client executable version.

The Doctor must identify actual driver/control-interface capability.

## 15.3 Native errors

Capture:

- Win32 error;
- NTSTATUS where available;
- operation;
- buffer/query phase;
- timing;
- driver/package context.

# 16. Installation / Package Discovery

Discover all plausible HidHide installation evidence, including:

- official dependency registry keys when present;
- installation root;
- installed-product records;
- user-visible binaries;
- driver binaries;
- file versions;
- product versions;
- file hashes;
- signatures;
- Driver Store packages;
- duplicate/stale packages;
- architecture;
- expected/current install layout;
- uninstall/update state;
- reboot-required state.

The Doctor shall not assume “client exists” means “driver is correct.”

# 17. Driver and Windows Evidence

Inspect:

- root HidHide device node;
- driver service state;
- loaded/running state;
- binary version;
- package version;
- loaded-vs-on-disk mismatch;
- filter registration;
- HID/XUSB-related class/filter state where relevant;
- device interface/control endpoint;
- installation timestamps;
- recent setup/remove events;
- Windows Error Reporting evidence;
- relevant Event Log entries;
- relevant SetupAPI logs;
- restart/pending replacement indicators.

Windows evidence is diagnostic evidence, not permission to modify unrelated driver infrastructure.

# 18. Device Ecosystem Diagnostics

Device enumeration must not be an all-or-nothing operation.

The Doctor shall:

- enumerate physical and virtual gaming devices;
- identify HID and XInput/XUSB-relevant devices where feasible;
- inspect each device independently;
- survive malformed/broken devices;
- record which device caused which failure;
- identify absent/phantom/stale entries;
- detect duplicate/ambiguous identities;
- identify device instance paths used by HidHide;
- correlate persistent hidden-device entries with current PnP state;
- distinguish physical controllers from virtual outputs where confidently known;
- expose uncertainty rather than guess.

A single broken device must never crash the Doctor.

# 19. Isolation Verification

Isolation is not one Boolean.

The Doctor should separately reason about:

- HidHide globally active;
- application whitelist/inverse semantics;
- HOTAS BF6/feeder exemption;
- physical device hide state;
- persistent vs session hide state;
- virtual output visibility;
- configuration consistency;
- effective current device access where verifiable.

Verification should avoid disruptive configuration changes unless the user explicitly authorizes a controlled test.

# 20. Diagnosis / Classification Engine

Diagnosis is deterministic evidence reasoning.

Each diagnosis should contain:

```text
Diagnosis
  diagnosisId
  title
  severity
  confidenceBand
  confidenceScore
  supportingEvidence[]
  contradictingEvidence[]
  affectedCapabilities[]
  userImpact
  repairability
  candidateRepairs[]
  knownIssueSignature
```

## 20.1 Confidence bands

Minimum bands:

- Confirmed
- Very High
- High
- Moderate
- Uncertain

Automatic invasive repair must require an adequate evidence/confidence policy defined in the Repair Safety Model.

## 20.2 Multiple diagnoses

The Doctor may report primary and secondary diagnoses.

Do not force every system into exactly one root cause when evidence supports independent failures.

# 21. Knowledge Base

The Doctor shall have a versioned **HidHide Knowledge Base** containing signatures and remediation metadata.

Example signature:

```text
Signature:
  Windows 11 25H2
  HidHide client 1.5.230.0
  GetWhitelist -> ERROR_INVALID_PARAMETER (0x57)

Classification:
  HidHide control-interface compatibility failure

Repair:
  none qualified by default unless a tested remedy is added

Evidence confidence:
  high
```

Knowledge-base entries may reference known upstream issues, but shall never convert an internet anecdote directly into an automatic repair.

A KB rule must specify:

- signature;
- required evidence;
- excluded/contradicting evidence;
- diagnosis;
- confidence;
- safe repair options;
- applicable versions;
- retirement/supersession information.

# 22. Repair Planning Architecture

Repairs are generated from confirmed findings/diagnoses.

Repair screen is not a generic dangerous toolbox.

Each repair plan contains:

```text
RepairPlan
  planId
  diagnosesAddressed[]
  riskClass
  operations[]
  expectedChanges[]
  expectedDuration
  elevationRequired
  rebootRequired / maybe
  userActions[]
  backupPlan
  rollbackPlan
  verificationPlan
  preconditions[]
  abortConditions[]
```

Before authorization the user must see what will change.

# 23. Repair Execution and Elevated Helper

Repair risk levels:

- **R0 Observe** - no modification.
- **R1 Configuration** - narrow HidHide/HOTAS configuration changes.
- **R2 Component** - service/filter/component consistency repair.
- **R3 Package Repair** - reinstall currently approved package.
- **R4 Approved Upgrade** - move to a specifically HOTAS-qualified package/version.
- **R5 Recovery** - remove damaged installation state, install known-good package, restore and fully verify.

The detailed gates are governed by the Repair Safety Model.

## 23.1 No arbitrary command execution

The elevated helper shall use typed operations, not an unrestricted command string supplied by UI or IPC.

## 23.2 Structured progress

Every repair operation reports:

- operation ID;
- current substep;
- percent where calculable;
- change made;
- success/failure;
- rollback state;
- user action.

# 24. Backup, Rollback, and Transaction Journal

Before any repair that can materially alter system state, capture a recovery snapshot appropriate to the risk level.

Potential snapshot evidence:

- HidHide whitelist;
- blacklist;
- active/inverse state;
- registry-backed configuration;
- package identities;
- driver versions;
- Driver Store package references;
- relevant service/filter state;
- HOTAS BF6 integration state.

The Doctor maintains a durable repair transaction journal containing:

- authorized plan;
- each operation;
- before value;
- after value;
- operation result;
- reboot boundary;
- verification result;
- rollback result if used.

# 25. Reboot Continuation

Reboot is a first-class repair boundary.

Before reboot the UI shall show:

- overall repair progress;
- completed steps;
- exact reason restart is required;
- steps that will run afterward;
- whether rollback is available;
- Restart Now / Restart Later.

A durable continuation token/session journal is written before reboot.

After restart the Doctor resumes:

```text
Continuing HidHide repair from before restart
Step 9 of 12 - verifying replacement driver
```

It must not pretend the repair completed merely because an installer returned `ERROR_SUCCESS_REBOOT_REQUIRED`.

# 26. Provider / Version Management

v1.1 Doctor supports official HidHide installations first.

The architecture must allow future provider identities such as a HOTAS-qualified fork without entangling provider with diagnostic logic.

Represent at least:

- provider;
- installed client version;
- loaded driver version;
- package version;
- protocol capability set;
- trust/signature state;
- approval channel/status.

## 26.1 Approved packages only

Automatic package repair/upgrade may use only:

- known source;
- pinned immutable version/hash;
- validated signature/trust policy;
- Doctor-approved compatibility matrix;
- explicit qualification evidence.

Never auto-install “whatever `master` is today.”

# 27. Reports, Export, Privacy, and Support Bundles

Every completed session can produce a report.

Minimum summary:

- session ID;
- Doctor version;
- Windows version/build;
- HidHide provider/versions;
- checks total/completed;
- findings;
- diagnoses/confidence;
- repairs offered/performed;
- reboot status;
- final verification.

## 27.1 Sanitized export

Support bundles must classify sensitive data and avoid blindly exporting:

- unrelated personal paths;
- unrelated application inventory;
- usernames where unnecessary;
- arbitrary registry content;
- unrelated event logs.

User can preview what is included.

Machine identifiers should be minimized/redacted unless technically necessary and explicitly shown.

# 28. Failure Handling and Degraded Operation

A successful Doctor run does not require repair success.

Valid terminal outcomes include:

### Healthy

No material issues.

### Diagnosed, repair available

Clear plan available.

### Diagnosed, repair unavailable

Cause isolated; no qualified deterministic repair currently exists.

### Partially diagnosed

Evidence narrowed the problem but uncertainty remains.

### Failed safely

A required diagnostic subsystem failed, but no unsafe mutation occurred.

No terminal state may falsely imply health simply because the Doctor itself did not crash.

# 29. Performance and Responsiveness

The Doctor is not HOTPATH, but it must still feel excellent.

Requirements:

- UI thread never runs blocking driver/package/event-log scans;
- bounded worker execution;
- cancellation for read-only diagnosis where safe;
- no unbounded file/event-log searches;
- live evidence batching;
- progress updates coalesced to sensible UI rates;
- no decorative animation consuming significant CPU/GPU;
- idle Doctor should be near-idle;
- repair helper exits after its authorized work.

# 30. Accessibility and Interaction Quality

- Meaning is never encoded by color alone.
- Keyboard navigation is supported.
- Focus and selected state are distinct.
- Reduced motion is respected.
- Text is selectable/copyable where useful.
- Error codes can be copied.
- User Action is visible with text/icon/severity, not only color.
- High-DPI behavior is qualified.
- Pane resizing has accessible alternatives.
- Focus View remains a complete accessible alternative to multi-pane layout.

# 31. Security Model

The Doctor manipulates driver configuration and therefore requires a security model, not merely good intentions.

Requirements:

- least privilege by default;
- narrow elevation;
- authenticated/local IPC;
- no unrestricted elevated shell;
- repair plan integrity check before execution;
- immutable approved package metadata;
- signature/hash verification;
- safe temporary-file handling;
- no network download inside elevated helper;
- no test-signing/security-disable repair paths for normal users;
- explicit audit log of privileged changes;
- reject repair if critical preconditions changed between planning and commit.

# 32. Testing and Qualification

Test families must include:

| Family | Purpose |
|---|---|
| Catalog tests | Every check has stable ID, prerequisites, status semantics |
| Probe tests | Independent failure isolation |
| Fixture tests | Known healthy/broken system snapshots |
| Fault injection | Access denied, invalid parameter, timeout, missing files, corrupt data |
| Device enumeration | Broken/absent/phantom device survival |
| Diagnosis rules | Required/contradicting evidence |
| Repair plan tests | No unsafe plan generation |
| Elevated helper | Typed-command authorization and rejection |
| Rollback | Restore prior state after injected failure |
| Reboot continuation | Resume/verify across boundary |
| UI transparency | Current step/progress/findings/action always accurate |
| Focus/Command Center equivalence | Same session state |
| Report privacy | Redaction/export contract |
| Soak | Repeated scans and long-running UI stability |
| Real-machine qualification | Official HidHide healthy + representative failures |

A synthetic fixture is not enough for final repair qualification.

# 33. Implementation Phases

| Phase | Scope | Exit gate |
|---|---|---|
| 0 - Architecture & evidence freeze | Repo audit, process model, operation/evidence/session schemas, catalog skeleton, safety contracts | No ambiguous ownership or privilege model |
| 1 - Read-only discovery engine | System/install/package/driver/protocol probes | Deep scan completes without mutation |
| 2 - Diagnostic catalog implementation | Full catalog, independent device/API probing, Windows evidence | Catalog coverage target met |
| 3 - Diagnosis engine | Rule/signature model, confidence, correlations | Known fixtures classified correctly |
| 4 - Doctor UI | Focus + Command Center, real progress, findings/action, technical layers | Owner can understand every active operation |
| 5 - Safe configuration repairs | R1 repairs, backups, verification | Fault-injected tests + real-machine qualification |
| 6 - Component/package repairs | R2/R3, narrow elevated helper | Full rollback and package safety gates pass |
| 7 - Reboot continuation & recovery | Durable transaction journal, restart resume, R4/R5 framework | Mid-repair reboot/failure tests green |
| 8 - HOTAS BF6 integration | Launch/deep-link/result handoff | Standalone independence preserved |
| 9 - Torture qualification | OS/version matrix, repeated scans, broken devices, repair fault injection | Release readiness criteria met |

**Implementation discipline:** do not combine all phases into one enormous Codex run. Each phase must leave a reviewable, testable candidate.

# 34. Explicitly Deferred Scope

- Custom HidHide fork.
- Custom signed HidHide release.
- Automatic upstream `master` installation.
- Remote/cloud repair service.
- General Windows driver-repair framework unrelated to HidHide.
- Automatic repair of unrelated controller vendor drivers.
- Kernel debugging automation for ordinary end users.
- Arbitrary registry cleaner behavior.
- Disabling security features to force compatibility.

# 35. Feature Completion Criteria

HidHide Doctor v1 is complete only when:

1. It is a separate independently launchable executable.
2. HOTAS BF6 can launch it from at least Devices and Diagnostics/App Health.
3. It runs non-elevated for ordinary diagnosis.
4. Privileged repairs use a narrow elevated helper.
5. Every significant operation exposes current phase, step, overall progress, current-step progress, findings-so-far, and user action.
6. Progress is work-based rather than fake time-based.
7. Focus View and Command Center View show the same Doctor Session and can switch live.
8. Command Center panes are independently scrollable and user-resizable.
9. User layout/density preferences persist.
10. Diagnostic probes fail independently.
11. The Doctor can inspect the HidHide control interface directly rather than depending exclusively on HidHide CLI.
12. HidHide installation, package, driver, configuration, device, isolation, and Windows evidence are separately assessed.
13. A broken gaming device cannot crash the Doctor.
14. Loaded driver/client/package mismatches are detected.
15. Diagnoses include evidence and confidence.
16. Known-issue signatures are versioned and do not automatically imply repair.
17. Repair plans are generated from diagnoses, not exposed as random dangerous tools.
18. Every repair has preconditions and a verification plan.
19. Material repairs have backup/rollback.
20. Reboot-required repairs resume and verify afterward.
21. No arbitrary upstream build is installed automatically.
22. No ordinary repair disables Windows security/driver-signing protections.
23. The Doctor can finish with “diagnosed, no qualified repair” without pretending failure.
24. Exported reports are sanitized and previewable.
25. UI never sits unexplained during a long wait.
26. Disabled/unavailable actions explain why.
27. Automatic repair never assumes exclusive ownership of unrelated HidHide configuration.
28. Final verification proves actual post-repair health instead of trusting installer exit codes.
29. Failure injection demonstrates safe abort/rollback behavior.
30. Real-machine owner review confirms the tool is both easy enough for Chewy and detailed enough for an engineer to defend at a whiteboard for an unreasonable amount of time.

# Appendix A - Representative Workflows

## A.1 Healthy Scan

```text
Open Doctor
↓
Build 124-check plan
↓
System / package / driver / API / devices / isolation
↓
124/124 complete
↓
Healthy
↓
No action required
```

## A.2 Client/Driver Version Mismatch

```text
Client: 1.5.230.0
Package: newer package staged
Loaded driver: older
Pending replacement/restart: yes
↓
Diagnosis: incomplete driver replacement
↓
Repair plan: preserve config, finish approved package repair, reboot, resume
↓
Post-boot loaded version verified
↓
Isolation requalified
```

## A.3 One Protocol Operation Broken

```text
Control device open             PASS
GetActive                       PASS
GetInverse                      PASS
GetWhitelist                    FAIL 0x57
GetBlacklist                    PASS
Device enumeration              PASS
↓
Do not label whole driver "missing"
↓
Classify exact control-interface failure
↓
Known-signature correlation
↓
Repair only if qualified remedy exists
```

## A.4 Broken Device Enumeration

```text
Global gaming enumeration shows failure
↓
Per-device enumeration
↓
Identify exact device instance causing failure
↓
Continue all unrelated checks
↓
Report device and evidence
↓
Offer only safe proven remediation
```

## A.5 Repair Needs Reboot

```text
Repair 76% complete
Driver replacement staged
↓
RESTART REQUIRED
↓
persist repair transaction
↓
restart
↓
Doctor resumes automatically
↓
verify loaded driver
↓
restore/verify config
↓
isolation qualification
↓
Repair Complete
```

# Appendix B - Technical Baseline and Upstream Assumptions

This design freeze uses the upstream state visible in September 2026 as a technical baseline, not as a permanent hard-coded truth.

Key baseline facts:

- HidHide is an open-source Windows kernel-mode device filter with a configuration client and CLI.
- Current upstream documentation exposes a WDM control device at `\\.\HidHide` and an interface GUID for programmatic integration.
- The current developer contract documents get/set operations for whitelist, blacklist, active state, inverse mode, and newer process-lifetime session blacklist support.
- Persistent configuration is represented in the HidHide service’s `Parameters` registry data in current source.
- Upstream explicitly advises third-party integrators to be conservative and not assume exclusive ownership of a user’s configuration.
- The latest signed public release visible at design-freeze time is v1.5.230.0, while upstream source contains newer work and fixes.
- Upstream issue #215 documents a current Windows 11 25H2 failure signature where `GetWhitelist` returns `ERROR_INVALID_PARAMETER`.
- Upstream issue #217 documents the release/source gap and examples of client/driver version mismatch after replacement pending reboot.
- Any future package/fork work remains separate from HidHide Doctor v1 and must follow Windows driver-signing requirements.

Authoritative upstream references for implementation review:

- `nefarius/HidHide` README
- `nefarius/HidHide` DEVELOPER.md
- `nefarius/HidHide` INSTALL_LAYOUT.md
- `nefarius/HidHide` BUILD_AND_RELEASE.md
- `HidHide/src/Logic.h`
- `HidHideIoctlContract.h`
- upstream issue #215
- upstream issue #217

Do not hard-code assumptions from this appendix when the installed system can be directly inspected.

# Appendix C - Requirement Index

| Prefix | Area |
|---|---|
| HD-GOV | Governance |
| HD-SCOPE | Product scope |
| HD-ARCH | Architecture |
| HD-INTEG | HOTAS BF6 integration |
| HD-PRIV | Privilege/elevation |
| HD-SESSION | Session model |
| HD-PROG | Progress |
| HD-UI | UI/information architecture |
| HD-EVID | Evidence |
| HD-DIAG | Diagnostic engine |
| HD-API | HidHide protocol |
| HD-PKG | Installation/package |
| HD-DRV | Driver/Windows |
| HD-DEV | Device ecosystem |
| HD-ISO | Isolation |
| HD-DX | Diagnosis |
| HD-KB | Knowledge base |
| HD-REPAIR | Repair planning/execution |
| HD-ROLL | Backup/rollback |
| HD-REBOOT | Restart continuation |
| HD-REPORT | Reports/privacy |
| HD-PERF | Performance |
| HD-A11Y | Accessibility |
| HD-SEC | Security |
| HD-TEST | Testing |

# Appendix D - Glossary

| Term | Meaning |
|---|---|
| Doctor Session | One complete diagnostic/repair evidence timeline |
| Check | One cataloged diagnostic observation operation |
| Evidence | Immutable observed fact/result |
| Finding | User-meaningful abnormal or notable result derived from evidence |
| Diagnosis | Evidence-supported explanation/classification |
| Repair Plan | Predeclared typed set of changes intended to address diagnoses |
| Repair Transaction | Durable execution record of an authorized plan |
| Verification | Post-action checks proving expected health |
| Focus View | Vertically organized complete UI layout |
| Command Center View | Side-by-side four-pane complete UI layout |
| User Action | Explicit representation of whether/what the user must do |
| Qualified repair | Deterministic remediation that passed defined safety/testing gates |


# External Technical Reference Snapshot

These documents were written against a September 2026 upstream snapshot. Before implementation, Codex/engineering should re-check upstream because HidHide is active source code even when signed releases lag.

- HidHide upstream repository: https://github.com/nefarius/HidHide
- Developer guide: https://github.com/nefarius/HidHide/blob/master/DEVELOPER.md
- Install layout: https://github.com/nefarius/HidHide/blob/master/INSTALL_LAYOUT.md
- Build/release notes: https://github.com/nefarius/HidHide/blob/master/BUILD_AND_RELEASE.md
- Driver logic/config constants: https://github.com/nefarius/HidHide/blob/master/HidHide/src/Logic.h
- Current public releases: https://github.com/nefarius/HidHide/releases
- Issue #215 (`GetWhitelist` / `ERROR_INVALID_PARAMETER` on Windows 11 25H2): https://github.com/nefarius/HidHide/issues/215
- Issue #217 (signed release/source gap and version-mismatch discussion): https://github.com/nefarius/HidHide/issues/217
- Microsoft driver-signing overview: https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing


# v1.1 Amendment — Platform Portability and Machine Independence

> **Non-negotiable product requirement:** HidHide Doctor is not a machine-specific companion utility. It SHALL behave as a portable Windows diagnostic and repair product that can be installed and run on other users' supported Windows systems without assuming the owner's hardware, paths, account, controller inventory, HOTAS BF6 installation state, or prior HidHide configuration.

## Supported-platform contract

The Doctor's primary supported deployment target SHALL be:

- Windows 10 and Windows 11 systems on which HidHide itself is supportable;
- Intel/AMD x64;
- Windows on ARM64 where the Doctor and applicable HidHide components are available for that architecture.

The Doctor SHALL detect OS version, build, edition, architecture, WOW64/emulation state where relevant, and package architecture before running architecture-sensitive diagnostics or repairs.

The Doctor SHALL NOT assume that an arbitrary Windows machine matches the development machine.

The Doctor SHOULD still launch into a safe diagnostic/compatibility state when HidHide is absent, corrupted, partially installed, wrong-architecture, or otherwise unusable.

Unsupported operating-system or architecture combinations SHALL receive an explicit compatibility finding rather than an unexplained launch failure or an attempted repair.

The portability goal does not require pretending that HidHide supports operating systems that upstream HidHide itself does not support. The Doctor's UI and evidence model MAY explain that Windows 7/8 or unsupported 32-bit configurations are outside the supported repair contract rather than attempting dangerous compatibility workarounds.

## HD-PORT-001 SHALL — No machine-specific paths

No diagnostic or repair logic may depend on hard-coded development-machine paths, usernames, drive letters, repository locations, controller names, GUIDs, or installation directories.

Paths SHALL be resolved from Windows/package/registry/application-discovery APIs and verified before use.

## HD-PORT-002 SHALL — No owner-specific hardware assumptions

The Doctor SHALL support zero, one, or many gaming-input devices from arbitrary vendors.

It SHALL NOT assume:
- Thrustmaster;
- Virpil;
- VKB;
- MFG;
- vJoy device count;
- a specific USB topology;
- a specific controller GUID;
- a specific HOTAS BF6 Device Rig.

Any device-specific knowledge must be evidence-driven or represented in an explicit vendor/device knowledge rule.

## HD-PORT-003 SHALL — HOTAS BF6 is optional

HidHide Doctor SHALL be independently launchable and useful even when HOTAS BF6:
- is not installed;
- is not running;
- is broken;
- has never been configured.

HOTAS BF6 integration is an additional launch/deep-link/context channel, not a runtime dependency of the Doctor.

## HD-PORT-004 SHALL — Architecture-aware binaries and packages

The build/release pipeline SHALL produce and qualify the correct Doctor binaries for every supported CPU architecture.

Architecture-sensitive repair operations SHALL reject mismatched packages before mutation.

The Doctor SHALL never install an x64 driver/package on ARM64 or an ARM64 package on x64.

## HD-PORT-005 SHALL — Dynamic capability detection

The Doctor SHALL detect what the current machine actually supports instead of assuming a fixed feature set.

Examples include:
- HidHide protocol capabilities;
- session-blacklist support;
- installed client/driver generation;
- available Windows APIs;
- Event Log/WER availability;
- repair-helper architecture;
- package format and provider.

Unsupported probes SHALL become `Not Applicable` or explicit capability findings, not fatal errors.

## HD-PORT-006 SHALL — Windows-build variability is first-class

Diagnostics and repair classification SHALL account for OS-build differences.

A finding may be scoped by:
- Windows major/minor;
- feature release;
- build number;
- revision;
- architecture;
- security configuration.

Knowledge-base signatures SHALL declare the environment in which they are valid.

## HD-PORT-007 SHALL — Locale and regional independence

Core diagnostics SHALL not parse localized human-readable command output when a structured Windows API, registry value, WMI/CIM property, SetupAPI property, Event Log field, or binary protocol result is available.

The Doctor SHALL not depend on English service-status text, decimal formatting, date formatting, or localized error descriptions for correctness.

Raw numeric codes and structured identifiers remain authoritative.

## HD-PORT-008 SHALL — Least-assumption privilege model

The Doctor SHALL launch unelevated by default on every supported system.

A diagnostic check that lacks permission SHALL report `Permission Limited` with evidence and continue other checks where possible.

Elevation is requested only for a specific qualified repair or privileged diagnostic operation that genuinely requires it.

## HD-PORT-009 SHALL — No dependency on interactive shell tools

PowerShell, cmd.exe, pnputil.exe, sc.exe, reg.exe, or other command-line utilities MAY be used only where justified and version-tolerant.

Where practical, the Doctor SHOULD use Windows APIs directly.

If an external Windows utility is required, its presence/version/result must be validated and its output must not be assumed identical across systems.

## HD-PORT-010 SHALL — Packaging independence

Installation discovery SHALL tolerate:
- normal MSI installs;
- manual/portable HidHide component placement where supported;
- partially completed upgrades;
- stale Driver Store packages;
- missing client with present driver;
- present client with missing driver;
- multiple historical packages.

The Doctor must diagnose the actual state rather than assuming one installer layout.

## HD-PORT-011 SHALL — Hardware enumeration fault isolation

One malformed, disconnected, inaccessible, vendor-broken, or partially enumerated HID device SHALL NOT terminate the full Doctor session.

Per-device enumeration and evidence collection must isolate failures and continue.

## HD-PORT-012 SHALL — Reproducible machine fingerprint for diagnostics

Every Doctor report SHALL include a privacy-reviewed environment fingerprint sufficient to reproduce or classify platform-specific issues, including:
- Windows edition/version/build/revision;
- CPU architecture;
- Doctor architecture;
- HidHide package/client/driver versions;
- applicable protocol capabilities;
- repair-helper architecture;
- relevant security/driver state.

It SHALL not include personally identifying data merely for uniqueness.

## HD-PORT-013 SHALL — Clean-machine qualification

Release qualification SHALL include clean Windows virtual machines or equivalent clean hosts, not only the developer workstation.

At minimum, qualification SHALL cover:
- current supported Windows 10 x64 baseline if still in product support;
- current supported Windows 11 x64 baseline;
- at least one current Windows 11 x64 feature release different from the primary development machine;
- Windows 11 ARM64 once ARM64 is declared production-supported;
- HidHide absent;
- clean HidHide install;
- damaged/partial HidHide fixtures;
- upgrade/mismatch fixtures.

## HD-PORT-014 SHALL — Hardware-diversity qualification

Before declaring broad production readiness, test with representative controllers from multiple vendors/classes and with synthetic/fault-injected device fixtures.

The system's correctness may not depend on one owner's HOTAS.

## HD-PORT-015 SHALL — Fresh-user qualification

A Windows account that has never run HOTAS BF6 or HidHide Doctor must be able to launch the Doctor and receive coherent results without pre-created settings, cache files, or owner-specific registry state.

## HD-PORT-016 SHALL — Read-only portability before repair portability

Every newly supported Windows build/architecture must first pass full read-only diagnosis qualification.

Automatic repair support for that environment may be enabled only after the Repair Safety Model's evidence and qualification requirements are met.

A platform may therefore be:
- diagnosis-supported;
- repair-supported;
- package-repair-supported;
- recovery-supported;

as separate capability levels.

## HD-PORT-017 SHALL — Unknown future Windows builds fail safe

When the Doctor encounters a newer unqualified Windows build, it SHALL NOT pretend that invasive repair behavior has already been proven safe.

Read-only diagnostics SHOULD continue where compatible.

Repairs with environment-sensitive risk SHALL be blocked or downgraded until their preconditions are proven.

## HD-PORT-018 SHALL — Portable UI behavior

Focus View and Command Center View SHALL remain usable across:
- common laptop resolutions;
- high-DPI scaling;
- multiple monitor scales;
- portrait/landscape constraints where practical;
- x64 and ARM64 builds.

The Command Center may enforce a documented minimum useful width and offer Focus View below it, but shall not silently become unusable.

## HD-PORT-019 SHALL — Build provenance

Every distributed Doctor binary and repair helper SHALL identify:
- version;
- architecture;
- build provenance;
- source revision;
- release channel.

Diagnostic reports SHALL include these identifiers.

## HD-PORT-020 SHALL — Machine-independence release gate

A HidHide Doctor release SHALL NOT be called production-ready solely because it works on the owner's PC.

The release gate requires passing the defined OS/architecture/clean-machine matrix and demonstrating that no machine-specific assumptions remain in diagnostic or repair code.
