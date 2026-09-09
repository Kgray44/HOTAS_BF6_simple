# Flight Deck Phase 9 Diagnostics validation

## Scope and provenance

Phase 9 is an isolated native Flight Deck Diagnostics presentation on branch
`codex/flight-deck-phase9-diagnostics`. The named accepted Phase 8 branch tip
was `4c20a05c86bf77e18f51bcf6c9ed273e6f821481`. Its accepted Flight Deck
Adaptive Response source was present as uncommitted work in its isolated
worktree, so it was byte-verified and carried into this candidate as local
snapshot commit `a6fe80970dfdca6f7b3dfbf702d714031ab91dfd`; the original
worktree was not changed. Phase 9 is not merged, pushed, tagged, released, or
promoted.

`FlightDeckDiagnostics.qml` is selected only in Flight Deck mode. Standard,
Legacy, and Top Gun retain the established inline Diagnostics page.

## Native diagnostic composition

- System Health is a centralized, health-first summary over the shared
  `FlightDeckReadiness` model. Physical input, mapping runtime, virtual output,
  isolation, profile, game association, Automation, and Adaptive Response use
  published state; no parallel health calculation was introduced.
- Signal Path shows the actual selected-controller route data from
  `AppBackend.axes`: physical axes, configured virtual destinations, and the
  availability/routing/final-output state that the snapshot makes safe to
  display. It deliberately does not claim simultaneous multi-controller
  routing.
- Live inspection preserves the existing raw, calibrated, and final-output
  axis states, buttons, POVs, Adaptive telemetry, event history, update age,
  latency summaries, input/vJoy rates, vJoy capacity, HidHide readiness, and
  Automation health. Technical Details keeps the existing redacted Copy
  Diagnostics export; it does not surface raw HID paths or a new report.
- Recovery affordances are navigation-only handoffs to Devices, Axes,
  Profiles, Automation, and Adaptive Response. They do not configure output,
  modify HidHide, toggle mapping, activate profiles, or invoke Automation.

## Runtime discipline and boundaries

- No `src/` mapper, DirectInput, MappingWorker, vJoy, HidHide, profile,
  Automation, Adaptive Response, or game-detection source changed. The work
  contains QML presentation, QML registration, native-QML lifecycle coverage,
  and documentation only.
- Diagnostics adds no hot-path callback, controller-report observer, polling
  timer, lock, allocation, serialization, or logging. It consumes the
  existing UI-safe snapshots.
- Day Ops is absent from this baseline. Its eventual coexistence/launch
  contract is pending and is not claimed by Phase 9.

## Validation and visual review

`app_qml_startup_tests` selects the Flight Deck Diagnostics route and uses
display-only fixture snapshots to verify healthy, attention, and unavailable
route states. It checks the exact Roll-to-X, Pitch-to-Z, and Yaw-to-Rz route
data, the shared output/isolation health tones, section navigation, attention
filtering, technical disclosure, and a pointer-driven Devices recovery route.
Before and after each diagnostic interaction it compares the active profile,
compiled runtime axis routes, Automation rules, controller inventory, and
Adaptive context to prove the surface is observational.

With `HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR` set, the test produces Dark and
Light healthy, attention, routing-problem, advanced-details, signal-path,
output-technical, adaptive-technical, 900 x 650, and wide captures. The
reviewed captures show contained cards, readable state-color separation,
healthy/attention/fault differentiation, and no narrow or wide overlap. This
host's Qt offscreen renderer draws installed UI glyphs as boxes, so the
captures prove layout, hierarchy, color, scroll containment, and state
treatment—not native-desktop font rasterization. A desktop launch was not
used; no active mapper process was present and no physical input or vJoy proof
is claimed.

## Completed local validation

- `qmllint` completed without errors for `FlightDeckDiagnostics.qml` (the
  existing QML style reports unqualified-access warnings).
- Release `HOTASMapper` and every registered test executable built successfully
  from the final candidate.
- Focused `app_qml_startup_tests` passed with regenerated visual capture in
  73.70 seconds.
- Final full CTest passed 11/11 in 93.57 seconds, including the native-QML
  lifecycle regression in 80.28 seconds.
- One first post-build visual invocation stopped before Diagnostics in an
  inherited Adaptive Response test assertion: its test-only suspended-mapping
  fixture observed a machine-ready vJoy bit. The injected live samples were
  present; the unchanged candidate passed the immediate retry and the final
  full CTest gate. No production output behavior was changed to mask it.

## Evidence boundary and next slice

This work proves local source composition, QML loading, observational pointer
behavior, existing-backend diagnostic parity, offscreen layout, and local
build/test evidence. It does not prove physical controller capture,
DirectInput/Raw Input identity, vJoy output, actual desktop glyph rendering,
protected-main integration, release state, or owner acceptance.

The recommended Phase 10 slice is native Flight Deck Settings over the
existing command/validation ownership, with any later Day Ops coexistence
handled as an explicit integration decision rather than inferred from this
Diagnostics work.
