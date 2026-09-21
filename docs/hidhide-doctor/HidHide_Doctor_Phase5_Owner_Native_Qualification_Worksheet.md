# HidHide Doctor Phase 5 owner native-qualification worksheet

This worksheet records only a normal, read-only Doctor review. It does not
authorize a repair, elevation prompt, installer, restart, device-configuration
change, or release.

## Candidate receipt

Before starting, record the staged candidate folder, `VERSION`, component
manifest SHA-256, and the SHA-256 of `HidHide Doctor.exe`. Confirm that the
component manifest lists the four product executables and that all listed file
hashes verify. If the receipt does not agree, stop and report **NO VERDICT**.

## Native review matrix

For every row, record `PASS`, `FAIL`, or `NOT RUN`; include the observed
scaling value and a short note. Do not use an offscreen or fixture result as a
substitute.

| Review | Result | Evidence to record |
|---|---|---|
| Launch Doctor from **Devices** |  | Opens/focuses the paired staged Doctor once; no unexpected prompt or mutation. |
| Launch from **Diagnostics/App Health** |  | Same paired binary and one-process behavior. |
| Launch from **Flight Deck → Diagnostics** |  | Same paired binary and one-process behavior. |
| Read-only scan at the current Windows scaling |  | First visible progress, completion state, and whether the window remains responsive. |
| Interactive CPU contention: idle, moderate, and heavy |  | For each level, run `scripts/invoke-hidhide-doctor-interactive-contention.ps1` for its bounded interval, then record the JSON receipt, whether each normal read-only control reacts promptly, and any measured or observed delay. No repair, export, or device change. |
| Activity lifecycle selection and Inspector navigation |  | For one check ID with multiple Timeline entries, select at least two distinct lifecycle rows. Each selection must update the **Activity Context** card with its own event type, phase, status, time, and detail. The canonical evidence may be shared; that is expected. Verify the compact `↶` / `↷` controls navigate evidence and retain their clear Previous/Next accessible names. |
| Each available Windows scaling from 100% through 200% |  | Actual scaling value, readable text, no clipped controls, usable small-window behavior. |
| Keyboard-only route |  | Tab order reaches the primary controls; Space/Enter activates the intended control; Escape closes only the transient surface. |
| Accessibility semantics |  | Inspect accessible name/role with the owner’s available tool. If no screen reader is exercised, record `NOT RUN`, not `PASS`. |
| Doctor control-style consistency |  | Buttons, segmented controls, progress, timeline, focus indicator, and error/status presentation are visually coherent. |
| Five HOTAS themes |  | Record each actual theme label; launch/focus Doctor through each of the three entry points and confirm no visual or focus regression. |
| Practical HOTAS pass |  | With the owner’s safe physical setup, confirm no disruption to the normal mapping path. This is observational only; do not alter HidHide configuration. |

## Outcome

The owner should return this completed table together with the candidate
receipt and, if useful, a Safe-to-Share bundle. A `FAIL` or an unperformed
required row is a blocker, not a reason to improvise a repair.
