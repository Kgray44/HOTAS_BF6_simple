# Flight Deck Phase 7 Automation validation

## Scope

Phase 7 is a Flight Deck presentation over the accepted Phase 6 baseline
`b096fe2d679fa6a98f3551900cfb402e2c30a9ad`. It reuses the existing
`AppBackend` Automation projection, commands, compiler, persistence, and
`AutomationEngine`; no mapper, input-capture, or runtime-worker source is
changed.

`FlightDeckAutomation.qml` is loaded only by the Flight Deck shell. Legacy,
Standard, and Top Gun retain the existing Automation page. The page source is
packaged as a QML resource so the Flight Deck can load it without changing the
legacy page registration path.

## Product behavior covered

- The Automation landing surface presents the existing master enable switch,
  total and active-rule counters, filter/search controls, and rule summaries.
- Cards use authoritative condition/action summaries and health fields; an
  active marker appears only when `AppBackend` reports the rule active.
- Create, duplicate, delete, save, and enable/disable use the existing backend
  commands. The interaction coverage pointer-tests duplicate, rename/save, and
  delete confirmation against an isolated copied rule. Drafts remain invalid
  until the existing compiler accepts them.
- The native `WHEN`/`DO` editor supports all current condition types 0–16 and
  action types 0–16, one to four entries on each side, all/any matching,
  while-active/toggle/brief activation, priority, and existing type-specific
  values.
- Profile and button links set presentation selection only. They do not call a
  profile activation command or inject input into Automation.

## Fixture and visual matrix

`app_qml_startup_tests` captures these Automation states for both Dark and
Light Flight Deck appearances when `HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR` is
set: editor deep link, mixed active/disabled/attention list, enabled filter,
disabled filter, attention search, empty state, and a 18-rule long-name
collection at normal, 900 x 650, and 1600 x 980 layouts. This is an 18-state
visual matrix. The narrow and wide captures also assert that the Automation
Flickable's content width matches its viewport. Fixtures are display-only and
do not alter Automation execution.

The actual offscreen captures were reviewed for state treatment, card geometry,
spacing, and narrow/wide containment. This host's offscreen Qt renderer drew
the installed UI fonts as missing-glyph boxes, so they do not establish native
desktop text rasterization; a rendered desktop review remains required for
typographic sign-off.

The focused QML interaction coverage pointer-selects both a `WHEN` button and
a `DO` virtual button, saves the target rule, verifies A/B/C isolation, checks
enable isolation and zero active rules without input, iterates every condition
and action enum, persists a two-condition `ANY` rule, and confirms new-rule
creation is execution-safe.

## Boundaries and follow-up

This work proves local source, offscreen QML, backend-command, and build/test
behavior. It does not prove physical controller input, vJoy output, rendered
hardware behavior, or owner acceptance. Day Ops was not present in the Phase 6
baseline, so its coexistence remains pending integration. The recommended next
Flight Deck slice is Adaptive Response, Diagnostics, or Settings; it should
reuse their existing authority in the same way.

## Completed local validation

- Release `HOTASMapper` build succeeded with the Automation resource packaged.
- The focused `app_qml_startup_tests` passed after the visual matrix capture.
- Full CTest completed 11/11 passing after all registered test executables
  were built.
