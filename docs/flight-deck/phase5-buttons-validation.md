# Flight Deck Phase 5 — Buttons, hats, and live input validation

## Baseline and isolation

- Source baseline: accepted Phase 4 commit
  `76184c38568d35473e1fb961cdcc5795db0bb67f`.
- Candidate branch/worktree: `codex/flight-deck-phase5-buttons` in
  `C:\Users\kkids\Documents\HOTAS_BF6-flight-deck-phase5-buttons`.
- `origin/main` did not contain the accepted Phase 4 commit at inspection. The
  dirty primary checkout, existing Phase 4 worktree, generated artifacts,
  unrelated worktrees, and separately running V2.4 mapper process were left
  untouched. This candidate is not merged, pushed, tagged, or released.

## Native Buttons composition

- `FlightDeckButtons` replaces only Flight Deck page 1. Standard, Legacy, and
  Top Gun retain their established Buttons page and dialogs. The Flight Deck
  header delegates its existing Learn Route and Quick Map actions to those
  established controls.
- The page reads `AppBackend.buttons`, `povs`, and `povInputs`, together with
  the backend's published output choices, profile-trigger choices and modes,
  mapping-control choices, native POV choices, and Automation rules. Rendering
  adds no runtime action dispatch.
- The page first shows richer assigned physical-button cards, followed by
  compact unassigned chips that open a focused editor. All/Assigned/Unassigned
  filters and text search only filter the presentation. The cards show the
  current bounded pressed and virtual-pressed indicators; the UI does not
  synthesize input or feed state back to the mapper.
- Expanded button controls use existing `setButtonCustomName`,
  `setButtonMapping`, `setProfileTrigger`, and `setMappingControl` commands.
  Existing mapping conflict choices remain explicit: cancel, intentionally
  share a button route, or replace a conflicting route. Clear maps only the
  selected physical button to the existing disabled route.
- Profile controls expose only the existing profile target and Hold/Toggle
  modes. The Profile deep link carries a profile ID through the existing host
  and selects it on the existing Profile Library page; navigation does not
  activate the profile.
- Automation is displayed as a relationship discovered from existing physical
  button or POV Automation conditions. It is not presented as a fabricated
  direct button action. Its deep link carries a rule ID to the existing
  Automation editor and does not execute the rule.
- Hats remain native POV controls: the card renders the authoritative discrete
  directions, including diagonals when they exist, and highlights the current
  direction. Direction editors invoke existing `setPovMapping` and
  `setPovProfileTrigger` commands. Native continuous/discrete vJoy POV output
  remains the separate existing `setNativePovOutput` path.
- The baseline exposes a selected-controller Button/POV snapshot, not a
  simultaneous multi-controller mapping surface. Devices & setup remains the
  recovery and controller-selection route.

## Test seams and safety

- `HOTAS_STARTUP_TESTING` adds a bounded UI-snapshot fixture only to the
  `app_qml_startup_tests` target. Its source is preprocessor-excluded from the
  shipping mapper and does not enumerate a device, write DirectInput reports,
  start vJoy, or invoke button/profile/Automation runtime behavior.
- Visual fixtures are QML presentation overrides. They cover assigned and
  unassigned controls, a live pressed state, long control/device/profile text,
  32 controls, an Automation relationship, a diagonal-capable hat, and native
  POV availability without changing hardware discovery or production routing.
- The functional fixture creates an incomplete, disabled Automation draft only
  to prove presentation navigation, then removes it. It has no conditions or
  effects, cannot enter the runtime Automation set, and cannot execute an
  output action.

## Validation record

- Focused `app_qml_startup_tests` exercises Flight Deck Light and Dark button
  page creation; a real pointer selection of Button 2 to vJoy Button 7;
  Button 1/2/3 isolation; targeted clear/unassign; profile and Automation
  presentation deep links; a real pointer selection of Hat 1 Up while
  preserving Hat 1 Up-Right; compact filtering; and retained Legacy, Standard,
  and Top Gun lifecycle coverage.
- The production `HOTASMapper` Release target built successfully. After the
  complete Release test graph was built, full CTest passed 11/11; this included
  `app_backend_startup_tests` (12.32 seconds) and
  `app_qml_startup_tests` (46.84 seconds).
- Light/Dark captures are generated locally in
  `artifacts/flight-deck-phase5-visual/` for normal mixed, large-count, hat
  editor, 900 × 650, and 1600 × 980 states. On this offscreen host, text is
  rendered as glyph boxes; review therefore proves geometry, clipping,
  scrolling, contrast, density, and state treatment—not final platform font
  rendering or physical-controller behavior.
- HOTPATH: no production DirectInput, mapper, button edge/timing, Automation
  runtime, or vJoy-write source changes are in scope. A mapper performance
  benchmark is unnecessary for this presentation-only candidate.

## Recommended Phase 6

Build the native Flight Deck Profiles or Automation workspace next, reusing
these presentation-only deep-link seams rather than expanding Buttons into a
second editor or action model.
