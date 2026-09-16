# HidHide Doctor Phase 5 integration and qualification record

## Basis and scope

- Accepted Phase 4 SHA: `03c7efdf52c69e9edb78747e38780360b5969cde`.
- Phase 5 branch: `codex/hidhide-doctor-phase5-integration-qualification`.
- Worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase5-integration-qualification`.
- Final SHA: recorded on the submitted Phase 5 candidate PR.
- This record is evidence-only. It does not record an owner-authorized repair,
  tag, merge, or public release.

## Product integration

HOTAS exposes **Open HidHide Doctor** from Devices, Flight Deck Diagnostics,
and App Health. It discovers `HidHide Doctor.exe` beside the running mapper,
uses `QProcess::startDetached`, does not elevate, and fails safely when that
paired file is absent. The mapping worker has no new dependency: all launch and
result work is on the UI/control plane.

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
- Staged Doctor startup smoke exited 0.
- A staged owner-machine read-only scan exited 0 with 238 checks, 3 findings,
  and 2 diagnoses. It retained the known 1.5.230.0 client / 1.4.181.0 driver
  mismatch, pending-restart evidence, and degraded HID enumeration as separate
  evidence. Its R3 proposal remained explicitly not FieldQualified.

The platform, hardware, failure injection, privacy/export, repair, reboot,
performance, contention, installer/update/uninstall, native owner review, and
external tester matrix are recorded honestly in the qualification ledger.
Unexecuted entries remain unqualified; no qualification level was promoted.

## Release recommendation

The staged package is a local integration candidate, not a releasable RC.
Do not publish, tag, merge, or authorize R3 from this record. Complete the
unqualified matrix and obtain explicit owner authorization before any live
repair or release decision.
