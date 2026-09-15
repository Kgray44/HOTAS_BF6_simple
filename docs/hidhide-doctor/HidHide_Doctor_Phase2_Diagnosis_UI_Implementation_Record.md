# HidHide Doctor Phase 2 — Diagnosis and UI Implementation Record

> **Status:** Phase 2 diagnostic candidate only. It grants no repair, helper,
> elevation, merge, release, tag, or Phase 3 authority.

## Provenance

| Item | Value |
|---|---|
| Accepted Phase 1 base | `64300bbc5a5c4a061658917be8f1e6f07885d4a1` |
| Phase 2 branch | `codex/hidhide-doctor-phase2-diagnosis-ui` |
| Phase 2 worktree | `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase2-diagnosis-ui` |
| Final Phase 2 SHA | Supplied by the stacked-PR handoff after this source-controlled record is committed. A Git commit cannot contain its own final object ID without changing it. |
| Knowledge/signature version | `HD-KB-2.0`; explicit `HD-KSIG-*` deterministic signatures |

Phase 1 remains the observation boundary:

```text
read-only native observation -> EvidenceRecord -> DoctorCheckResult
                              -> Finding -> Diagnosis
```

No Phase 2 path writes HidHide configuration, the registry, services, drivers,
devices, files, or process state. It adds deterministic interpretation only;
it is outside the DirectInput -> MappingWorker -> vJoy hot path.

## Diagnosis architecture

`DoctorFindingEngine` extracts facts solely from the completed Phase 1
snapshot and evidence IDs. `DoctorKnowledgeEngine` applies 17 versioned,
deterministic rules and returns catalog-backed `HD-X-*` / `HD-KB-*` results,
findings, diagnoses, and explicit correlation evidence. The catalog now has
238 completed records per full scan: 215 Phase 1 observations and 23 Phase 2
derived records (18 cross-correlation plus 5 knowledge checks).

`Finding` is a factual statement with ID, severity/status, affected scope,
human and technical explanations, evidence IDs, confidence band, related
findings, and repairability classification. `Diagnosis` adds a primary or
secondary role, problem family, signature/version, impact, usual resolution,
supporting and contradicting evidence, candidate repair IDs, provenance, and
an explainable confidence score.

Confidence is repeatable rather than heuristic: the rule names required and
supporting evidence, missing expectations, contradictions, score, band, and
reason. Contradictions are retained as evidence and produce the explicit
inconsistent-evidence diagnosis rather than silently winning a conflict.
Primary diagnoses represent the strongest root-cause correlation; related
conditions may appear as secondary diagnoses. Every repairability state is
descriptive only in Phase 2; all repairs are disabled and no repair plan is
created.

Supported families include HidHide absent/client-only/driver-only states,
client-driver version mismatch, incomplete replacement and pending restart,
missing or inaccessible control endpoint, isolated whitelist `0x57` failure,
broad protocol failure, client GUI crash evidence, malformed/broken HID
enumeration, stale configuration, accidentally hidden virtual output,
architecture mismatch, future Windows evidence, permission limitation, and
contradictory or insufficient evidence.

## Forensic workstation UI

The QML workstation uses a graphite, dense technical visual system with amber,
red, blue, and green semantic tokens; no broad gradients or decorative
surface has diagnostic meaning. It presents a diagnostic-session header,
machine/environment strip, live checks/health counts, elapsed/session ID,
and an explicit "repairs disabled" boundary.

- **Command Center** is the wide layout: System Health rail, metric strip,
  four resizable panes (Diagnostic Plan, Current Step, Findings & Diagnoses,
  User Action), an Evidence Inspector drawer, and optional Live Evidence.
- **Focus View** is the same canonical session reorganized as a vertical
  review order; it does not create or mutate an independent copy of results.
- **System Health rail** exposes Installation, Service/Driver, Protocol,
  Configuration, Devices, and Windows evidence with semantic status.
- **Evidence Inspector** opens a selected evidence record and preserves its
  ID, source, provenance, timing, status, and redaction boundary.
- **Activity timeline / Live Evidence** exposes bounded activity events and
  lets a reviewer open the associated record.
- **Density** is Compact, Standard, or Comfortable. Presentation preference,
  pane widths, selected view, and maximized pane use presentation-only
  `QSettings`; no diagnostic result is persisted or changed.
- **Responsive behavior** switches below 1040 logical pixels to Focus View;
  the window minimum is 720 x 560. Panes can maximize and Escape restores
  them. QML logical-pixel layout is DPI-aware by Qt, but native multi-DPI
  visual acceptance remains an owner-review item.
- **Accessibility** includes semantic `Accessible.name` labels for controls,
  keyboard Escape restore, visible text labels, focusable standard controls,
  and semantic status colors. Formal assistive-technology and contrast audits
  have not been performed.

The plan has a fixed 238-item bound and the activity stream is capped at 600
events. Raw evidence remains on demand rather than being eagerly expanded in
the main dashboard. Compile/startup checks prove QML loadability; native
large-data rendering, screen-reader behavior, and high-DPI visual polish are
not substituted with offscreen evidence and require owner review.

## Fixture demonstrations and results

Development-only fixture mode runs the production Phase 2 analysis path,
labels the simulated session, and never touches machine state:

```text
HidHide Doctor.exe --development-fixture "<name>" --headless --report <path>
```

Available labels are `Healthy System`, `GetWhitelist 0x57`, `Broken HID
Device`, `Client Driver Mismatch`, `Pending Restart`, `Partial Install`, and
`Contradictory Evidence`. Each verified run produced `HD-KB-2.0`, 17/17
rules, and 23 Phase 2 records. Expected diagnoses were respectively none,
`HD-DIAG-WHITELIST-API`, `HD-DIAG-DEVICE-ENUMERATION`,
`HD-DIAG-VERSION-MISMATCH`, `HD-DIAG-INCOMPLETE-REPLACEMENT`,
`HD-DIAG-PARTIAL-INSTALL`, and `HD-DIAG-INCONSISTENT-EVIDENCE`.

The deterministic domain matrix additionally covers absence, client-only,
driver-only, access denied, control missing, broad protocol failure, GUI
crash evidence, malformed HID variants, stale configuration, virtual output
hidden, architecture mismatch, future Windows, insufficient evidence, Event
Log permission limitation, cancellation, and contradiction handling. It
checks exact deterministic output and verifies that cancellation still emits
the full 238-result plan. Healthy and insufficient-evidence fixtures are the
false-positive/inconclusive guard cases.

## Real-machine and headless result

On 2026-09-15, a normal non-elevated, headless read-only scan completed in
594 ms with schema 3, no cancellation, 23 Phase 2 records, and the redacted
report option enabled. It recorded three findings:

- `HD-FND-VERSION-MISMATCH`
- `HD-FND-PENDING-RESTART`
- `HD-FND-BROKEN-DEVICE`

It correlated those facts into the primary
`HD-DIAG-INCOMPLETE-REPLACEMENT` and the device-ecosystem
`HD-DIAG-DEVICE-ENUMERATION` diagnosis. This is evidence, not authorization
to repair: the user action remains diagnosis complete with repairs disabled.
No physical HOTAS operational or game-visibility claim is made.

Headless `--report` now writes schema 3. In addition to the Phase 1 snapshot,
it contains `knowledgeEngine`, `findings`, `diagnoses`, and
`activityTimeline`, including evidence links, confidence explanations,
contradictions, repairability, role, signature, and provenance.

Sensitive exports remain redacted by default. Potentially identifying paths,
device/container IDs, protocol values, evidence details, activity detail, and
sensitive event/restart data are replaced with `[redacted]`; safe metadata and
native error codes remain useful for diagnosis.

## Validation and limits

| Check | Result |
|---|---|
| `hidhide_doctor_domain_tests` | Pass: 22 checks, including deterministic diagnosis, cancellation, report, and read-only boundaries. |
| `hidhide_doctor_standalone_startup_smoke` | Pass offscreen after QML compilation. |
| Phase 2 fixture reports | Pass: seven labeled scenarios, expected diagnoses, 17/17 rules, 23 derived records each. |
| Real headless scan | Pass: 594 ms, schema 3, redacted report, no mutation. |
| Mapping hot-path benchmark | All reported scenarios showed `hot_path_allocations=0`; Phase 2 has no mapper/hot-path source changes. |
| Native UI visual / high-DPI / screen-reader acceptance | Not independently established; the Phase 2 forensic workstation must remain open for owner review. |

An unrelated broad `mapping_core_tests` run did not complete within the
interactive validation window and was terminated with its CTest parent. It
did not produce a Phase 2 failure signal and no mapper source was changed,
but it is intentionally not represented as a passing regression.

## Phase 3 entry recommendation

Do not enter Phase 3 automatically. First obtain owner acceptance of the
open Phase 2 workstation and triage the recorded real-machine evidence. If
repair is later authorized, create a separately scoped Phase 3 proposal with
its own preconditions, explicit mutation inventory, confirmation, rollback,
audit, elevation, and validation contracts. This Phase 2 branch contains no
such authority or implementation.
