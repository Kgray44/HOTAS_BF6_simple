# Flight Deck Phase 4 — Axes validation

## Baseline and isolation

- Source baseline: accepted Phase 3 commit
  `b783b11e5a29186ab75d7a3f3b73fcf2475e7bad`.
- Candidate branch/worktree: `codex/flight-deck-phase4-axes` in
  `C:\Users\kkids\Documents\HOTAS_BF6-flight-deck-phase4-axes`.
- `origin/main` did not contain the accepted Phase 3 commit at inspection, so
  no concurrent branch was merged or cherry-picked. The dirty primary checkout,
  Phase 3 artifacts, unrelated worktrees, and separately running V2.4 mapper
  process were left untouched.

## Native Axes composition

- `FlightDeckAxes` replaces only Flight Deck page 0; Standard and Legacy retain
  their existing Axes composition.
- Each card reads the existing `AppBackend.axes` snapshot and shows source,
  configured `vJoy` destination, normalized input, final virtual output when
  `virtualValid`, disabled/fixed/unavailable state, and concise transform and
  Adaptive Response status.
- The mapping selector uses the authoritative `virtualAxisChoices` list,
  including configured-but-unavailable entries. Mapping, conflict confirmation,
  learning, transform controls, aliases, and names all invoke established
  `AppBackend` commands; no Flight Deck mapping state exists.
- Expanded cards retain range mode, inversion, deadzone, hysteresis, output
  limits, curve family/strength/full-editor entry, and per-axis Adaptive
  Response deep link. The deep link selects the real axis before routing to the
  existing page.
- `FlightDeckResponsePreview` is bounded to 33 samples and rebuilds only on a
  configuration revision or expanded-axis change. It follows the existing
  static order: deadzone, inversion, curve, then output limits. It deliberately
  excludes hysteresis and the live Adaptive Response overlay.

## Scope limitation and recovery

The current mapper's axis model is authoritative for the selected controller,
not multiple physical devices feeding one profile concurrently. The page uses
per-axis source identity where future/current models provide it; otherwise it
states the selected-controller scope and routes controller selection to Devices
& setup. Unavailable output states use the existing vJoy readiness facts and
offer the existing Devices/setup route.

Day Ops is not present in this baseline, so its coexistence is pending the
later integration point. The native Flight Deck route and the retained
Standard/Legacy routes are covered here without claiming that absent mode.

## Validation record

- Focused `app_qml_startup_tests` passed after exercising a real pointer
  selection in the native mapping selector, independent Axis A/B/C routes,
  per-axis transform isolation, the existing Fast Adaptive Response preset,
  the Adaptive deep link, and native QML load/create coverage.
- Visual fixtures passed in Flight Deck Light and Dark: four/eight axes,
  per-axis multi-device labels, expanded response preview, unavailable output,
  long labels, disabled routes, and 900 × 650 / 1600 × 980 layouts. Captures
  are retained locally under `artifacts/flight-deck-phase4-visual/`; the
  software/offscreen host renders text as glyph boxes, so this proves geometry,
  clipping, scrolling, contrast, and state treatment rather than final
  platform font rendering.
- Release build passed for `HOTASMapper`; full Release CTest passed 11/11 in
  97.13 seconds (including `app_qml_startup_tests` in 95.99 seconds).
- HOTPATH: only QML, CMake QML registration, tests, and Flight Deck docs are in
  scope. No mapper, DirectInput, vJoy write, transform, or Adaptive Response
  runtime source is changed; a mapper benchmark is not required.

## Recommended Phase 5

Redesign Buttons as the next native Flight Deck workspace, reusing the same
authoritative routing, bounded live-state, and presentation-only fixture seams.
