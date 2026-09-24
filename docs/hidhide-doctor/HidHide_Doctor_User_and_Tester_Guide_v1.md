# HidHide Doctor: user and tester guide

HidHide Doctor is a separate, independently launchable Windows diagnostic tool
shipped beside HOTAS BF6. It observes HidHide, the driver package, service,
configuration, controller evidence, and related Windows evidence. It does not
need HOTAS BF6, a profile, a Device Rig, vJoy, a game, or any controller to
start.

## Opening Doctor

Launch `HidHide Doctor.exe` directly, or use **Open HidHide Doctor** in HOTAS
BF6 Devices, Diagnostics, or App Health. Opening it is non-elevated and does
not change HidHide. A HOTAS-initiated launch gives Doctor a small local intent
hint such as the selected Device Rig and expected vJoy output. Doctor always
checks the real system independently; the hint cannot make a missing device or
unhealthy component appear healthy.

If the paired Doctor binary is missing or does not match the HOTAS release,
HOTAS fails safely and Doctor shows a component mismatch before any repair can
be considered.

## Reading a scan

Doctor begins with bounded read-only checks. A status of **NO VERDICT** means
the available evidence is insufficient; it is neither healthy nor failed.
HidHide GUI trouble, a protocol error such as `GET_WHITELIST` returning
`ERROR_INVALID_PARAMETER`, and malformed HID enumeration are separate findings.
One does not prove the others.

## Repair qualification

Normal product mode exposes only FieldQualified repairs. LabQualified repairs
are not a consumer fix and cannot be enabled through an ordinary UI toggle.
An owner test may present a LabQualified plan, but the owner must deliberately
authorize the exact sealed plan. A UAC cancellation makes no change.

Package repair is especially conservative. If a verified rollback package is
not available, Doctor states **PACKAGE ROLLBACK UNAVAILABLE** before final
authorization. A configuration backup is not represented as a driver-package
rollback.

## Exporting support evidence

After a completed scan, use **Copy session** or **Export report**. The in-app
surface lets you choose the scope, Summary/Detailed/Forensic depth, Markdown,
JSON, plain text, or a local diagnostic bundle. **Safe to Share** is the
default and redacts personal paths and sensitive observation values; choose
**Local / Unredacted** only when the recipient and destination are approved.
The preview names included, redacted, and excluded categories before writing.
Nothing is uploaded. A local diagnostic bundle is a folder containing the
report, timeline, evidence, and manifest files; it is not a cloud upload or a
repair package.

For an external test campaign: launch Doctor, run the scan, inspect the
findings, choose Safe to Share unless an approved local exception is needed,
then export the bundle and send it with a screenshot only through the
owner-approved channel. Do not change Windows or HidHide state at random.
Execute a repair only when it is explicitly FieldQualified or the owner has
given a specific Lab test instruction.

## Privacy and restart handling

Doctor has no automatic telemetry or upload path. Restart-required work is
recorded as an observe-first continuation: after restart, Doctor reads actual
state before it considers any completion result. Uninstalling HOTAS BF6 does
not uninstall HidHide.
