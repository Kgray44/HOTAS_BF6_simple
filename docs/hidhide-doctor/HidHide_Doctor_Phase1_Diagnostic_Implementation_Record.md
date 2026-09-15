# HidHide Doctor Phase 1 — Diagnostic Implementation Record

> **Status:** Phase 1 candidate only. This record grants no repair, merge,
> release, elevation, or Phase 2 authority.

## Provenance

| Item | Value |
|---|---|
| Accepted Phase 0 base | `b986928a122be896e81b041c82d9db85d12981bc` |
| Phase 1 branch | `codex/hidhide-doctor-phase1-deep-diagnostics` |
| Phase 1 worktree | `C:\Users\kkids\Documents\HOTAS_BF6-hidhide-doctor-phase1-deep-diagnostics` |
| Final Phase 1 SHA | Supplied by the stacked-PR handoff after this source-controlled record is committed. A Git commit cannot contain its own final object ID without changing it. |
| Upstream revalidation | 2026-09-14: HidHide public `Shared/HidHideIoctlContract.h`, `HidHide/src/Logic.h`, developer and install-layout documentation. |

Phase 0's model is retained exactly:

```text
read-only native observation -> EvidenceRecord -> DoctorCheckResult
                              -> future Finding/Diagnosis/Repair only
```

Phase 1 produces no findings, diagnoses, repair plans, helper invocations, or
HidHide state changes. Doctor is outside the DirectInput -> MappingWorker ->
vJoy hot path.

## Real provider map

`ReadOnlyWindowsDiagnosticProvider` is the only production provider. It uses
bounded, native, observational sources:

| Area | Source |
|---|---|
| Platform, token, reboot | Registry reads, `GetNativeSystemInfo`, token query, uptime, storage, standard-path queries. |
| Install/files | ARP/uninstall registry reads, dynamic Program Files roots, file metadata, SHA-256, PE architecture, version resource, Authenticode trust. |
| Service/driver | SCM connect/query only; service config/status; Driver Store `FileRepository` and INF metadata read. |
| Devices | Bounded SetupAPI/CM enumeration plus read-only HID-interface capability reads; individual property errors retain device/property/error evidence. |
| Process/privilege | Toolhelp snapshot limited to HidHide/HOTAS names; no launch or injection. |
| Windows evidence | Bounded Application/System Event Log, WER archive, and `setupapi.dev.log` tail. |
| Configuration | HidHide parameter registry reads plus direct protocol GET results; disagreements become contradiction evidence. |

There is no hard-coded development-machine path, `winver` UI, required CLI,
PowerShell, WMI, or HidHide Client launch.

## Direct HidHide protocol

Only these documented read-access GET operations exist in the provider:

| Operation | Function |
|---|---:|
| `GET_WHITELIST` | 2048 |
| `GET_BLACKLIST` | 2050 |
| `GET_ACTIVE` | 2052 |
| `GET_INVERSE` | 2054 |

The reader opens read-only (`GENERIC_READ`, shared read/write/delete,
overlapped), retains SetupAPI interface discovery/open evidence, and uses the
upstream-compatible `\\.\HidHide` control endpoint for GETs. A published
interface that opens but cannot route the contract is evidence; it does not
poison the stable-name GET probes.

Each operation is independent, uses a 2.5-second timeout, calls `CancelIoEx`
for pending I/O, and records a distinct timed-out or failed result with exact
native error. Boolean, list-size, list-payload, MULTI_SZ validation, and
bounded repeat results are separate. No SET IOCTL is defined or callable.

## Catalog coverage

All 238 v1.1 IDs are registered from the embedded governing catalog and use
the governing title.

| State | IDs | Count |
|---|---|---:|
| Direct Phase 1 source | `HD-SYS-001..018`, `HD-PORT-001..030`, `HD-API-001..024`, `HD-CFG-001..024`, `HD-DEV-001..028`, `HD-WIN-001..018` | 142 |
| Conditional/read-only | `HD-INST-001..016`, `HD-PKG-001..020`, `HD-DRV-001..022`, `HD-ISO-001..015` | 73 |
| Deferred to Phase 2 | `HD-X-001..018`, `HD-KB-001..005` | 23 |

Conditional checks yield direct evidence when safe, otherwise explicit
`Unknown`/`Not Applicable`, never a category-wide healthy default. Deferred
checks say deterministic correlation and knowledge-signature evaluation is a
Phase 2 concern.

## Evidence coverage and limits

The component matrix records discovered client, CLI, driver, Driver Store, and
installer candidates with hash, PE architecture, file/product version,
timestamps, and the exact signature result Windows returned. `Not signed` is
not interpreted as signed.

Configuration checks cover persistent/API active and inverse values,
whitelist/blacklist counts, duplicate/missing/malformed paths, stale device
references, virtual-output hiding, active-empty lists, inverse context, and
read completeness. They distinguish configured state from proof of live game
visibility.

Device data includes canonical ID, label, manufacturer, hardware/compatible
IDs, location, status, problem code, Container ID, actual device-class driver
provider/version, HID interface paths, and HID parser usage page/usage when
the device exposes it. Interface handles are opened `GENERIC_READ` with shared
read/write/delete access only to obtain `HidP_GetCaps`; no report is sent or
received and no game visibility is claimed. A malformed property is isolated
and cannot abort the rest of enumeration.

Event Log queries are provider/channel/time bounded. WER is limited to 32
reports and 32 KiB each. SetupAPI uses a bounded tail and 32 matching lines.
Pending-reboot evidence reads pending-file-rename, CBS, and Windows Update
markers only. Missing/limited sources stay explicit; the program never
elevates.

## Progress, cancellation, report, and UI

The standalone UI loads before a single bounded diagnostic worker starts.
Actual provider stages update the canonical `DiagnosticPlan` and work-derived
progress; no timer fabricates progress. Focus and Command Center read the same
session. The UI shows current phase/check, completion, remaining work,
warning/failure count, elapsed time, cancel, and clean rerun. Each rerun has a
new session identity.

Cancellation prevents later work where possible, marks unstarted entries
`Cancelled`, retains collected evidence, and safely joins the worker. The same
engine powers the GUI and `--headless --report <path>`.

Schema-2 JSON exports provenance, coverage, check results with evidence IDs,
all evidence records, component/package/service/protocol/device/process data,
event/WER/SetupAPI records, restart markers, contradictions, and limits.
Redaction hides profile paths, protocol/config values, and text excerpts while
keeping IDs, states, timing, and native errors.

## Automated evidence

`hidhide_doctor_domain_tests` drives the same engine across 30 deterministic
fixtures: healthy/absent/partial/mixed-version installs; newer Driver Store
package; restart; missing/denied/timed-out control; isolated 0x57 and
all-protocol failure; GUI crash evidence; malformed/zero/large device sets;
stale/missing/virtual configuration; x64/ARM64/wrong-architecture/future/
non-English environments; Event Log/WER/SetupAPI limits; cancellation; and
registry/live contradiction.

It fuzzes 64 malformed evidence combinations (long/odd Unicode labels,
duplicate IDs, missing values, unknown errors, unusual paths, and missing
event fields), requires bounded valid JSON, validates redaction, verifies
progress/cancellation/rerun semantics, and statically guards against registry
writes, service changes, process launch, device class-installer calls, and
`IOCTL_SET` exposure. A focused metadata fixture proves that observed HID
usage, device-class driver context, and Container ID set `HD-DEV-007`,
`HD-DEV-010`, and `HD-DEV-012` independently. The domain-test target copies
its `Qt6Core` and `Qt6Test` runtime beside the executable so its direct launch
is a valid test entry point, not only a CTest-only configuration; it does not
invoke a competing QML deployment during a parallel full build. The two GUI
runtime deployments are serialized by a build-tree lock because `windeployqt`
owns shared output paths; this does not introduce a Mapper/Doctor runtime
dependency.

## Local candidate results

The 2026-09-15 local headless run completed the same engine with 238 results
and evidence records, 39 relevant device candidates, 37 HID interface paths,
26 captured usage page/usage pairs, 38 device-class provider/version contexts,
three observed class-filter registrations, 14 individual protocol
observations, and a 225,784-byte redacted schema-2 report in 1,168 ms engine
time (1,725 ms process wall time; neither is a cross-machine SLA). The
Container-ID evidence check was healthy; sensitive Container IDs and interface
paths are redacted in the exported report. `OPEN_INTERFACE_GUID`,
`OPEN_CONTROL`, active/inverse, whitelist/blacklist size and payload, and their
repeat checks were healthy.
The raw report also retains published-interface evidence separately from the
stable control-name GET endpoint, rather than silently discarding either path.

Focused domain and standalone startup tests passed after the final candidate
build. No UAC prompt, helper, HidHide GUI/CLI launch, registry write, service,
package, or device mutation, or HidHide SET action occurred. This is not proof
of all Windows versions/controllers, public release qualification, or live
game isolation.

## Phase 2 entry recommendation

Phase 2 may consume the stable Phase 1 evidence records for audited
correlation, knowledge-base signatures, and user-authorized repair planning.
It must not infer a package-layout manifest, filter-order rule, raw HID report
descriptor, topology, session SET capability, or live game-process visibility
from Phase 1's evidence alone. New authority is required for every mutation,
release, merge, and owner acceptance.
