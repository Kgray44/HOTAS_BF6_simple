# HidHide Doctor

## Diagnostic Catalog

> **Status:** Design Freeze v1.1. Catalog of mandatory diagnostic domains and baseline checks.

| **Document** | HidHide Doctor Diagnostic Catalog |
|---|---|
| **Version** | Design Freeze v1.1 |
| **Purpose** | Define stable diagnostic IDs, required observations, dependencies, outputs, and repair relevance |
| **Rule** | A check observes. It does not repair. |

# 1. Catalog Semantics

Every check has a stable ID.

Recommended result states:

- `Healthy`
- `Info`
- `Warning`
- `Failure`
- `Unknown`
- `NotApplicable`
- `Blocked`
- `TimedOut`

A check result contains evidence; diagnosis rules later interpret combinations of checks.

## 1.1 Check contract

Each implemented check shall define:

```text
checkId
title
phase
prerequisites
privilege
weight
timeout
sideEffects = None for this catalog
evidenceSchema
statusRules
privacyClass
```

Read-only diagnostics must not mutate HidHide or Windows state.

## 1.2 Diagnostic phases

| Prefix | Phase |
|---|---|
| HD-SYS | System Environment |
| HD-INST | Installation Discovery |
| HD-PKG | Package Integrity |
| HD-DRV | Kernel Driver State |
| HD-API | Protocol / API Health |
| HD-CFG | Configuration Integrity |
| HD-DEV | Device Ecosystem |
| HD-ISO | Isolation Verification |
| HD-WIN | Windows Evidence |
| HD-X | Consistency / Correlation |
| HD-KB | Knowledge-Base Signature Evaluation |

# 2. Baseline Mandatory Checks

The following catalog is intentionally broad. Implementations may add checks, but must not renumber/reuse an existing ID for a different meaning.

# System Environment

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-SYS-001` | Windows product/version/build | Record edition, version, build, architecture. | Foundation for compatibility rules. |
| `HD-SYS-002` | Native architecture | Record x64/ARM64 and process architecture. | Detect architecture mismatches. |
| `HD-SYS-003` | WOW64 state | Determine whether any Doctor/HidHide component runs under emulation. | Package/process context. |
| `HD-SYS-004` | Current boot time | Record boot timestamp/uptime. | Correlate pending replacement and recent installs. |
| `HD-SYS-005` | Administrative token availability | Determine whether elevation could be requested, without elevating. | Repair planning only. |
| `HD-SYS-006` | Current Doctor elevation state | Record elevated/non-elevated. | Prove least-privilege operation. |
| `HD-SYS-007` | Secure Boot state | Read state when accessible. | Evidence only; never auto-disable. |
| `HD-SYS-008` | Test-signing state | Read boot configuration/signing mode. | Flag unsupported/development environments. |
| `HD-SYS-009` | Memory Integrity/HVCI state | Read when available. | Compatibility evidence only. |
| `HD-SYS-010` | Pending reboot indicators | Aggregate qualified Windows restart-required evidence. | Version mismatch diagnosis. |
| `HD-SYS-011` | Pending file rename/replace evidence | Inspect relevant pending replacement state without modifying it. | Driver update diagnosis. |
| `HD-SYS-012` | System time sanity | Detect gross clock anomalies affecting signatures/log correlation. | Evidence reliability. |
| `HD-SYS-013` | Windows Update recency context | Record recent OS build/update identifiers when locally available. | Known-issue correlation. |
| `HD-SYS-014` | System restore availability | Inform repair planning only; do not create restore point here. | Recovery context. |
| `HD-SYS-015` | Free system-drive space | Read free space. | Prevent package repair failure. |
| `HD-SYS-016` | Doctor write workspace availability | Verify own app-data temp/report path. | Session durability. |
| `HD-SYS-017` | Doctor version/build identity | Record exact Doctor version/SHA/channel. | Support evidence. |
| `HD-SYS-018` | HOTAS BF6 launch context | Record whether launched standalone/from HOTAS and supplied issue context. | Integration evidence. |

# Installation Discovery

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-INST-001` | Official HidHide dependency registry presence | Read official package availability/version dependency key if present. | Installation discovery. |
| `HD-INST-002` | Official HidHide install-path registry presence | Read official install root key if present. | Locate components. |
| `HD-INST-003` | Canonical install root existence | Check reported/default install path. | Package layout. |
| `HD-INST-004` | HidHideClient executable presence | Locate all candidates. | Component inventory. |
| `HD-INST-005` | HidHideCLI executable presence | Locate all candidates. | Component inventory. |
| `HD-INST-006` | HidHide Watchdog/setup components | Inventory known upstream payload where applicable. | Package inventory. |
| `HD-INST-007` | Multiple HidHide install roots | Detect duplicate/legacy roots. | Stale installation diagnosis. |
| `HD-INST-008` | Installed product/ARP entries | Enumerate HidHide product entries. | Installer consistency. |
| `HD-INST-009` | MSI product identity | Record package/product/upgrade identity when available. | Repair targeting. |
| `HD-INST-010` | Install timestamp evidence | Record file/package timestamps cautiously. | Correlation only. |
| `HD-INST-011` | Architecture consistency | Compare installed payload architecture to OS. | Installation health. |
| `HD-INST-012` | Uninstall registration health | Verify uninstall metadata points to plausible package. | Repair planning. |
| `HD-INST-013` | Orphaned install metadata | Metadata present while payload absent. | Broken install diagnosis. |
| `HD-INST-014` | Payload present without installer metadata | Files exist with no corresponding package identity. | Manual/stale install diagnosis. |
| `HD-INST-015` | Conflicting provider identity | Detect nonstandard provider/fork metadata. | Provider-aware handling. |
| `HD-INST-016` | HidHide license/readme provenance | Confirm package appears to originate from known provider where feasible. | Trust context. |

# Package Integrity

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-PKG-001` | HidHideClient file version | Read version resources. | Client/driver comparison. |
| `HD-PKG-002` | HidHideCLI file version | Read version resources. | Component mismatch. |
| `HD-PKG-003` | HidHide driver file candidates | Locate all HidHide.sys candidates. | Driver inventory. |
| `HD-PKG-004` | Driver file version | Read file version of installed candidate. | Mismatch detection. |
| `HD-PKG-005` | Driver Store package enumeration | Enumerate all HidHide driver packages. | Package history/duplicates. |
| `HD-PKG-006` | Active driver package identity | Resolve package associated with device/service. | Loaded-vs-store analysis. |
| `HD-PKG-007` | Duplicate driver packages | Detect multiple relevant versions. | Stale package evidence. |
| `HD-PKG-008` | Client Authenticode signature | Validate signature/trust where applicable. | Trust evidence. |
| `HD-PKG-009` | CLI Authenticode signature | Validate signature/trust where applicable. | Trust evidence. |
| `HD-PKG-010` | Driver signature status | Validate package/binary signing through Windows APIs. | Driver-load evidence. |
| `HD-PKG-011` | Installer/package signature status | Validate installed package provenance when available. | Repair safety. |
| `HD-PKG-012` | File hash inventory | Hash relevant HidHide files. | Immutable evidence/known-good comparison. |
| `HD-PKG-013` | Expected payload completeness | Compare installed payload to version/provider manifest when known. | Missing-file diagnosis. |
| `HD-PKG-014` | Unexpected extra HidHide payload | Record extra binaries in install root without deleting. | Stale/tamper evidence. |
| `HD-PKG-015` | File architecture validation | Confirm PE architecture. | Mismatch detection. |
| `HD-PKG-016` | Loaded binary vs on-disk binary version | Compare active module/service package evidence with disk. | Pending replacement diagnosis. |
| `HD-PKG-017` | Loaded binary vs package version | Correlate loaded version to active package. | Pending replacement diagnosis. |
| `HD-PKG-018` | Client/CLI version consistency | Compare user-mode component versions. | Mixed install diagnosis. |
| `HD-PKG-019` | Client/driver protocol-era compatibility | Infer only from qualified capability matrix + direct probes. | Compatibility diagnosis. |
| `HD-PKG-020` | Canonical install-layout conformance | Compare against known provider/version layout. | Packaging evidence. |

# Kernel Driver State

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-DRV-001` | Root HidHide device node present | Locate `root\HidHide` or provider-equivalent. | Driver installation state. |
| `HD-DRV-002` | PnP device status | Capture CM/PnP status/problem code. | Driver health. |
| `HD-DRV-003` | HidHide service present | Inspect SCM service. | Driver registration. |
| `HD-DRV-004` | Service start configuration | Record start type/config. | Installation consistency. |
| `HD-DRV-005` | Service running/loaded state | Record actual runtime state. | Driver health. |
| `HD-DRV-006` | Service binary path | Verify points to expected package/binary. | Package consistency. |
| `HD-DRV-007` | Control device symbolic link openable | Attempt read-appropriate access to `\\.\HidHide`. | Protocol precondition. |
| `HD-DRV-008` | Control-device interface GUID enumeration | Resolve current interface path if supported. | Alternative access evidence. |
| `HD-DRV-009` | HID class filter registration | Inspect relevant filter registration cautiously. | Filter-chain diagnosis. |
| `HD-DRV-010` | XUSB/XInput-related filter registration | Inspect relevant classes where provider/version uses them. | Xbox hiding diagnosis. |
| `HD-DRV-011` | Filter ordering consistency | Compare expected ordering rules where known. | Advanced health. |
| `HD-DRV-012` | Orphaned filter registration | Filter entry references missing/uninstalled service. | Recovery candidate. |
| `HD-DRV-013` | Loaded driver version | Resolve runtime version from active package/file evidence. | Mismatch diagnosis. |
| `HD-DRV-014` | Driver package provider | Record provider/signer. | Provider-aware behavior. |
| `HD-DRV-015` | Driver package date/version | Record INF metadata. | Package context. |
| `HD-DRV-016` | Driver non-stoppable/reboot implications | Determine current replaceability from service/device semantics. | Repair planning. |
| `HD-DRV-017` | Driver device-interface enumeration errors | Record SetupAPI/CM failures. | Driver/PnP diagnosis. |
| `HD-DRV-018` | Control open access denied | Classify ACL/privilege/security failure separately. | Protocol diagnosis. |
| `HD-DRV-019` | Control open path not found | Separate missing device from API operation failure. | Driver state. |
| `HD-DRV-020` | Control open timeout/hang | Bound and classify. | Safety. |
| `HD-DRV-021` | Driver ETW provider availability | Detect registered provider/manifest where applicable. | Deep diagnostics. |
| `HD-DRV-022` | Driver event/counter source availability | Record optional telemetry capability. | Deep diagnostics. |

# Protocol / API Health

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-API-001` | Control-device baseline open | Open/close independent of higher-level proxy. | API health. |
| `HD-API-002` | GET_ACTIVE | Probe independently and record native result/value. | Cloak health. |
| `HD-API-003` | GET_INVERSE | Probe independently and record native result/value. | Whitelist semantics. |
| `HD-API-004` | GET_WHITELIST size query | Probe size-negotiation semantics independently. | Detect #215-like failure. |
| `HD-API-005` | GET_WHITELIST payload query | Retrieve only if size query valid. | Configuration evidence. |
| `HD-API-006` | Whitelist MULTI_SZ structural validity | Validate terminators/even-byte contract. | Corruption diagnosis. |
| `HD-API-007` | GET_BLACKLIST size query | Probe independently. | Device-list API. |
| `HD-API-008` | GET_BLACKLIST payload query | Retrieve independently. | Configuration evidence. |
| `HD-API-009` | Blacklist MULTI_SZ structural validity | Validate payload. | Corruption diagnosis. |
| `HD-API-010` | Session blacklist capability | Detect supported IOCTL/capability without mutating persistent state. | Feature matrix. |
| `HD-API-011` | Session clear capability presence | Detect protocol support. | Feature matrix. |
| `HD-API-012` | Protocol query timing | Measure each independent request. | Hang/degradation evidence. |
| `HD-API-013` | Repeated GET_ACTIVE consistency | A few bounded read repeats to detect unstable interface. | Intermittence detection. |
| `HD-API-014` | Repeated GET_WHITELIST consistency | Bounded repeats only when first call is safe. | Intermittence detection. |
| `HD-API-015` | Repeated GET_BLACKLIST consistency | Bounded repeats. | Intermittence detection. |
| `HD-API-016` | Unknown/unsupported IOCTL classification | Capability probe must distinguish unsupported from broken. | Version/capability matrix. |
| `HD-API-017` | Client/CLI behavior comparison | Optional read-only compare if executables are healthy; never required. | GUI/CLI-only failure diagnosis. |
| `HD-API-018` | CLI version command survivability | Run with strict timeout only if useful and safe. | User-mode fault evidence. |
| `HD-API-019` | CLI app-list survivability | Optional compare; failure does not block Doctor direct API. | Client-specific diagnosis. |
| `HD-API-020` | CLI dev-list survivability | Optional compare. | Client-specific diagnosis. |
| `HD-API-021` | CLI dev-gaming survivability | Optional compare. | Enumeration/client diagnosis. |
| `HD-API-022` | HidHideClient crash evidence correlation | Do not launch solely to provoke crash; correlate existing evidence. | Client diagnosis. |
| `HD-API-023` | Native error-domain preservation | Verify every API failure keeps exact error. | Diagnostic quality gate. |
| `HD-API-024` | Protocol capability fingerprint | Build normalized set of actual supported operations. | Provider/version agnostic operation. |

# Configuration Integrity

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-CFG-001` | Active/cloak state | Interpret GET_ACTIVE result. | Isolation readiness. |
| `HD-CFG-002` | Whitelist inverse state | Interpret GET_INVERSE result. | Access semantics. |
| `HD-CFG-003` | Application list count | Count validated entries. | Configuration summary. |
| `HD-CFG-004` | Application list path normalization | Normalize without rewriting. | Duplicate/stale analysis. |
| `HD-CFG-005` | Duplicate whitelist entries | Detect duplicates. | R1 repair candidate. |
| `HD-CFG-006` | Missing whitelist application files | Flag absent paths. | R1 repair candidate. |
| `HD-CFG-007` | Unresolvable whitelist paths | Flag malformed/unexpandable entries. | Corruption analysis. |
| `HD-CFG-008` | HOTAS BF6 executable exemption | Determine expected HOTAS paths from trusted local install context. | Integration health. |
| `HD-CFG-009` | HidHide Doctor exemption need | Determine whether Doctor itself needs access for specific verification. | Repair/test planning. |
| `HD-CFG-010` | Blacklist/hidden-device count | Count entries. | Configuration summary. |
| `HD-CFG-011` | Duplicate blacklist entries | Detect duplicates. | R1 repair candidate. |
| `HD-CFG-012` | Blacklist entry syntax validity | Validate device-instance-path form. | Corruption analysis. |
| `HD-CFG-013` | Blacklist entry current PnP resolution | Resolve to present/absent/unknown. | Stale device analysis. |
| `HD-CFG-014` | Blacklist physical/virtual classification | Classify when evidence is sufficient. | Isolation correctness. |
| `HD-CFG-015` | Virtual-output accidentally hidden | Detect vJoy/known virtual destination hidden when inappropriate. | HOTAS functional issue. |
| `HD-CFG-016` | Expected physical HOTAS hidden | Compare HOTAS device/rig expectations when launched with context. | Integration issue. |
| `HD-CFG-017` | Unrelated user entries preservation set | Identify entries Doctor must not treat as HOTAS-owned. | Repair safety. |
| `HD-CFG-018` | Registry/API configuration consistency | Compare API read with persisted provider state only where safe/meaningful. | Driver/config mismatch. |
| `HD-CFG-019` | Active with empty blacklist | Informational/suspicious depending context. | Configuration quality. |
| `HD-CFG-020` | Inverse-mode risk/context | Explain unusual inverse semantics. | User-facing clarity. |
| `HD-CFG-021` | Path case/canonical duplication | Detect equivalent duplicates without mutating. | R1 candidate. |
| `HD-CFG-022` | Nonexistent HOTAS whitelist path | Detect moved/uninstalled executable. | Integration repair. |
| `HD-CFG-023` | Session blacklist current ownership visibility | If queryable, record session-vs-persistent behavior. | Runtime isolation evidence. |
| `HD-CFG-024` | Configuration read completeness | Ensure all independent fields either observed or explicitly unknown. | Diagnosis quality. |

# Device Ecosystem

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-DEV-001` | Gaming HID enumeration baseline | Enumerate gaming-class HID devices through Doctor-owned path. | Device inventory. |
| `HD-DEV-002` | All HID enumeration baseline | Enumerate broader HID set with bounds. | Cross-check. |
| `HD-DEV-003` | Per-device enumeration isolation | Individually inspect each candidate so one failure is attributable. | Crash prevention. |
| `HD-DEV-004` | Device instance ID | Capture canonical instance ID. | HidHide correlation. |
| `HD-DEV-005` | Friendly/product name | Capture human label. | UI. |
| `HD-DEV-006` | VID/PID | Capture where applicable. | Identity. |
| `HD-DEV-007` | Usage page/usage | Capture HID usage. | Gaming classification. |
| `HD-DEV-008` | Present/absent state | Resolve current presence. | Stale entries. |
| `HD-DEV-009` | Started/problem status | Capture PnP problem code. | Device health. |
| `HD-DEV-010` | Driver provider/version per device | Record device stack context. | External-driver issue correlation. |
| `HD-DEV-011` | Composite parent/child topology | Map when relevant. | HidHide selection semantics. |
| `HD-DEV-012` | Container ID/grouping | Use when useful for physical-device grouping. | Identity. |
| `HD-DEV-013` | Duplicate logical device candidates | Detect duplicate exposures/interfaces. | Isolation explanation. |
| `HD-DEV-014` | Phantom/stale device nodes | Identify absent historical nodes. | Enumeration noise. |
| `HD-DEV-015` | Access/open failure per device | Record exact device, code, operation. | Broken-device diagnosis. |
| `HD-DEV-016` | Descriptor/property query failure per device | Fault isolate malformed property. | Broken-device diagnosis. |
| `HD-DEV-017` | Unexpected property type/size | Treat as device-specific evidence, not crash. | Robustness. |
| `HD-DEV-018` | Virtual device classification | Identify known vJoy/virtual providers where evidence supports it. | Isolation protection. |
| `HD-DEV-019` | HOTAS rig membership correlation | When launched by HOTAS, map devices to saved rig identities. | Integration evidence. |
| `HD-DEV-020` | Unverified/ambiguous HOTAS identity | Expose ambiguity rather than guess. | Repair safety. |
| `HD-DEV-021` | Device reconnect requirement | Infer whether HidHide config change would require reconnect. | User-action planning. |
| `HD-DEV-022` | Device currently in-use evidence | Where safely obtainable. | Repair/reconnect planning. |
| `HD-DEV-023` | Broken device enumeration signature | Aggregate per-device failure into explicit finding. | Known issue correlation. |
| `HD-DEV-024` | Enumeration result stability | Bounded repeat only when needed to classify intermittent device. | Intermittence. |
| `HD-DEV-025` | XInput/XUSB device enumeration | Inspect relevant Xbox-style devices where applicable. | Upstream fix/context. |
| `HD-DEV-026` | HID raw vs high-level enumeration discrepancy | Compare two independent sources where useful. | Fault localization. |
| `HD-DEV-027` | Blacklisted device not enumerated by Doctor | Flag unresolved hidden entry. | Stale/corrupt config. |
| `HD-DEV-028` | Enumerated device missing from HidHide client path | Compare optional client/CLI evidence. | Client-specific bug. |

# Isolation Verification

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-ISO-001` | HidHide globally active for intended use | Interpret active state. | Readiness. |
| `HD-ISO-002` | Whitelist semantics allow feeder | Evaluate inverse + app list. | Readiness. |
| `HD-ISO-003` | HOTAS BF6 expected executable access | Determine effective exemption configuration. | Integration. |
| `HD-ISO-004` | Physical target devices selected for hiding | Compare intended vs actual. | Integration. |
| `HD-ISO-005` | Virtual outputs not unintentionally hidden | Protect vJoy/other output. | Integration. |
| `HD-ISO-006` | Required physical device still accessible to Doctor/HOTAS path | Read-only verification where possible. | Feeder viability. |
| `HD-ISO-007` | Game-facing visibility model consistency | Infer from config/device stack; avoid pretending direct game proof if unavailable. | Clarity. |
| `HD-ISO-008` | Persistent vs session strategy capability | Report whether current driver supports safer session hiding. | Future integration. |
| `HD-ISO-009` | Isolation readiness composite | Derived from component checks, never used as sole evidence. | UI summary. |
| `HD-ISO-010` | Partial isolation state | Identify some-but-not-all intended devices hidden. | Troubleshooting. |
| `HD-ISO-011` | Overbroad hiding state | Detect unrelated devices selected by HOTAS-owned intent if known. | Safety. |
| `HD-ISO-012` | Underbroad hiding state | Detect intended devices missing from hide list. | Functionality. |
| `HD-ISO-013` | Reconnect pending after hide-list change | Detect likely state where configuration changed but device not re-enumerated. | User action. |
| `HD-ISO-014` | HidHide client itself denied from hidden device view | Informational/context for GUI behavior. | GUI diagnosis. |
| `HD-ISO-015` | Session blacklist stale-impossible invariant | For supported driver, verify process-lifetime semantics assumptions from capability. | Advanced. |

# Windows Evidence

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-WIN-001` | Recent Application Error events for HidHideClient | Query bounded time window. | Crash classification. |
| `HD-WIN-002` | Recent Application Error events for HidHideCLI | Query bounded window. | Crash classification. |
| `HD-WIN-003` | Windows Error Reporting records for HidHideClient | Extract exception/module/code where available. | Crash evidence. |
| `HD-WIN-004` | Windows Error Reporting records for HidHideCLI | Extract evidence. | Crash evidence. |
| `HD-WIN-005` | System driver-load failures for HidHide | Bounded event query. | Driver diagnosis. |
| `HD-WIN-006` | PnP/device install events for HidHide | Bounded correlation. | Package/reboot diagnosis. |
| `HD-WIN-007` | Service Control Manager HidHide events | Bounded query. | Service diagnosis. |
| `HD-WIN-008` | SetupAPI.dev.log HidHide records | Bounded targeted parse. | Install diagnosis. |
| `HD-WIN-009` | MSI installer event correlation | Bounded targeted query. | Package diagnosis. |
| `HD-WIN-010` | Recent reboot/shutdown correlation | Correlate install/repair timestamps. | Pending replace diagnosis. |
| `HD-WIN-011` | Crash exception code classification | Preserve e.g. 0xc0000409 distinctly. | Known signature. |
| `HD-WIN-012` | Faulting module capture | Record ucrtbase/other module without over-interpreting. | Crash evidence. |
| `HD-WIN-013` | ETW manifest/provider registration | Detect whether deep trace can be offered. | Advanced evidence. |
| `HD-WIN-014` | Existing HidHide ETW trace artifact discovery | Only Doctor-owned/known locations. | Support evidence. |
| `HD-WIN-015` | Driver install return/reboot-required evidence | Parse known setup results. | Repair planning. |
| `HD-WIN-016` | Windows security block evidence | Only where explicit events support it. | Do not guess. |
| `HD-WIN-017` | File-in-use/replacement evidence | Correlate driver replacement. | Reboot planning. |
| `HD-WIN-018` | Event timestamp clock consistency | Validate correlation reliability. | Evidence quality. |

# Consistency / Correlation

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-X-001` | Client/driver/package version triad | Correlate all three. | Mixed install diagnosis. |
| `HD-X-002` | Package newer but loaded driver older + pending reboot | High-value incomplete replacement signature. | Repair planning. |
| `HD-X-003` | Driver running + control open fails | Localize control-device problem. | Diagnosis. |
| `HD-X-004` | Control open succeeds + one GET fails | Localize per-operation protocol failure. | Diagnosis. |
| `HD-X-005` | GET_WHITELIST 0x57 on 25H2 signature | Evaluate known #215-style signature, with version/build evidence. | Knowledge-base correlation. |
| `HD-X-006` | GUI/CLI fail while Doctor direct API succeeds | Classify user-mode client fault. | Repair/working-around client. |
| `HD-X-007` | Doctor API fails while service reports running | Driver/protocol incompatibility signature. | Diagnosis. |
| `HD-X-008` | dev-gaming/client fails + per-device Doctor finds one broken device | Broken enumeration signature. | Diagnosis. |
| `HD-X-009` | Configuration API values vs registry/provider persistence mismatch | Potential driver stale-state/corruption. | Advanced diagnosis. |
| `HD-X-010` | Blacklist references missing devices only | Stale configuration finding. | R1 candidate. |
| `HD-X-011` | HOTAS exempt + physical hidden + virtual visible | Healthy HOTAS isolation pattern. | Positive proof. |
| `HD-X-012` | HOTAS not exempt + physical hidden | Predict feeder denial risk. | Integration finding. |
| `HD-X-013` | Virtual output blacklisted | Predict game input loss. | Integration finding. |
| `HD-X-014` | Official client absent but driver healthy | Component/package issue without declaring driver dead. | Repair planning. |
| `HD-X-015` | Multiple driver-store packages + active older package | Stale/update context. | Repair planning. |
| `HD-X-016` | Recent update + version mismatch + no reboot | Incomplete update correlation. | Diagnosis. |
| `HD-X-017` | Signature invalid on active driver/package | Trust failure; block automated package manipulation until classified. | Safety. |
| `HD-X-018` | No qualified repair despite confirmed diagnosis | Explicit terminal state generation. | Truthfulness. |

# Knowledge-Base Signature Evaluation

| ID | Check | Required observation | Why it matters |
|---|---|---|---|
| `HD-KB-001` | Evaluate official known-signature set | Run deterministic signature rules. | Diagnosis enrichment. |
| `HD-KB-002` | Reject signature on missing mandatory evidence | Prevent false match. | Safety. |
| `HD-KB-003` | Reject/suppress signature on contradiction | Prevent false match. | Safety. |
| `HD-KB-004` | Record signature version/source | Auditability. | Support. |
| `HD-KB-005` | Record repair qualification level for matched signature | Never infer from diagnosis alone. | Repair safety. |


# 3. Derived Summary Objects

The UI may show summary health objects, but they are derived from checks and must never replace them.

Recommended summaries:

```text
System Environment
Installation
Package Integrity
Kernel Driver
Protocol/API
Configuration
Devices
Isolation
Windows Evidence
```

Each summary reports:

- Healthy / Degraded / Fault / Unknown
- completed check count
- important findings
- unresolved/blocked checks

# 4. Known-Signature Baseline

Initial knowledge-base signatures should include, at minimum:

## HD-KSIG-001 — Whitelist invalid-parameter failure on modern Windows

Required evidence should include:

- control device can be opened or driver is otherwise proven running;
- `GET_WHITELIST` fails with `ERROR_INVALID_PARAMETER (0x57)`;
- exact Windows build recorded;
- exact installed/loaded HidHide versions recorded.

The current upstream issue #215 is useful evidence for the existence of this failure family, but **does not by itself authorize a repair**.

## HD-KSIG-002 — Mixed client/driver installation pending reboot

Candidate evidence:

- client/package version differs from loaded driver;
- newer package exists/staged;
- replacement/reboot evidence exists;
- loaded old driver remains active.

This may become repairable only after package/reboot semantics are proven for the exact provider/version.

## HD-KSIG-003 — Broken device enumeration

Candidate evidence:

- HidHide client/CLI gaming-device enumeration fails or crashes;
- Doctor’s fault-isolated per-device enumeration identifies one or more problematic device instances;
- unrelated protocol/configuration checks remain healthy.

Repair path depends on actual cause and provider version.

## HD-KSIG-004 — User-mode client failure with healthy driver API

Evidence:

- Doctor direct control-device probes succeed;
- core configuration is readable;
- HidHideClient/HidHideCLI has existing crash evidence or bounded invocation fails.

This diagnosis may permit HOTAS BF6/Doctor to manage configuration without relying on the broken GUI, without pretending the GUI itself was repaired.

# 5. Implementation Rules

1. Checks are read-only.
2. Checks do not call repair operations.
3. One failed check never prevents unrelated checks unless a hard prerequisite is genuinely missing.
4. A check that cannot safely distinguish states returns `Unknown` rather than inventing one.
5. Native errors are evidence.
6. Timeouts are evidence.
7. A blocked check explains its prerequisite.
8. Catalog IDs are stable once implemented.
9. Additional checks may be appended without renumbering.
10. Any check that unexpectedly mutates system state is a defect and blocks release.

# 6. Catalog Coverage Count

This v1.1 baseline defines **208 mandatory checks** across the major HidHide/Windows diagnostic domains.

The count is intentionally large. The user should not have to run them manually; the Doctor does the tedious archaeology while the interface keeps the story understandable.


# v1.1 Diagnostic Catalog Amendment — Platform & Portability Checks

The following checks are mandatory additions to the catalog and run before architecture-sensitive HidHide diagnosis.

| ID | Check | Evidence / Result |
|---|---|---|
| HD-PORT-001 | Windows edition | Structured OS edition/product information |
| HD-PORT-002 | Windows major/minor/version | Structured version information |
| HD-PORT-003 | Windows build number | Numeric build |
| HD-PORT-004 | Windows revision/UBR | Numeric revision where available |
| HD-PORT-005 | Native machine architecture | x64 / ARM64 / other |
| HD-PORT-006 | Doctor process architecture | Native/emulated architecture |
| HD-PORT-007 | WOW64/emulation state | Structured process state |
| HD-PORT-008 | Supported diagnosis platform | Supported / limited / unsupported |
| HD-PORT-009 | Supported repair platform | Qualified repair level |
| HD-PORT-010 | Doctor binary architecture match | Pass/fail |
| HD-PORT-011 | Repair helper architecture match | Pass/fail/not installed |
| HD-PORT-012 | HidHide package architecture | x64 / ARM64 / unknown |
| HD-PORT-013 | HidHide driver architecture compatibility | Pass/fail/unknown |
| HD-PORT-014 | Clean/fresh-user prerequisites | No hidden dependency on owner state |
| HD-PORT-015 | Locale/culture | Informational; not used to determine core truth |
| HD-PORT-016 | UI scale / primary DPI | Informational for reproducibility |
| HD-PORT-017 | Relevant runtime dependencies | Present/missing/version |
| HD-PORT-018 | Structured Windows API availability | Capability list |
| HD-PORT-019 | HidHide protocol capability discovery | Capability list from probes |
| HD-PORT-020 | External utility dependence | Any required utility located/versioned |
| HD-PORT-021 | HOTAS BF6 presence | Optional integration state only |
| HD-PORT-022 | Doctor standalone-launch integrity | Pass/fail |
| HD-PORT-023 | Machine-specific path audit | No development-machine assumptions detected |
| HD-PORT-024 | Driver Store layout discovery | Dynamic resolved state |
| HD-PORT-025 | Installation-path discovery | Dynamic resolved state |
| HD-PORT-026 | Multi-package architecture conflicts | None / finding |
| HD-PORT-027 | Wrong-architecture remnants | None / finding |
| HD-PORT-028 | Unsupported OS explanatory path | Explicit finding/action |
| HD-PORT-029 | Unqualified future Windows build policy | Read-only/repair restrictions applied |
| HD-PORT-030 | Environment reproducibility fingerprint | Privacy-reviewed fingerprint generated |

## Platform test matrix requirement

The catalog SHALL be executed in automated or controlled qualification against multiple environment fixtures. A check that only has evidence from the development machine is not considered platform-qualified.

Minimum release matrix entries are tracked by OS build, architecture, HidHide state, account freshness, and repair capability tier.
