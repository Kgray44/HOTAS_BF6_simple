# HidHide Doctor Phase 5 integration and qualification record

## Basis and scope

- Accepted Phase 4 SHA: `03c7efdf52c69e9edb78747e38780360b5969cde`.
- Phase 5 branch: `codex/hidhide-doctor-phase5-integration-qualification`.
- Worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase5-integration-qualification`.
- Current accepted product-candidate SHA: `6734b773a5f2a70764ef029b705b804625cea154`.
- PR #68's current-head Documentation Check and HOTAS BF6 CI both reached
  terminal `SUCCESS` for that SHA.
- This record is evidence-only. It does not record an owner-authorized repair,
  tag, merge, or public release.

## Product integration

HOTAS exposes **Open HidHide Doctor** from Devices, Flight Deck Diagnostics,
and App Health. It discovers `HidHide Doctor.exe` beside the running mapper,
uses `QProcess::startDetached`, does not elevate, and fails safely when that
paired file is absent. The mapping worker has no new dependency: all launch and
result work is on the UI/control plane.

The normal interactive Doctor window owns a same-user local endpoint. A second
normal launch forwards only its 32-character integration token to that existing
window, focuses it, and exits without creating another workstation. Headless,
fixture, and startup-smoke invocations deliberately remain independent for
testability; the endpoint cannot carry a repair command or unbounded payload.

The v1 integration protocol passes a 32-character session token only. The
payload is a one-time, at-most-16 KiB JSON document under the current user's
local application data. It has a schema/version, bounded fields, an absolute
executable-path hint, at most 16 controller IDs, and no operation, helper,
command, service, registry, or arbitrary path field. Doctor removes it after
reading. Context is displayed as a hint and cannot replace native evidence.
Malformed, oversized, reused, or unsupported context is rejected.

Doctor returns a bounded local status (`Doctor opened` or `Diagnosis complete`)
only. HOTAS optionally consumes that result once and refreshes ordinary,
low-frequency HidHide readiness. It does not synchronously wait, poll the
Doctor, invoke repair, or enter the DirectInput → MappingWorker → vJoy path.
An invoking-version mismatch is visibly reported and blocks Lab repair mode.

## Live-review reporting and presentation addendum

The Phase 5 live-review addendum adds a single `DoctorReportComposer` that
projects `DoctorSession` into Markdown, JSON, plain text, or a bounded local
diagnostic-bundle directory. The default is **Safe to Share**: evidence marked
potentially identifying or sensitive is replaced with a redaction marker, and
the JSON redaction manifest explicitly records included, redacted, and excluded
categories. Local / Unredacted is a deliberate opt-in. Copy actions share the
same composer as export; no pane text is treated as the report source of truth.
The bundle writer uses atomic per-file writes, a per-file 8 MiB cap, and has no
network/upload capability.

The export command remains asynchronous, but it is not reported as successful
until the atomic writer has committed and verified the selected destination.
The completion state names that native local path and explicitly says that
nothing was uploaded. Missing destination folders, partial writes, failed
commits, and a post-commit path/size mismatch are surfaced as safe export
failures with the affected path. The domain export test uses a real Qt event
loop and verifies the requested Markdown file, nonzero contents, cleared busy
state, and exact completion path. The owner also used the native file picker to
write a Local / Unredacted report to Downloads, opened `test_report2.md`, and
confirmed that it was present and looked good. That is direct evidence for the
desktop destination, commit, and reopen path; it is not a Safe to Share privacy
review.

The Qt Quick file and folder dialogs now hand their selected `QUrl` values to
typed model invokables. Local-path conversion occurs only in C++, not through
a JavaScript URL method; a non-local URL reports a visible safe failure. The
domain contract asserts both dialog handoffs, rejects the former invalid QML
conversion, asserts the visible `Exporting…` and completion-status bindings,
and exercises the same `file:///` local URL type through a real export. The
configured 17-test CTest suite passed again after this correction.

The owner-exported report exposed a separate text-integrity issue: Windows
version-resource strings could retain their terminating NUL. The provider now
preserves the API's character count and truncates only at that terminator; the
composer also removes NUL characters defensively from report text. A report
fixture injects NUL into both HidHide version values and asserts that neither
Markdown nor JSON contains one. A fresh staged, read-only owner-machine report
contained zero NUL bytes while retaining the full `1.5.230.0` client and
`1.4.181.0` driver versions. This was diagnostic observation only; it invoked
no repair or elevation.

The bounded deterministic parser campaign uses seed `0x5AFEF00D` and 64
malformed inputs each for launch/result handoff, repair journals, helper IPC
frames, package metadata, and Safe-to-Share report/evidence composition. The
campaign asserts rejection (where input is untrusted), valid bounded JSON for
reports, no NUL bytes, and no test hang. It runs only against temporary
fixture/test data; it cannot invoke a mutable provider, helper execution, or
live HidHide operation.

The Command Center no longer continuously binds persisted pane fractions back
into a live `SplitView`. Saved fractions are restored once, a user drag owns
the geometry until release, and the released geometry alone is persisted. A
layout-reset epoch is the only explicit re-restore path. The unnecessary
Diagnostic Plan and Findings horizontal scrollbars were removed; their custom
thumb/corner artifacts were the reported mystery squares. Activity and
Evidence Inspector now share a deliberate bottom dock (side-by-side when wide,
stacked when narrow), while Focus retains an inline inspector. Density changes
internal typography and spacing at 112%, 100%, or 92% without changing the
outer workspace geometry.

Displayed scan progress is presentation-only at a 33 ms cadence. Actual engine
progress remains authoritative; a bounded estimate never completes an active
step or session early. Recorded engine/session timings are exported with an
explicit note that they are not a user-perceived latency measurement.

## Distribution and provenance

The stage script requires `HidHide Doctor.exe` and `HidHideDoctorRepair.exe`,
deploys Qt for both mapper and Doctor, checks all three component file versions
against `HOTAS_VERSION`, and produces `HidHideDoctor-ComponentManifest.json`
plus a SHA-256 checksum. Doctor/helper now carry dedicated version resources.
The staged `2.6.2` candidate had 1,387 manifest entries. All three application
binaries reported `NotSigned`; no signing certificate was available or invented.
Helper protocol is v2 and integration protocol is v1.

## Qualification evidence

- Built mapper, Doctor, helper, domain tests, and layout tests with MSVC 19.44
  and Qt 6.8.3.
- `hidhide_doctor_domain_tests`, standalone startup smoke, and layout tests
  passed (including the bounded one-time integration context/result test, safe
  report/redaction/bundle coverage, and a 10-second post-drag no-fightback
  layout check).
- The configured 17-test CTest suite passed 17/17 immediately before the
  final redundant Findings horizontal-scrollbar deletion; the rebuilt final
  candidate then passed the focused domain, deep-repair, standalone-startup,
  and layout set 4/4.
- The complete configured suite was rerun after the live-review work and
  passed 17/17. After the single-instance change, the rebuilt Doctor-focused
  set passed 4/4; both build-directory and staged repeat-launch probes exited
  0 without increasing the one-process count.
- Staged Doctor startup smoke exited 0.
- After the owner desktop export confirmation, the corrected staged package
  passed startup smoke and the focused domain, deep-repair, standalone-startup,
  and layout set 4/4. Its fresh read-only report contained zero NUL bytes and
  retained the complete 1.5.230.0 client / 1.4.181.0 driver version strings.
- The final configured CTest suite passed 17/17 after the report-text
  correction.
- The current `hidhide_doctor_domain_tests` suite passed in 24.41 seconds with
  the bounded deterministic handoff/journal/helper/package/report corpus.
- A staged owner-machine read-only scan exited 0 with 238 checks, 3 findings,
  and 2 diagnoses. It retained the known 1.5.230.0 client / 1.4.181.0 driver
  mismatch, pending-restart evidence, and degraded HID enumeration as separate
  evidence. Its R3 proposal remained explicitly not FieldQualified.
- The owner manually exercised **Devices → Open HidHide Doctor**,
  **Diagnostics/App Health → Open HidHide Doctor**, and
  **Flight Deck → Diagnostics → Open HidHide Doctor** in the fresh staged build.
  Devices was independently observed to launch the paired staged Doctor with
  the bounded integration token; the owner confirmed the App Health and Flight
  Deck routes correctly focused the existing Doctor, with one staged Doctor
  process. This is entry-point evidence only, not full native visual, DPI, accessibility,
  hardware, or repair acceptance.

The platform, hardware, failure injection, privacy/export, repair, reboot,
performance, contention, installer/update/uninstall, native owner review, and
external tester matrix are recorded honestly in the qualification ledger.
Unexecuted entries remain unqualified; no qualification level was promoted.

## Release recommendation

The staged package is a local integration candidate, not a releasable RC.
Do not publish, tag, merge, or authorize R3 from this record. Complete the
unqualified matrix and obtain explicit owner authorization before any live
repair or release decision.

## Required Phase 5 evidence audit

This table makes the requested Phase 5 record categories explicit. **Fixture**
means deterministic coverage only; **Not qualified** is intentionally not a
negative product claim, nor a substituted real-machine result.

| Required category | Current evidence / status |
|---|---|
| 1–3. Phase 4 basis, Phase 5 lineage, final candidate | Phase 4 `03c7efdf52c69e9edb78747e38780360b5969cde`; Phase 5 branch/worktree above; integration baseline `560842c0afff93c9fd387c3c3d566f8e97c51b9a` is recorded on the submitted PR, whose head is the current candidate. |
| 4–8. Architecture, launch points, context, result, independence | Devices, Flight Deck Diagnostics, and App Health invoke the paired standalone Doctor through the bounded v1 local protocol; Doctor independently observes evidence. In the fresh staged owner session, Devices launched the paired Doctor, while Diagnostics/App Health and Flight Deck Diagnostics focused that same window; the staged Doctor process count remained one. Repeated normal launches focus the same-user existing Doctor instead of creating another process. |
| 9–11. Packaging, version compatibility, updater | Stage script requires mapper, Doctor, and helper at one `HOTAS_VERSION`, embeds component version resources, and creates a component manifest. Updater atomicity remains Not qualified. |
| 12. Qualification matrix | The platform, hardware, privilege, locale, display, and recipe matrix is the qualification ledger. |
| 13–15. Clean, healthy, partial HidHide | Deterministic diagnostic fixtures only; clean/healthy/partial real or VM environments are Not qualified. |
| 16. Owner machine | Exact staged candidate `C:\hotas-builds\hidhide-doctor-phase5-rc-6734b77-stage` completed a read-only scan: Windows build 26200 x64, 238 checks, 4 findings, 3 diagnoses, zero NUL bytes, and full 1.5.230.0 client / 1.4.181.0 driver version strings. Version mismatch, restart, HID enumeration, and stale-configuration evidence remained separate. |
| 17. External tester campaign | A safe-to-share diagnostic bundle and tester guide are provided; an external tester has not yet run the candidate. |
| 18–22. R1–R5 qualification | R1–R3 remain LabQualified; R4 is unavailable without an approved newer package; R5 is unavailable without an independently verified rollback package. No FieldQualified promotion occurred. |
| 23. Security audit | Bounded typed context/result storage rejects malformed, oversized, reused, and unsupported inputs; helper protocol safety remains covered by deterministic tests. The new deterministic corpus covers handoff, journals, helper frames, package metadata, and reports. A complete hostile-input audit remains Not qualified. |
| 24. Fuzz results | Seeded `0x5AFEF00D`, 64-input-per-surface deterministic campaign passed without crash, hang, or malformed-input acceptance. Sustained fuzzing and resource-pressure fuzzing remain Not qualified. |
| 25. Privacy/export audit | Default Safe to Share output redacts non-exportable evidence, records included/redacted/excluded items, writes atomically with 8 MiB per-file bounds, and has no upload path. The owner successfully exported, reopened, and visually accepted a Local / Unredacted Downloads report; a corrected staged read-only report has zero NUL bytes. Native owner Safe to Share privacy review remains open. |
| 26. DPI/accessibility | Native-QML fixture coverage includes density and layout behavior; real Windows DPI, screen-reader, and owner visual review are Not qualified. |
| 27–28. Performance and CPU contention | No measured release budgets or contention campaign was run; Not qualified. |
| 29. Hardware diversity | Deterministic controller/provider coverage only; real hardware matrix is Not qualified. |
| 30–31. Full HOTAS and theme regression | Configured local CTest coverage is recorded; five-theme native acceptance and the full practical manual regression matrix are Not qualified. |
| 32. Installer/update/uninstall | Stage package and standalone startup smoke passed; install, update, and uninstall on a test machine are Not qualified. |
| 33–34. RC artifact and manifest | Exact local candidate at `C:\hotas-builds\hidhide-doctor-phase5-rc-6734b77-stage`; 1,387 manifest entries and matching SHA-256 `E5EF328048C4EAEC828BDFD8FE118855AF2E0E746D1F76996226C48F6B573290`. This is not a public release artifact. |
| 35–36. Blockers and limitations | Cross-machine coverage, owner repair/reboot, native review beyond the exercised entry points, accessibility/DPI, contention, installer/update/uninstall, full manual regression, and external testing remain release blockers. Components are `NotSigned`. |
| 37. Release recommendation | **NOT READY**. No merge, tag, public release, elevation, or repair authorization is implied by this record. |
