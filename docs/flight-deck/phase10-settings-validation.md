# Flight Deck Phase 10 Settings validation

## Scope and provenance

Phase 10 starts from the accepted local Flight Deck Phase 9 commit
`134ecd2f0b0c85da8bae09340c4243bf7bc4ab29` on the isolated branch
`codex/flight-deck-phase10-settings`. It is neither merged, pushed, tagged,
released, nor promoted.

`FlightDeckSettings.qml` is a native control-plane presentation over existing
`AppBackend` commands and `ThemeManager` presentation state. It has no mapper,
profile, controller, Automation, Adaptive Response, DirectInput, vJoy, or
HidHide implementation ownership.

## Native settings composition

- General: tray behavior, Devices & setup handoff, automatic verified-device
  switching, and preferred connected-device selection.
- Appearance: a single experience selector for existing Legacy, Standard, and
  Top Gun themes plus preview-gated Flight Deck; Flight Deck itself offers the
  actual Light and Dark appearances only.
- Startup, game, and updates: Start Mapping on Launch, Automatic Game
  Detection, Profiles handoff, update status/check/install.
- Advanced configuration: disabled axis value, curve smoothing/duration,
  selected vJoy device, vJoy configuration launch, layout creation/selection,
  managed visibility identity, HidHide status/configuration, and explicitly
  scoped maintenance confirmations.

## Isolation and lifecycle contract

`ThemeManager.presentationChoices` centralizes existing themes and marks
Flight Deck as Preview only when the existing preview gate is enabled.
Selecting a presentation changes only presentation state. The native-QML test
uses real pointer sequences for Settings toggles, appearance controls,
Profiles navigation, Flight Deck-to-Standard card selection, and
Standard-to-Flight-Deck selector selection. It snapshots controller/profile,
route, Automation, Adaptive Response, vJoy-selection, and mapping-request
configuration across each presentation transition.

The test also repeats Legacy, Flight Deck, Top Gun, Flight Deck, Standard, and
Flight Deck selection to prove exactly one active presentation host and no
stale Flight Deck shell. No settings test starts mapping, creates a profile,
changes HidHide, or substitutes telemetry.

## Visual review matrix

With `HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR` set, the native-QML test renders
Dark and Light Settings captures for main, appearance, startup, disabled
advanced controls, long values, 900 x 650, and wide layouts. The fixture-only
long update and vJoy messages exercise wrapping and containment; production
always reads the authoritative values.

The expected review checks are grouping hierarchy, obvious primary versus
secondary actions, Hover/pressed/focused/disabled control treatment, Light and
Dark readability, scroll containment, and 900 x 650/wide layout resilience.
Offscreen captures are layout evidence only; they do not prove native desktop
font rasterization, physical devices, DirectInput/Raw Input identity, vJoy
output, protected-main integration, release state, or owner acceptance.

## Completed local validation

- Release build completed with the repository Qt 6.8.3 MSVC toolchain.
- `qmllint` completed without errors for the changed Settings/Devices QML;
  its unqualified-access advisories are the established QML style warnings.
- `theme_manager_tests` passed in 0.09 seconds, including preview gating,
  centralized presentation choices, and configuration isolation checks.
- The focused `app_qml_startup_tests` passed in 76.89 seconds with regenerated
  Dark and Light Settings captures. It exercised native pointer toggles,
  both Flight Deck appearances, Profiles handoff, Flight Deck-to-Standard,
  preview-gated Standard-to-Flight-Deck, repeated cross-experience selection,
  and the configuration snapshots around those transitions.
- Final full CTest passed 11/11 in 87.57 seconds; the native-QML lifecycle
  regression passed in 74.30 seconds during that gate.

The Qt offscreen renderer substituted box glyphs for the installed UI fonts.
The reviewed captures therefore prove layout, hierarchy, control states,
scrolling, Light/Dark contrast, and containment rather than desktop font
rasterization. No foreground mapper process, physical controller, or vJoy
output path was started or claimed as evidence.
