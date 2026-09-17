# HidHide Doctor Phase 5 integration and qualification record

## Basis and scope

- Accepted Phase 4 SHA: `03c7efdf52c69e9edb78747e38780360b5969cde`.
- Phase 5 branch: `codex/hidhide-doctor-phase5-integration-qualification`.
- Worktree: `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase5-integration-qualification`.
- Earlier owner-reviewed product implementation SHA:
  `6734b773a5f2a70764ef029b705b804625cea154`.
- Earlier qualification-evidence baseline: `7ee23194e18e8bbd80ce143e11052800669ce19c`.
  Those earlier checks are historical evidence only and are not substituted for
  a later PR head.
- Current crash-fix and bundle-folder product candidate:
  `0846edfc73b282660fcf2d410d9523d5e0545cb1`. Its staged `2.6.2` package is
  `C:\hotas-builds\hidhide-doctor-phase5-rc-0846edf-crashfix-stage`; it has
  1,387 component-manifest entries, a component-manifest SHA-256 of
  `81F3CAEF21504C5456405281BD603B62947690994A86AA38E9DF358F103B590E`, and
  `HidHide Doctor.exe` SHA-256 of
  `9A46EEA359F95505344E620426D39FD637AD71F7AD07B36C98ABB62CB3783DD9`.
  Every staged file was locally checked against that manifest by path, byte
  count, and SHA-256. On exact PR head
  `5ed6f17f6c83f5328db2be8bd2cf6092e0b246c0`, GitHub Actions run
  `35231842975` reached terminal `SUCCESS`: documentation synchronization,
  qualified Qt/toolchain setup, QML lint, Release configuration, compilation
  of the mapper, launcher, and all tests, and the mapped automation, launcher
  readiness, and benchmark test gate all passed. A later PR head still requires
  its own terminal CI result.
- Current product-fix source SHA: `aba8d2358a5899648be7f1b1b7346648a3159163`.
  It preserves native Windows Unicode command-line arguments when Doctor reads
  a report destination, instead of decoding `argv` through the local ANSI code
  page. The resulting immutable candidate is
  `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2`. It has 1,387 component-manifest
  entries, component-manifest SHA-256
  `34A2A1BD50409BCB8D4F81FF5EDFF6249D916B0E42C03074EF2B896AC37C13CC`, and
  `HidHide Doctor.exe` SHA-256
  `6E45DE692B327470EDBF81D5691758E773D68057757DB929CB62E68D9AF9DC1D`.
  Every staged entry was rechecked by path, byte count, and SHA-256. This newer
  source head likewise requires its own terminal CI result.
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

On the crash-fix staged candidate, the owner selected Downloads as a parent
destination for a real Safe-to-Share bundle. Doctor created the timestamped
child directory `HidHideDoctor-Diagnostic-Bundle-20260917-140145111Z`, rather
than flattening files into Downloads. It contained exactly `evidence.json`,
`manifest.json`, `report.json`, `report.md`, and `timeline.json`; all JSON
artifacts parsed, none were empty or contained NUL, and `manifest.json` named
the four report artifacts. The report's redaction manifest recorded 12
included, 714 redacted, and 2 excluded entries. A scoped content check found no
current username, machine name, user-profile, temp, or app-data path. The
composer records automatic network upload as not performed. This qualifies one
owner-created artifact's stated structure and scoped privacy result only; it is
not a universal data-classification audit, evidence of external delivery
behavior, or an RC decision.

Two normal interactive runs of the prior candidate crashed after diagnosis.
Their Windows error events and minidumps showed an access violation in QtCore
while a scan-worker progress callback copied a full `DoctorSession` through a
queued UI connection. The crash-fix candidate no longer sends progress-session
copies across that thread boundary: the scan worker retains the session and
moves one completion payload to the UI thread, which replaces the model session
only after the scan completes. During that scan the presentation may remain in
its preparing state instead of showing granular live session snapshots; this is
a deliberate stability-first tradeoff. `--scan-smoke` runs the normal QML and
read-only scan non-interactively, exiting after final session delivery. The
post-fix domain suite passed in 10.85 seconds; six build-output and five staged
`--scan-smoke` runs exited 0 without a new crash dump. The current staged
interactive process remained responsive through the owner export and the
subsequent artifact inspection. This is focused regression evidence, not a
general native-UI or release qualification.

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

On the exact staged candidate, three process startup-smoke samples completed
in 473, 464, and 476 ms; three complete read-only process-launch-to-report
samples completed in 305, 359, and 260 ms and each output had zero NUL bytes.
One idle interactive-process working-set sample was 286,765,056 bytes. These
are process-level baselines only, not cold-boot, user-visible interaction, CPU
contention, or memory-pressure qualification.

Production hygiene was also inspected in the staged binary. Fixture mode is
reachable only by an explicit `--development-fixture` command-line switch;
Lab repair requires the additional explicit `--lab-repair-mode` switch. The
fixture-gate startup smoke passed. Normal mode is visibly read-only and cannot
execute LabQualified repairs. This is an explicit development gate, not a
normal-user repair path.

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
- The current `0846edf` product candidate built successfully. Its focused
  `hidhide_doctor_domain_tests` run passed in 10.85 seconds. The resulting
  staged package passed five repeated `--scan-smoke` runs after six successful
  build-output smoke runs; all 1,387 staged files matched its component manifest
  by path, byte count, and SHA-256.
- The complete configured suite then passed 17/17 from the build directory
  configured against this worktree; its independently repeated domain-test leg
  passed in 6.56 seconds and the native-QML layout test passed in 16.25 seconds.
- The owner exported one real Safe-to-Share bundle from that staged interactive
  process. The five expected artifacts parsed and were nonempty/NUL-free; the
  report redaction manifest recorded 12 included, 714 redacted, and 2 excluded
  entries, while the scoped local-identifier check found no current user or
  host/profile/temp/app-data path. This is one narrow owner-machine result.
- The final-source focused Doctor CTest set passed 5/5: domain, deep repair,
  standalone startup, path-boundary contract, and layout. The new contract
  starts the Doctor directly with `ProcessStartInfo.ArgumentList`: a Unicode
  report destination containing spaces, ampersand, percent, semicolon, and
  apostrophe created valid JSON with exit 0; a regular-file parent exited 3 and
  created no report. No shell interpreted either destination.
- The immutable final stage passed manifest verification (1,387/1,387 entries),
  `--startup-smoke`, `--scan-smoke`, and headless report creation, all exit 0.
  Its final-stage process campaign completed 27 runs with no failure or timeout:
  9 startup smokes had a 553.435 ms median (515.728–595.093 ms), 9 headless
  report serializations had a 295.748 ms median (280.710–383.385 ms), and three
  scan smokes per synthetic CPU level had medians of 1388.309 ms idle,
  1478.011 ms moderate, and 1733.912 ms heavy. This is process-level evidence,
  not first-visible or owner interaction latency.

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
| 1–3. Phase 4 basis, Phase 5 lineage, final candidate | Phase 4 `03c7efdf52c69e9edb78747e38780360b5969cde`; Phase 5 branch/worktree above; integration baseline `560842c0afff93c9fd387c3c3d566f8e97c51b9a` is recorded on the submitted PR. Earlier owner review used `6734b773a5f2a70764ef029b705b804625cea154` and records baseline `7ee23194e18e8bbd80ce143e11052800669ce19c`. The current product candidate is the Unicode report-destination fix `aba8d2358a5899648be7f1b1b7346648a3159163`, staged at `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2`; its source head requires terminal CI. |
| 4–8. Architecture, launch points, context, result, independence | Devices, Flight Deck Diagnostics, and App Health invoke the paired standalone Doctor through the bounded v1 local protocol; Doctor independently observes evidence. In the fresh staged owner session, Devices launched the paired Doctor, while Diagnostics/App Health and Flight Deck Diagnostics focused that same window; the staged Doctor process count remained one. Repeated normal launches focus the same-user existing Doctor instead of creating another process. |
| 9–11. Packaging, version compatibility, updater | Stage script requires mapper, Doctor, and helper at one `HOTAS_VERSION`, embeds component version resources, and creates a component manifest. Deterministic updater fixtures roll back interrupted/invalid replacement and preserve external configuration; real installed-product update atomicity remains Not qualified. |
| 12. Qualification matrix | The platform, hardware, privilege, locale, display, and recipe matrix is the qualification ledger. |
| 13–15. Clean, healthy, partial HidHide | Deterministic diagnostic fixtures only; clean/healthy/partial real or VM environments are Not qualified. |
| 16. Owner machine | Exact staged candidate `C:\hotas-builds\hidhide-doctor-phase5-rc-6734b77-stage` completed a read-only scan: Windows build 26200 x64, 238 checks, 4 findings, 3 diagnoses, zero NUL bytes, and full 1.5.230.0 client / 1.4.181.0 driver version strings. Version mismatch, restart, HID enumeration, and stale-configuration evidence remained separate. |
| 17. External tester campaign | The verified standalone final kit is `C:\hotas-builds\HidHideDoctor-Phase5-ExternalQualification-Kit-2.6.2-Final`, with 1,392 receipt-manifest entries and kit-manifest SHA-256 `932BE13709F56A432A08FFFBE17B505E063C98BC26D31F247F0ED1CEAD619C58`. Its candidate receipt agrees with final-stage manifest SHA-256 `34A2A1BD50409BCB8D4F81FF5EDFF6249D916B0E42C03074EF2B896AC37C13CC`, declares `repairAuthorized: false`, and includes the self-contained candidate, user/tester guide, owner worksheet, external matrix, candidate receipt, and receipt instructions. An external tester has not yet run it. |
| 18–22. R1–R5 qualification | R1–R3 remain LabQualified; R4 is unavailable without an approved newer package; R5 is unavailable without an independently verified rollback package. No FieldQualified promotion occurred. |
| 23. Security audit | Bounded typed context/result storage rejects malformed, oversized, reused, and unsupported inputs; helper protocol safety remains covered by deterministic tests. The new deterministic corpus covers handoff, journals, helper frames, package metadata, and reports. The final stage also passes the direct no-shell Unicode/metacharacter report-destination contract. A complete hostile-input audit remains Not qualified. |
| 24. Fuzz results | Seeded `0x5AFEF00D`, 64-input-per-surface deterministic campaign passed without crash, hang, or malformed-input acceptance. Sustained fuzzing and resource-pressure fuzzing remain Not qualified. |
| 25. Privacy/export audit | Default Safe to Share output redacts non-exportable evidence, records included/redacted/excluded items, writes atomically with 8 MiB per-file bounds, and records automatic network upload as not performed. The owner successfully exported, reopened, and visually accepted a Local / Unredacted Downloads report; a corrected staged read-only report has zero NUL bytes. On the current `0846edf` stage, the owner also exported a Safe-to-Share child bundle with the five expected artifacts, valid JSON, no empty/NUL file, 12 included/714 redacted/2 excluded entries, and no matches for the scoped current user/host/profile/temp/app-data identifiers. This narrowly qualifies that real artifact; it is not a universal privacy or external-delivery audit. |
| 26. DPI/accessibility | Native-QML fixture coverage includes density and layout behavior; real Windows DPI, screen-reader, and owner visual review are Not qualified. |
| 27–28. Performance and CPU contention | Final-stage process data: 9 startup-smokes median 553.435 ms (515.728–595.093); 9 headless report serializations median 295.748 ms (280.710–383.385); three scans per level exited 0 at idle median 1388.309 ms, moderate synthetic CPU median 1478.011 ms, and heavy synthetic CPU median 1733.912 ms (heavy maximum 1796.992 ms). A five-second protected interactive-idle observation was 0.02% CPU across 16 logical processors, 163,213,312 B working set, and 149,762,048 B private memory. First-visible and native-interaction latency plus low-resource/write-failure qualification remain Not qualified. |
| 29. Hardware diversity | Deterministic controller/provider coverage only; real hardware matrix is Not qualified. |
| 30–31. Full HOTAS and theme regression | Configured local CTest coverage is recorded; five-theme native acceptance and the full practical manual regression matrix are Not qualified. |
| 32. Installer/update/uninstall | Final stage packaging and standalone startup/scan/headless-report smoke passed. The installer compiler is unavailable on this host, and the governed install/upgrade/uninstall acceptance script deliberately refuses to run outside an isolated GitHub Actions Windows runner. Installed-product update atomicity, recovery, and uninstall are therefore Not qualified locally. |
| 33–34. RC artifact and manifest | Immutable final stage `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2`: 1,387 component-manifest entries, manifest SHA-256 `34A2A1BD50409BCB8D4F81FF5EDFF6249D916B0E42C03074EF2B896AC37C13CC`, zero path/size/hash mismatches, and stage `--startup-smoke`, `--scan-smoke`, and headless report each exited 0. `HidHide Doctor.exe` SHA-256 is `6E45DE692B327470EDBF81D5691758E773D68057757DB929CB62E68D9AF9DC1D`; mapper, launcher, Doctor, and helper each report `NotSigned`. This is not a public release artifact; the current PR head requires terminal CI. |
| 35–36. Blockers and limitations | Cross-machine coverage, owner repair/reboot, native review beyond the exercised entry points, accessibility/DPI, user-visible/low-resource qualification, installed-product update/uninstall, full manual regression, and external testing remain release blockers. Process-level CPU contention is locally evidenced but is not a substitute for those user-visible or resource-pressure gates. Components are `NotSigned`. |
| 36a. Final local CTest rerun | A fresh final-source sequential CTest run passed all 18 registered tests in 260.14 seconds with a 180-second CTest timeout bound. The new Doctor-focused path contract passed; `app_backend_startup_tests` passed in 13.20 seconds; `app_qml_startup_tests` passed in 157.25 seconds; and the theme test completed the suite. The earlier 339-second nonterminating local observation remains historical diagnostic context only: this is a new terminal 18/18 result, and the live mapper and Doctor were not touched. |
| 37. Release recommendation | **NOT READY**. No merge, tag, public release, elevation, or repair authorization is implied by this record. |
