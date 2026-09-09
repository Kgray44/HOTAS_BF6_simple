# Flight Deck Phase 6 — Profiles and categories validation

## Baseline and isolation

- Source baseline: accepted Phase 5 commit
  `4a9997baa8f9defe538657df1658d523ebf3206f`.
- Candidate branch/worktree: `codex/flight-deck-phase6-profiles-categories` in
  `C:\Users\kkids\Documents\HOTAS_BF6-flight-deck-phase6-profiles-categories`.
- `origin/main` did not contain a successor to the Phase 5 baseline at
  inspection. The dirty primary checkout, other worktrees, generated data, and
  separately running mapper process were left untouched. This candidate is not
  merged, pushed, tagged, released, or promoted.

## Native Profiles composition

- Flight Deck page 5 is a native, category-first Profiles presentation. It
  reads the published `AppBackend.profiles`, `profileCategories`,
  `profileDetail`, active-profile/category values, and running-application
  snapshot. It adds no profile store, detector, inheritance engine, or mapper
  hot-path observer.
- `Active Now` remains the authoritative runtime state. A selected category or
  profile is explicitly labelled as a configuration being viewed; browsing is
  presentation-only. Only the existing explicit profile/category activation
  commands can change the active runtime profile.
- Category detail exposes executable association, configured/running state,
  detection enablement, category enablement, default-profile behavior, and
  restore-last behavior. The descriptions match the model: restore-last first
  uses the last active category profile, then default/another enabled profile;
  default behavior chooses the configured default when the category activates.
- Profile detail reports profile-local mapping summaries, output context,
  Automation relationships, profile references, and the actual Adaptive
  Response precedence (Global → Category → Profile). Mapping remains
  profile-specific; neither it nor Adaptive inheritance is recomputed by QML.
- Axes and Buttons are honestly labelled as existing active-profile editors.
  Their controls are disabled while viewing another profile and never activate
  it. Adaptive Response receives the selected profile as context without
  activation; Automation navigation opens the existing editor without running a
  rule. Import/export deliberately hands off to the existing Profile Library
  workflow.

## Safety and interaction coverage

- The release startup harness uses presentation-only fixtures for category,
  profile, game-running, Adaptive, Automation, long-name, empty-category, and
  selected-not-active states. The fixture never invokes an `AppBackend` command
  or game-detection action.
- Functional coverage uses the authoritative model to create profiles and
  categories, duplicate, move, rename, configure default/restore-last and game
  association, explicitly activate through a real pointer click, reject active
  profile deletion, then delete the temporary profiles and empty categories.
  It also verifies that viewing and each Profiles deep link leave activation
  unchanged.
- Light and Dark captures cover library, category detail, selected inactive
  profile detail, 900 × 650, and 1600 × 980 layouts in
  `artifacts/flight-deck-phase6-visual/`. Offscreen rendering on this host uses
  glyph boxes, so review proves layout, clipping, scrolling, state treatment,
  contrast, and responsive geometry—not final installed-font rendering or
  physical controller behavior.

## Validation record

- Release targets `HOTASMapper` and `app_qml_startup_tests` built successfully
  in `build-flight-deck-phase6-check2`.
- `app_qml_startup_tests` completed with exit code 0 under the offscreen Qt
  platform. The harness also retains Legacy, Standard, and Top Gun lifecycle
  coverage. Existing baseline QML warnings from unrelated legacy pages remain
  outside this candidate; the final run emitted no `FlightDeckProfiles.qml`
  runtime warning or error.
- After all configured test executables were built, full Release CTest passed
  11/11 in 72.79 seconds, including `app_backend_startup_tests` (12.35
  seconds) and `app_qml_startup_tests` (58.10 seconds).
- `qmlformat` was applied and `qmllint` reported no errors for
  `FlightDeckProfiles.qml`. Its unqualified-access advisories are the existing
  inline-component style used by the Flight Deck family, not runtime failures.
- HOTPATH: no DirectInput, `MappingWorker`, vJoy write, profile-trigger runtime,
  game-detection runtime, or Automation runtime source changed. No performance
  benchmark is needed for this low-frequency presentation/configuration work.

## Scope boundary and next phase

Day Ops is absent from the Phase 5 baseline and was not invented. Phase 6
remains preview-gated alongside Legacy, Standard, and Top Gun. A sensible next
increment is a native Flight Deck Automation workspace, preserving the same
authoritative-command and no-execution-on-navigation boundary.
