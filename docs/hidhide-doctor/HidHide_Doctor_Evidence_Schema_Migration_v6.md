# HidHide Doctor evidence schema migration v6

## Scope

Phase 5 forensic reconstruction promotes provider facts into the canonical
`EvidenceRecord` that feeds the Activity timeline, Inspector, reports, and
diagnostic bundles. It does not add a repair path, elevation, network upload,
or a dependency on the DirectInput → MappingWorker → vJoy hot path.

## Version map

| Artifact | Earlier reader | New writer | Compatibility |
|---|---:|---:|---|
| `DoctorDiagnosticEngine::serializeJson` | 5 | 6 | Additive. A v5 reader may retain its existing root/check/summary fields and ignore the new forensic objects. |
| `DoctorReportComposer` report and bundle manifest | 1 | 2 | Additive. A v1 reader may ignore detailed evidence fields and activity metadata. |

Both roots declare `schemaCompatibility`: the minimum prior reader and the
version that introduced forensic evidence. No existing stable field changes
meaning or is removed.

## New canonical evidence fields

Each record can carry source display name and provider, subsystem, operation,
method/API, target identity/display, expected and observed state, status
rationale, start/completion timestamps, a monotonic microsecond duration,
timeout, labelled fields, bounded attempts, and backlinks to checks, findings,
diagnoses, and peer evidence. Protocol records additionally preserve endpoint,
access, IOCTL identifier, request/response byte counts, retry count, bounded
payload sample, and native error data.

The Inspector renders these as labelled groups—Identity, Observation, Target,
Method, Timing, Native Result, Relationships, Technical, and Raw—rather than
depending on a generic free-text fallback.

## Bounds and privacy

The canonical record retains at most 48 labelled fields. If a future provider
would exceed that bound, it records the original count and an explicit
truncation reason; it never copies an unbounded raw provider payload. Activity
history is bounded to 600 retained events and inserts an explicit truncation
marker with the omitted-event count. A bounded protocol payload sample retains
at most three entries plus the total count.

Safe-to-Share redaction is applied per field and per attempt using the existing
`EvidenceSensitivity` classification. The redacted Inspector copy commands,
report, JSON, and diagnostic bundle all derive from the same canonical record;
there is no separate export-only evidence reconstruction.

## Owner-review fixture

The explicit `Forensic Evidence Review` development fixture is labelled as
simulated data and brings together a healthy and a failed protocol request,
semantic Boolean state, trusted package signature, running driver service,
device-property error, Event Log, WER, SetupAPI, a derived contradiction, and
a recovery-transaction observation. It exists only for native Inspector review
of the canonical pipeline; it cannot run a repair or replace owner-machine
evidence. The domain test asserts that each family reaches a labelled evidence
field and that the resulting recovery diagnosis is linked through canonical
evidence rather than fixture-only UI state.

## Backward-reading guidance

Support tools should treat absent forensic fields as an older shallow record,
not as a healthy or empty observation. They should continue reading legacy
`summary`, `technicalDetails`, `durationMs`, and `nativeError` where present.
For v6/v2 records, prefer `fields`, `attempts`, `relationships`, and the
explicit collection metadata. A missing payload sample is not evidence that no
payload existed; it may be absent, sensitive, or intentionally bounded.
