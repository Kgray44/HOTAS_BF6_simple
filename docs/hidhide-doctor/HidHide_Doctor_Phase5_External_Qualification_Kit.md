# HidHide Doctor Phase 5 external-qualification kit

## Purpose and boundary

This kit qualifies the normal, read-only diagnostic route on a separate
machine. It does **not** authorize R1–R5 repair, elevation, an installer,
HidHide configuration changes, a restart, or a public release.

## Candidate receipt before execution

1. Extract the supplied candidate into its own folder; do not mix it with a
   previous stage.
2. Check `VERSION`, `HidHideDoctor-ComponentManifest.json`, and
   `SHA256SUMS.txt`. Compute the manifest SHA-256 and verify every manifest
   file by relative path, size, and SHA-256.
3. Record the `HidHide Doctor.exe` SHA-256 and the candidate-kit manifest
   SHA-256. If either receipt check fails, stop with **NO VERDICT**.

## Tester matrix

Use one result row for each distinct machine/OS/locale/architecture
combination. Report only the OS edition/build, architecture, locale, and
display scaling—never raw host, user, device-instance, or installation paths.

| Gate | PASS condition | Result / note |
|---|---|---|
| Unpack and receipt validation | All supplied manifest receipts agree. |  |
| Normal launch | Doctor starts without UAC and displays its read-only boundary. |  |
| Read-only scan | Progress becomes visible, scan completes, and no configuration changes occur. |  |
| Safe-to-Share export | A new timestamped child bundle contains `evidence.json`, `manifest.json`, `report.json`, `report.md`, and `timeline.json`; JSON is nonempty and parseable. |  |
| Keyboard and basic controls | Tab/Space/Enter/Escape behave as described in the owner worksheet. |  |
| Display scale | Record real scale and any clipping, overflow, or focus defect. |  |
| Entry point, if HOTAS is installed | Devices, Diagnostics/App Health, and Flight Deck → Diagnostics each select the paired candidate. |  |
| Driver condition | Record only `healthy`, `absent`, `partial`, `permission limited`, or `other`; never try to repair it. |  |
| Crash/fault evidence | Record app exit/result and whether a new Doctor crash artifact appeared. Do not attach sensitive event-log content. |  |

## Return receipt

Return a single folder named `HidHideDoctor-Phase5-Qualification-Receipt-<UTC>`
containing:

- this completed matrix as `result.md`;
- `candidate-receipt.txt` with candidate version and the two SHA-256 values;
- a Safe-to-Share bundle only if the tester is comfortable sharing it; and
- optional redacted screenshots.

The coordinator verifies that the result folder contains a file manifest and
returns an acknowledgement naming the received manifest SHA-256. A missing
matrix, failed receipt, or untested mandatory row is **NOT READY**, not an
implicit pass.
