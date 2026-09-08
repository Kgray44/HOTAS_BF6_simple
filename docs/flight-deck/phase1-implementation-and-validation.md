# Flight Deck Phase 1 implementation record

## Architecture

`ThemeManager` now carries three presentation-only concerns:

1. `currentTheme` remains the existing Legacy/Standard/Top Gun token choice.
2. `currentExperience` selects an application shell. `Existing` preserves the
   prior behavior; `Flight Deck` selects the alternate shell.
3. `flightDeckAppearance` selects intentional `Light` or `Dark` Flight Deck
   semantic resources.

Flight Deck is gated behind the explicit development launch flag
`--flight-deck-preview`; `--flight-deck-preview-isolated` enables the same
shell with test-mode settings for local visual verification. Without either
flag its identity cannot be selected and the ordinary UI exposes only the
existing experience. The persisted keys are under `presentation/`; no mapper,
profile, calibration, automation, or device configuration key is read or
written by the new presentation selection.

`FlightDeck.qml` owns the alternate rail/workspace composition. It embeds
`Standard.qml` with its existing chrome hidden, retaining the sole page host,
dialogs, routes, backend commands, and presentation-state preservation. This
is a narrow presentation seam rather than a business-logic fork.

## Foundation delivered

- Separate light/dark semantic token resource with application, navigation,
  surface, text, accent, health, information, attention, fault, selected,
  disabled, and focus roles.
- Named spacing, radius, control-size, and motion scales.
- Reusable Flight Deck surface, card, navigation item, and labelled status
  chip primitives with hover, selected, keyboard-focus, disabled, and
  semantic-state treatment.
- Alternate rounded rail/workspace shell with selected real routes and a
  backend-bound multi-controller/readiness card.
- Theme-manager isolation regression coverage and a QML light/dark shell
  route/selected-state contract that renders normal, minimum (900 × 650), and
  expanded (1600 × 980) shell captures for review.

## Intentional Phase 2+ work

- Redesign each page body and the dialogs for Flight Deck rather than using
  the established page body inside the new shell.
- Add a coherent user-facing experience selector only when enough page bodies
  have been redesigned to avoid an incomplete mixed presentation.
- Decide the eventual product treatment for the baseline’s absent fourth
  pre-Flight-Deck experience; it was not created or inferred in this phase.
- Design advanced views, density modes, and per-page accessibility review
  against the future page compositions.

## Validation record

Validated on the isolated candidate worktree from `origin/main`
`7bfa244e98d06219163c9c9cab4f562b60e0cf47`:

| Check | Result |
| --- | --- |
| Release build | Passed: all configured executable and test targets built. |
| Focused theme isolation | Passed: `theme_manager_tests` confirms preview gating and that presentation changes leave mapper/profile payload values unchanged. |
| Flight Deck shell | Passed: `app_qml_startup_tests` exercises Dark and Light, real routes for Overview, Axes, Profiles, and Adaptive Response, selected navigation state, and the embedded established page host. |
| Minimum layout | Passed at 900 × 650: the rail uses a constrained-layout scroll path and the test reaches the readiness card. |
| Visual inspection | Captured and reviewed Dark/Light at 1320 × 840, 900 × 650, and 1600 × 980. |
| Full local suite | Passed: 11/11 Release CTest targets in 38.99 seconds. |
| Existing experience coverage | Passed by the retained Legacy, Standard, and Top Gun lifecycle coverage in `app_qml_startup_tests`. |

The Phase 1 change does not touch `MappingWorker`, DirectInput acquisition,
transforms, or Adaptive Response runtime processing, so no hot-path benchmark
is required for this presentation-only change. Visual capture used the
isolated startup test rather than launching another mapper alongside the
already active physical-control process.
