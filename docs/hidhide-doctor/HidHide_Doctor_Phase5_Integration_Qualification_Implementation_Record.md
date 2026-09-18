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
- Current forensic-evidence candidate: `e8010fb`. Its full local CTest run at
  `C:\hotas-builds\hidhide-doctor-phase5-addendum1` passed all 18 registered
  tests with a 180-second CTest timeout; `app_qml_startup_tests` completed in
  150.05 seconds. Its independently staged R4 runtime is
  `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R4`: 1,387 payload files each
  agree by path, byte count, and SHA-256 with its component manifest. The
  manifest SHA-256 is
  `A0CC2F622C244FFC35E2B0152F8243AAC9E6478C467B47FC7FA180C04F141F54`;
  `HidHide Doctor.exe` is
  `6A9E2B4ECB43223DECF9C0D73048D16AB987A96883D3FE8CC4ABE6A118817306`.
  Staged `--startup-smoke` and read-only `--scan-smoke` each exited 0. This
  records local candidate provenance only; it does not qualify repair,
  accessibility, cross-machine behavior, or release.
- Current forensic owner-review-fixture candidate: `e8dcd71`. Its full local
  CTest run at `C:\hotas-builds\hidhide-doctor-phase5-addendum1` passed all
  18 registered tests in 241.99 seconds with the same 180-second timeout
  bound. The newly added fixture is staged in
  `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R5`; all 1,387 payload files
  match the stage manifest by path, byte count, and SHA-256. The manifest
  SHA-256 is `11D6C061E08CF0A3644BF4E6C2CEE124901FCDAD6BE81235C0263275EB97C70E`;
  `HidHide Doctor.exe` is
  `E6620839285D58F39BE57984218822C063AD242B26E82D71EB377866FD79002F`.
  The staged `Forensic Evidence Review` fixture completed headlessly with exit
  0. It remains simulated, explicitly labelled review evidence—not a repair
  action or a substitute for the required owner-machine scan.
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
- Current product-fix source sequence: `aba8d2358a5899648be7f1b1b7346648a3159163`
  preserves native Windows Unicode report destinations; `4ad2effb8517177ae925a154b832f5bcdfea0656`
  makes diagnostic-bundle promotion transactional; and
  `2f44ce7b198d8707d73f4184f565f9d95059a360` isolates the backend startup
  test binary from the real mapper. The latter is compiled only for
  `HOTAS_STARTUP_TESTING`; production mapper startup is unchanged. The resulting
  immutable R2 candidate is `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R2`.
  It has 1,390 component-manifest entries, component-manifest SHA-256
  `B895516AA73B9CF32F2354960294FBA62A3BAC4228F3C86E681308DFB8785753`, and
  `HidHide Doctor.exe` SHA-256
  `894576B68B78293B0BA1C7D95EBB4253E45515638730272CCA6AFD1BEB6D6E85`.
  Every staged entry was rechecked by path, byte count, and SHA-256. This newer
  product source is represented by PR record head
  `4e72f376193317fc9362ae1409b0362afa59dce1`, where both GitHub workflows
  reached terminal `SUCCESS`: Documentation Check run `35257325562` (12 s) and
  HOTAS BF6 CI run `35257325468` (15 min 13 s). The latter passed toolchain and
  documentation checks, QML lint, Release configuration, compilation, mapped
  automation, launcher readiness, and benchmark tests.
- Installer-contract source `95ccdb21b4969aa26cc43eadda3faf2cde42a6e0`
  requires candidate installations to contain mapper, launcher, Doctor, and
  helper; it smoke-tests the standalone Doctor after every resulting
  clean/upgrade/recovery candidate install, and verifies uninstall removal of
  every shipped executable. The historical v2.5.0 pre-upgrade fixture alone
  may lack the later Doctor components. On that exact head, Documentation Check
  run `35261972085` reached terminal `SUCCESS` in 10 seconds and HOTAS BF6 CI
  run `35261971968` reached terminal `SUCCESS` in 15 min 20 s. The later,
  non-publishing PR-only isolated installer qualification run `35287394992`
  reached terminal `SUCCESS` on source
  `4e8337e6477c6941746ce5bc9bad9432227ce123`: it built and staged the candidate,
  compiled its installer, smoke-tested clean install and uninstall component
  removal, validated clean install, v1.9.3 migration, v2.0 recovery, v2.5 N-1
  upgrade, default path/shortcuts, and standalone Doctor startup after every
  candidate state, then uploaded only non-public qualification artifacts. This
  is a narrow isolated Windows CI-runner result, not a field owner install or
  release decision.
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
The bundle writer has a per-file 8 MiB cap and no network/upload capability.
It writes the five artifacts to a private sibling staging directory with
atomic per-file writes, then performs one directory rename to expose the final
bundle only after every file has committed.

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

R2 changes only that bundle-commit boundary: the composer now creates a
private sibling directory, removes it on a write failure, and promotes it to
the user-selected timestamped directory only after all five artifacts commit.
The direct domain suite verifies a successful bundle leaves no staging sibling
visible and a blocked parent leaves no final bundle. This is deterministic
fixture evidence. The owner subsequently reported that the requested R2
launch, scan, and Safe-to-Share export check looked good. Downloads contained
only the earlier `HidHideDoctor-Diagnostic-Bundle-20260917-140145111Z` artifact,
created at `2026-09-17T14:01:45Z`, which predates the R2 request; therefore it
is not attributed to R2 and R2's transaction promotion has no separately
inspected owner-created artifact yet.

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
The immutable R2 staged `2.6.2` candidate has 1,390 manifest entries. All four
packaged executables (mapper, launcher, Doctor, and helper) reported
`NotSigned`; no signing certificate was available or invented. Helper protocol
is v2 and integration protocol is v1. When Artifact Signing is explicitly
configured, the release workflow submits and verifies all four staged
executables rather than signing only the mapper and launcher; that conditional
path has not run for this unsigned candidate.

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
- For immutable R2, the owner reported that the requested native launch, scan,
  and Safe-to-Share export sequence looked good. The retained Downloads bundle
  was created before the R2 request, so this is owner-reported visual/runtime
  evidence only, not a new R2 bundle artifact, independent transaction proof,
  or a replacement for the remaining native qualification gates.
- The final-source focused Doctor CTest set passed 5/5: domain, deep repair,
  standalone startup, path-boundary contract, and layout. The new contract
  starts the Doctor directly with `ProcessStartInfo.ArgumentList`: a Unicode
  report destination containing spaces, ampersand, percent, semicolon, and
  apostrophe created valid JSON with exit 0; a regular-file parent exited 3 and
  created no report. No shell interpreted either destination.
- The immutable R2 final stage passed manifest verification (1,390/1,390
  entries), `--startup-smoke`, `--scan-smoke`, and headless report creation,
  all exit 0. Its direct Unicode/metacharacter contract also passed: special
  destination exit 0 with valid JSON, blocked-parent exit 3, and no shell
  interpretation. Its exact-stage 15-process campaign had no failure or timeout:
  three startup smokes had a 506.794 ms median (468.240–527.912 ms), three
  headless report serializations had a 310.325 ms median (256.774–323.025 ms),
  and three scan smokes per synthetic CPU level had medians of 1327.669 ms idle,
  1388.527 ms moderate, and 1682.934 ms heavy. This is process-level evidence,
  not first-visible or owner interaction latency.
- The installer acceptance contract now treats mapper, Doctor, and helper as
  one candidate component set: every resulting candidate clean/upgrade/recovery
  installation must contain the Doctor and helper, and each invokes standalone
  Doctor `--startup-smoke`. The historical v2.5.0 pre-upgrade fixture is
  explicitly allowed to lack the later Doctor components, but the candidate
  replacement is not. The isolated uninstall smoke also asserts all four
  shipped executables are removed. The current host intentionally did not run
  that script, which is restricted to a GitHub Actions Windows runner.
- A fresh sequential CTest run after that contract change passed all 18
  registered tests in 247.97 seconds with `--timeout 180`; the strengthened
  release-contract test passed and the longest `app_qml_startup_tests` leg
  completed in 155.29 seconds. This is local automated evidence only.
- The domain suite now supplies two deterministic write-failure fixtures. A
  regular file substituted for the repair-journal root makes the planning-only
  dry run end `FailedSafely` with no mutation or journal artifact. A Markdown
  report whose parent is a regular file fails with no report artifact. These
  exercise unavailable-root/parent handling without changing disk capacity,
  permissions, AppData, or live HidHide state; they are fixture evidence only.
- The deep-helper regression now also re-seals and injects forbidden
  `command`, argument, executable, service, filter, registry-path, cache-path,
  and RunOnce-like package fields, plus a mismatched one-time nonce. Each is
  rejected by the typed helper protocol. A fresh full 18-test CTest run passed
  after adding those vectors; this is test-only helper-security evidence, not
  authorization for a mutable repair.
- After the requested native display review, the owner reported that everything
  looked good. No scale-factor list, page-by-page notes, keyboard-only
  traversal, accessibility-name/role capture, or screen-reader result was
  retained, so this is recorded as a narrow owner visual check only.
- Source `f2531bd8acf5d6f3bf2df760f1591714faa20c72` repairs the custom
  Doctor `AbstractButton` activation contract: both shared buttons and
  segmented-control choices now have explicit button roles and respond to
  Return/Enter as well as Space. The focused native-QML test covers forward
  Tab/Space, reverse Shift+Tab/Return, and Escape restoring a maximized pane;
  it passed with zero QML warnings. A fresh full 18-test CTest pass also
  completed after this change. This is fixture evidence, not a substitute for
  the required owner keyboard-only or screen-reader passes.
- The post-keyboard immutable candidate is
  `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R3`. Its 1,390 manifest entries
  passed a fresh path/size/SHA-256 check with zero mismatches. The component
  manifest SHA-256 is `F5BE48043A5555E645C519AD0ACDECB97E5BA5CD0598FF06CE7BED7D6B6B8491`;
  `HidHide Doctor.exe` SHA-256 is
  `8F01445C2EBC7B3918D817ECCDB6BF5CF1B5C27486BB32AAE82246B9394540D5`.
  Mapper, Doctor, and helper are 2.6.2 and `NotSigned`; staged startup and scan
  smoke each exited 0. R2 remains historical evidence only.
- On exact R3, the owner completed the focused keyboard-only verification:
  Tab then Space selected Command Center; Shift+Tab then Enter returned to
  Focus; a Command Center pane's MAX action restored with Escape; and keyboard
  traversal through Density, Inspector, and Export Report completed without an
  export or repair. The owner reported every step passed and visible focus was
  retained. This is a narrow native R3 keyboard result, not an exhaustive
  accessibility audit or screen-reader result. Documentation Check run
  `35271915479` and HOTAS BF6 CI run `35271915472` both reached terminal
  `SUCCESS` on source `8fb2b8731b83ea1ed8721d4231f321c5e497f8b8`.

## Phase 5 forensic evidence depth addendum

The follow-on evidence reconstruction keeps the accepted Inspector and
Timeline presentation direction, but replaces the prior shallow boundary where
rich Windows/HidHide provider snapshot data became one generic EvidenceRecord
per check. The canonical path is now provider snapshot → `DoctorCheckResult`
→ structured `EvidenceRecord` → `DoctorActivityEvent` → Inspector →
report/bundle. It remains read-only and stays entirely on the Doctor control
plane.

- Every new canonical record names its check and catalog purpose, provider,
  subsystem, status rationale, target, collection method, start/completion
  timestamps, monotonic microsecond duration, timeout, and explicitly labelled
  forensic fields. `GET_ACTIVE` and `GET_INVERSE` render semantic enabled or
  disabled statements; raw boolean values remain labelled raw data, never the
  whole owner-facing conclusion.
- Protocol records preserve endpoint, read-only access, API, IOCTL, request
  and response byte counts, attempt/timeout, timestamps, native code/error,
  and a sensitivity-classified bounded payload sample. Installation/package,
  service/driver, configuration, device, Event Log/WER/SetupAPI, and derived
  correlation checks retain their corresponding structured facts instead of a
  generic “no raw detail” fallback.
- Activity is a compact lifecycle trace: session/phase/check start, protocol
  completion, evidence recording, check completion, finding/diagnosis creation,
  repair-plan creation, user action, and session completion. It links IDs to
  canonical evidence rather than duplicating payload. Retention is bounded at
  600 events with an explicit omitted-event marker.
- The Inspector presents Identity, Observation, Target, Method, Timing, Native
  Result, Relationships, Technical, and Raw cards; it supports previous/next
  evidence and linked-evidence navigation. Its Safe-to-Share copy choices are
  summary, technical fields, complete evidence, and JSON.
- Diagnostic serialization moves from schema 5 to 6 and report/bundle schema
  from 1 to 2. Both changes are additive and carry compatibility metadata.
  `HidHide_Doctor_Evidence_Schema_Migration_v6.md` records the reader guidance,
  per-field Safe-to-Share redaction, and explicit collection bounds.
- A fresh full CTest run passed all 18 registered tests with the explicit
  180-second bound. That includes `hidhide_doctor_domain_tests` (with a
  zero-unexpected-shallow-record forensic coverage check),
  `hidhide_doctor_deep_repair_tests`, and `hidhide_doctor_layout_tests`; the
  isolated QML startup regression completed in 150.05 seconds. These are
  automated evidence only;
  the requested live owner acceptance across healthy, informational, warning,
  protocol, package, service, device, Windows evidence, diagnosis, and repair
  transaction families remains pending.

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
| 1–3. Phase 4 basis, Phase 5 lineage, final candidate | Phase 4 `03c7efdf52c69e9edb78747e38780360b5969cde`; Phase 5 branch/worktree above; integration baseline `560842c0afff93c9fd387c3c3d566f8e97c51b9a` is recorded on the submitted PR. Earlier owner review used `6734b773a5f2a70764ef029b705b804625cea154` and records baseline `7ee23194e18e8bbd80ce143e11052800669ce19c`. The candidate sequence is Unicode destination preservation `aba8d23`, transactional bundle commit `4ad2eff`, test-only mapper isolation `2f44ce7`, and installer-component contract `95ccdb2`, staged at `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R2`. Documentation Check `35261972085` and HOTAS BF6 CI `35261971968` both reached terminal `SUCCESS` on `95ccdb2`; their scope does not include the release-only isolated installer job. |
| 4–8. Architecture, launch points, context, result, independence | Devices, Flight Deck Diagnostics, and App Health invoke the paired standalone Doctor through the bounded v1 local protocol; Doctor independently observes evidence. In the fresh staged owner session, Devices launched the paired Doctor, while Diagnostics/App Health and Flight Deck Diagnostics focused that same window; the staged Doctor process count remained one. Repeated normal launches focus the same-user existing Doctor instead of creating another process. |
| 9–11. Packaging, version compatibility, updater | Stage script requires mapper, Doctor, and helper at one `HOTAS_VERSION`, embeds component version resources, and creates a component manifest. Deterministic updater fixtures roll back interrupted/invalid replacement and preserve external configuration; real installed-product update atomicity remains Not qualified. |
| 12. Qualification matrix | The platform, hardware, privilege, locale, display, and recipe matrix is the qualification ledger. |
| 13–15. Clean, healthy, partial HidHide | Deterministic diagnostic fixtures only; clean/healthy/partial real or VM environments are Not qualified. |
| 16. Owner machine | Exact staged candidate `C:\hotas-builds\hidhide-doctor-phase5-rc-6734b77-stage` completed a read-only scan: Windows build 26200 x64, 238 checks, 4 findings, 3 diagnoses, zero NUL bytes, and full 1.5.230.0 client / 1.4.181.0 driver version strings. Version mismatch, restart, HID enumeration, and stale-configuration evidence remained separate. |
| 17. External tester campaign | The verified R2 kit is `C:\hotas-builds\HidHideDoctor-Phase5-ExternalQualification-Kit-2.6.2-R2`, with 1,395 receipt-manifest entries and kit-manifest SHA-256 `E9E86227BC1713D1D29AC031FE5171AF28D855F525CCEC8356948A49DAEF3E94`. Its candidate receipt agrees with R2 stage-manifest SHA-256 `B895516AA73B9CF32F2354960294FBA62A3BAC4228F3C86E681308DFB8785753`, records the Doctor/helper hashes, declares `repairAuthorized: false`, and includes the self-contained candidate, user/tester guide, owner worksheet, external matrix, candidate receipt, and receipt instructions. An external tester has not yet run it. |
| 18–22. R1–R5 qualification | R1–R3 remain LabQualified; R4 is unavailable without an approved newer package; R5 is unavailable without an independently verified rollback package. No FieldQualified promotion occurred. |
| 23. Security audit | Bounded typed context/result storage rejects malformed, oversized, reused, and unsupported inputs; helper protocol safety remains covered by deterministic tests. The deterministic corpus covers handoff, journals, helper frames, package metadata, and reports. R2 also passes the direct no-shell Unicode/metacharacter report-destination contract and fixture coverage for transactional bundle promotion. A complete hostile-input audit remains Not qualified. |
| 24. Fuzz results | Seeded `0x5AFEF00D`, 64-input-per-surface deterministic campaign passed without crash, hang, or malformed-input acceptance. Sustained fuzzing and resource-pressure fuzzing remain Not qualified. |
| 25. Privacy/export audit | Default Safe to Share output redacts non-exportable evidence, records included/redacted/excluded items, records automatic network upload as not performed, and in R2 commits bundles through private staging plus a final directory rename. The owner successfully exported, reopened, and visually accepted a Local / Unredacted Downloads report; the earlier `0846edf` candidate also produced one Safe-to-Share bundle with the stated five artifacts and scoped privacy check. R2 has deterministic transactional-bundle coverage but awaits a new owner-native bundle check. Neither result is a universal privacy or external-delivery audit. |
| 26. DPI/accessibility | Native-QML fixture coverage includes density/layout behavior plus the `f2531bd` forward/reverse keyboard activation, Escape-restoration, and custom-button role contract. On R3, the owner confirmed the focused keyboard-only View selection, reverse View selection, pane MAX/Esc restore, and Density/Inspector/Export traversal with visible focus. The R3 stage passed startup/scan smoke. Real Windows DPI scale-by-scale review and screen-reader observation remain not qualified. |
| 27–28. Performance and CPU contention | R2 exact-stage process data: three startup smokes median 506.794 ms (468.240–527.912); three headless report serializations median 310.325 ms (256.774–323.025); three scans per level exited 0 at idle median 1327.669 ms, moderate synthetic CPU median 1388.527 ms, and heavy synthetic CPU median 1682.934 ms (heavy maximum 1713.139 ms). A five-second protected interactive-idle observation was 0.000% CPU across 16 logical processors, 163,209,216 B working set, and 149,696,512 B private memory. New deterministic writer fixtures show an unavailable journal root fails the planning-only dry run before mutation and a regular-file report parent leaves no report; first-visible/native-interaction latency and real low-resource/write-failure qualification remain Not qualified. |
| 29. Hardware diversity | Deterministic controller/provider coverage only; real hardware matrix is Not qualified. |
| 30–31. Full HOTAS and theme regression | Configured local CTest coverage is recorded; five-theme native acceptance and the full practical manual regression matrix are Not qualified. |
| 32. Installer/update/uninstall | Final stage packaging and standalone startup/scan/headless-report smoke passed. The installer compiler is unavailable on this host, and the governed install/upgrade/uninstall acceptance script deliberately refuses to run outside an isolated GitHub Actions Windows runner. Its acceptance contract now requires the Doctor and helper in every installed package, smoke-tests the standalone Doctor after each clean/upgrade/recovery installation, and asserts the uninstaller removes mapper, launcher, Doctor, and helper. This is source-contract coverage only; installed-product update atomicity, recovery, and uninstall remain Not qualified locally until that isolated workflow runs. |
| 33–34. RC artifact and manifest | Historical R2 evidence remains recorded above. The post-keyboard candidate is immutable R3 `C:\hotas-builds\HidHideDoctor-v1-RC1-2.6.2-R3`: 1,390 component-manifest entries, manifest SHA-256 `F5BE48043A5555E645C519AD0ACDECB97E5BA5CD0598FF06CE7BED7D6B6B8491`, zero fresh path/size/hash mismatches, and staged `--startup-smoke` / `--scan-smoke` exited 0. `HidHide Doctor.exe` SHA-256 is `8F01445C2EBC7B3918D817ECCDB6BF5CF1B5C27486BB32AAE82246B9394540D5`; mapper, Doctor, and helper each report `NotSigned`. Documentation Check `35271915479` and HOTAS BF6 CI `35271915472` reached terminal `SUCCESS` on R3 source `8fb2b8731b83ea1ed8721d4231f321c5e497f8b8`. This is not a public release artifact. |
| 35–36. Blockers and limitations | Cross-machine coverage, owner repair/reboot, native review beyond the exercised entry points, accessibility/DPI, user-visible/low-resource qualification, installed-product update/uninstall, full manual regression, and external testing remain release blockers. Process-level CPU contention is locally evidenced but is not a substitute for those user-visible or resource-pressure gates. Components are `NotSigned`. |
| 36a. Final local CTest rerun | A fresh installer-contract-source sequential CTest run passed all 18 registered tests in 247.97 seconds with a 180-second CTest timeout bound. The Doctor-focused path contract and strengthened release-contract test passed; `app_qml_startup_tests` passed in 155.29 seconds; and the theme test completed the suite. The startup-test binary no longer starts the real mapper, which removes the observed post-assertion shutdown hang without changing production startup. This remains automated/offscreen evidence; the live mapper and Doctor were not touched. |
| 37. Release recommendation | **NOT READY**. No merge, tag, public release, elevation, or repair authorization is implied by this record. |
