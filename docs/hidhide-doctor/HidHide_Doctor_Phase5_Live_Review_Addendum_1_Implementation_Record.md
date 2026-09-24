# HidHide Doctor Phase 5 live-review addendum 1 — implementation record

## Implemented candidate changes

- One core `DoctorReportComposer` now supplies all report copy/export data.
  It emits Markdown, JSON, plain text, and a local diagnostic-bundle directory
  from the complete `DoctorSession`, rather than from a visible QML pane.
- Inspector **Copy** and `Ctrl+C` compose the selected evidence record (the
  record selected by a finding, diagnosis, or activity row), while pane headers
  copy their complete underlying sections and the top action copies the full
  session. No copy action scrapes truncated visible QML text.
- Safe to Share is the default privacy choice. The composer redacts evidence
  whose sensitivity is not `SafeToExport`, records included/redacted/excluded
  manifest entries, never uploads, caps each output file at 8 MiB, and writes
  atomically.
- The command workspace restores persisted split fractions once, persists only
  after a user releases a divider, and has an explicit reset epoch instead of
  a width-change feedback loop. The extraneous Plan and Findings horizontal
  scrollbars (the source of the small custom-scrollbar artifacts) were removed.
- The Activity Timeline and Evidence Inspector are co-resident in a bottom
  dock; the same inspector is inline in Focus. When both are open on a wide
  window, their 40/60 initial split has a user-draggable divider whose released
  fractions are persisted. This replaces the former presentation overlay.
- Displayed progress is driven at a 33 ms presentation cadence. Engine values
  remain authoritative, and estimated motion is bounded below completion.
  Density typography is 112%, 100%, and 92% for Comfortable, Compact, and
  Dense respectively.

## Focused automated evidence

- `hidhide_doctor_domain_tests` passed with report schema, Safe-to-Share
  redaction, manifest, scope, and diagnostic-bundle assertions.
- `hidhide_doctor_standalone_startup_smoke` passed.
- `hidhide_doctor_layout_tests` passed with a 10-second post-drag stability
  hold, dual-bottom-dock coexistence and divider persistence, and rendered
  typography/density checks.

## Deliberate non-claims

This is an integration candidate, not owner acceptance. The tests do not prove
native desktop visual quality, screen-reader behavior, visual capture review,
user-perceived timing, copy selection semantics, or hardware/repair behavior.
The owner visual review remains required. No repair, elevation, merge, tag, or
release was performed under this addendum.
