# Flight Deck Phase 8 Adaptive Response validation

## Scope and authority

Phase 8 is an alternate native Flight Deck presentation over candidate base
`4c20a05c86bf77e18f51bcf6c9ed273e6f821481`. It adds
`FlightDeckAdaptiveResponse.qml`, which is selected only while Flight Deck is
active. Standard, Legacy, and Top Gun continue to load the established
`AdaptiveResponsePage.qml`.

The page does not add or alter mapper, DirectInput, vJoy, predictor, profile,
or simulator semantics. It calls the existing `AppBackend` Adaptive Response
projection and commands. A source audit found the same 27 `backendObject`
Adaptive capability surfaces in the legacy and Flight Deck QML pages; no
legacy backend capability is missing from the Flight Deck destination.

## Product composition

- The top-level context makes category, profile, axis, editing layer, and the
  full effective runtime chain visible before a control is changed.
- Basic response gives the selected-axis enable switch and the existing preset
  set a spacious, immediate path. The normal page makes the current response
  understandable before exposing tuning.
- The static rapid-reversal preview and a separate A/B comparison use real
  preview data. Physical, predicted-mapped, final-output, and optional
  baseline traces remain distinct; the compact lead plot explains the mapped
  output difference without mixing incompatible units.
- Advanced Tuning groups every existing configuration property by Prediction,
  Motion Detection, Reversal and Settling, Coherent Motion, Safety and
  Limits, and Predictor Model. It retains the authoritative inherit/reset
  behavior.
- The native preset workshop retains save, edit, rename, duplicate, and
  dependency-aware delete. It deliberately edits the preset layer rather than
  activating a profile or changing runtime selection.
- Telemetry prioritizes physical input, predicted mapped output, final output,
  lead, horizon, confidence, and the backend motion state. Secondary motion
  values remain supporting instrumentation.
- One Response Lab changes only its input source between Live Controller and
  Interactive. It has bounded live-history inspection, source-rate and
  manual-input controls, record/replay, trace visibility, and separate axis,
  velocity, acceleration, and state displays. Internal traces stay behind
  Advanced Traces.
- Test Lab uses the existing synthetic scenarios and Test Lab values as a
  deliberate engineering surface; it does not write to a live configuration.
  The optional monitor is read-only and is explicitly pinnable.

## Runtime discipline

- No `src/` runtime or hot-path source changed. No report-rate QML dispatch or
  new logging was introduced.
- Live history refresh is bounded to the existing published history at a 33 ms
  display cadence and only while the analysis workspace is near the viewport.
  Its history is capped to the selected 2/5/10/30-second display window.
- The interactive simulator is isolated and capped at 900 display samples;
  preview recomputation happens on a configuration/context action, never per
  controller report. Hidden/off-viewport analysis timers are inactive.

## Validation and visual review

`app_qml_startup_tests` loads the native Flight Deck route, exercises physical
pointer interaction for context/axis, enable, presets, an advanced slider,
predictor selection, comparison, trace visibility, history inspection,
monitor, Test Lab, and the custom-preset lifecycle. It also proves axis and
profile isolation, observational live telemetry safety, source-switch safety,
and that Test Lab and trace controls do not change configuration.

When `HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR` is set, the test generates and
reviews both Light and Dark captures for basic, off, custom, static rapid
reversal, A/B comparison, advanced, preset workshop, advanced controls, live
analysis, interactive Test Lab and its bottom, unavailable long-name, 900 x
650 basic and static-preview, and wide states. The test also asserts
post-expansion ordering and scroll containment for Advanced, Telemetry,
Analysis, and Test Lab.

The final inspected evidence was generated in the test-owned
`artifacts/flight-deck-phase8-visual-v24` directory. It showed clean card
containment, graph contrast, distinct traces, no section overlap, and usable
900 x 650 graph geometry in both appearances. This host's offscreen Qt
renderer renders installed UI glyphs as boxes, so these captures prove layout,
color, trace, and containment—not actual desktop font rasterization. A native
desktop launch was intentionally not attempted because a separate active
mapper worktree must remain undisturbed.

## Boundaries

This work proves local source, QML loading, backend-command parity, bounded
presentation behavior, offscreen rendering, and test/build results. It does
not prove physical controller capture, DirectInput/Raw Input identity, vJoy
output, protected-main integration, a native desktop font render, or owner
acceptance. Day Ops was absent from this candidate baseline and remains a
separate integration concern.

## Completed local validation

- Release `HOTASMapper` and every registered test executable built from the
  Phase 8 candidate.
- The final focused native-QML validation passed after visual capture.
- Final full CTest passed 11/11, including the persisted-experience lifecycle
  regression check.
