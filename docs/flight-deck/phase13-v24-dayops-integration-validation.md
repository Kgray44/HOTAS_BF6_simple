# Flight Deck Phase 13 V2.4 / Day Ops integration validation

## Decision and provenance

Phase 13 is an isolated reconciliation of the accepted Flight Deck Phase 12
candidate with the released V2.4.0 source that introduced Device Rigs,
App Health, the production Day Ops theme, and current device-scoped
configuration rules.

| Source point | SHA | Meaning |
| --- | --- | --- |
| Current released V2.4.0 baseline | `6f7e7a2` | Current `origin/main`, and the peeled target of annotated tag `v2.4.0`; includes the V2.4 schema-release acceptance repair. |
| V2.4.0 Device Rigs source | `15f3f4067fdd2dcb6705659c944b0e1142f5d80e` | The preceding V2.4.0 Device Rigs and Setup Health product source. |
| Accepted Flight Deck Phase 12 | `fc18eb7a113d6f49c0512f2928d9cd2df0c0ccb0` | Existing accepted parity-integration worktree source. |
| Common merge base | `7bfa244e98d06219163c9c9cab4f562b60e0cf47` | Common ancestor used to audit both incoming changes. |
| Phase 13 candidate | Recorded by the commit that adds this document | Local-only integration candidate on `codex/flight-deck-phase13-v24-integration`. |

The candidate is based on current released V2.4.0, including its
schema-release acceptance repair, with Phase 12 merged into the isolated
worktree. It is not pushed, tagged, released, installed, or used in a live
mapper session. Existing user processes and the parent worktree were not
modified.

## Reconciliation result

The result retains five distinct selectable presentations:

| Presentation | Source of truth | Phase 13 result |
| --- | --- | --- |
| Legacy | Existing concrete legacy shell | Preserved unchanged outside the centralized selector model. |
| Standard | Existing Standard shell | Preserved as the normal non-Flight-Deck host, including V2.4 Devices and App Health routes. |
| Top Gun | Existing Standard token variant | Preserved as a token variant and selectable through the same model. |
| Day Ops | V2.4 production token variant | Preserved as a real `Day Ops` theme, with its V2.4 descriptive text and no Flight Deck aliasing. |
| Flight Deck Preview | Phase 12 alternate shell | Available only when the existing preview command-line gate is enabled. Its Light/Dark appearance remains separate presentation state. |

`ThemeManager.themeChoices()` now contains only the four real themes, while
`presentationChoices()` adds Flight Deck only for a preview-enabled run. The
Standard Settings selector consumes that centralized model, so the visible
choices, persisted selection semantics, and preview exclusion all agree. A
Flight Deck selection replaces the loader host; it does not activate a
profile, start mapping, alter a device rig, or clone backend state.

The V2.4 crash-presentation record, tray-theme update, Day Ops tokens,
Devices route, Device Rig editor, App Health deep links, and configuration
ownership remain on their V2.4 paths. Flight Deck reuses the same
`AppBackend`, `ThemeManager`, controllers, output layouts, profile store,
Automation engine, and Adaptive Response authority. No DirectInput,
MappingWorker, vJoy, HidHide, predictor, timing, allocation, or report-path
logic was added or changed for this integration.

## Device-scoped Automation finding and correction

V2.4 requires a physical Automation condition to resolve to exactly one
authoritative input device. The Phase 12 Automation fixture had been written
against the older unscoped default and initially failed after V2.4 merge.
Phase 13 preserves the production rule instead of weakening it: the Flight
Deck test first selects the fixture Device Rig's single stick editing context,
then saves the physical condition/action rules through the existing backend.
The fixture subsequently exercises the real Device Rig deletion transition so
later legacy-profile checks return to their genuine no-rig state.

This is test reconciliation only. The product Automation API, configuration
schema, compiler, and runtime remain V2.4 authorities.

## Validation

The candidate was configured with the repository Qt `6.8.3 msvc2022_64`
toolchain, the MSVC 2022 x64 environment, and Ninja in `b15`.

| Check | Result | Evidence |
| --- | --- | --- |
| Release application build | Pass | `cmake --build b15 --config Release --target HOTASMapper --parallel 4` completed and deployed the candidate-local Qt runtime. |
| Full CTest matrix | Pass | After merging the current V2.4 release-repair baseline, `ctest --test-dir b15 --output-on-failure -j 1` passed **12/12** in 114.20 s. |
| Flight Deck QML lifecycle | Pass | The final current-baseline `app_qml_startup_tests` pass completed in 99.37 s after the V2.4 device-context and Day Ops switch reconciliation. A separate visual-capture invocation also passed in 104.45 s. |
| Five-presentation lifecycle | Pass | Legacy, Standard, Top Gun, Day Ops, and preview Flight Deck run through the QML suite. The suite verifies Standard-to-Flight-Deck pointer selection and forward/back host switching through all four real themes. |
| Configuration invariance | Pass, local fixture | Cross-presentation checks compare profile/category, axes, buttons, POVs, Automation, Adaptive Response, controllers, Device Rigs, edit context, vJoy device, disabled-axis value, and mapping request state before/after the presentation switches. |
| Device Rig/App Health integration | Pass, local fixture | V2.4's Devices lifecycle, explicit single-device edit scope, Device Rig deletion transition, recovery/deep-link surfaces, and current Settings route remain in the same QML lifecycle traversal. |
| Flight Deck Dark/Light structure | Pass, local fixture | 146 QML captures were emitted: 73 Dark and 73 Light, covering overview, Devices, Axes, Buttons, Profiles, Automation, Adaptive Response, Diagnostics, Settings, dialogs, and narrow/wide states. |
| Conventional visual matrix | Pass, structural | 36 opt-in QML captures cover Legacy, Standard, Top Gun, and Day Ops Device Rig states and dialogs. |
| Native Windows QML visual review | Pass, local fixture | `b15\\app_qml_startup_tests.exe` was run directly with the CTest offscreen override removed, `HOTAS_QML_TEST_THEME=Day Ops`, and the Flight Deck visual-output hooks enabled. It exited 0 and wrote 9 Day Ops plus 146 Flight Deck Windows-rendered captures (73 Dark, 73 Light). |

The ordinary QML visual runs use Qt's `offscreen` CTest platform. On this host
that platform substitutes boxed glyphs despite successful layout and
interaction coverage, so those captures prove composition, bounds, color,
state, and route coverage only. The direct Windows-QPA fixture run above is
the rendered-text proof: its representative captures were visually inspected
at 1650x1050 in both Flight Deck appearances, at the 1125x813 narrow
Automation/Settings states, and on the Day Ops Device Rigs route. Examples
retained with this candidate are
`artifacts/phase13-native-flight-deck-review/flight-deck-dark-ready-normal.png`,
`artifacts/phase13-native-flight-deck-review/flight-deck-light-ready-normal.png`,
`artifacts/phase13-native-flight-deck-review/flight-deck-dark-automation-large-long-minimum.png`,
`artifacts/phase13-native-flight-deck-review/flight-deck-light-settings-minimum.png`,
`artifacts/phase13-native-dayops-review/devices-day-ops.png`, and
`artifacts/phase13-native-dayops-review/devices-day-ops-editing-target.png`.
The last is a deliberately scrolled Device Rig fixture state; its partial
viewport is not presented as a full-page fit claim.

The suite's Flight Deck switch stress now explicitly includes
`theme:Day Ops` as well as Legacy, Standard, Top Gun, and `flight-deck`. It
requires exactly one Flight Deck host while preview is selected, none while a
real theme is selected, and an unchanged authoritative configuration snapshot
on each transition.

## Scope boundaries and remaining owner-session evidence

1. This is a local integration candidate, not a protected-main merge, tag,
   release, public artifact, signing result, updater result, or owner
   acceptance.
2. The local QML suite proves app-owned route and state behavior. It does not
   prove physical controller capture, exact DirectInput-to-Raw-Input identity,
   live vJoy/HidHide behavior, or a running-game session.
3. The direct native Qt rendering run proves the captured app-owned Day Ops
   and Flight Deck states, but not OS-owned dialogs or owner-specific display,
   input, accessibility, or hardware conditions. Do not substitute the
   offscreen boxed-glyph images for this native-render evidence.
4. Windows pickers, UAC, vJoy/HidHide configuration tools, the uninstaller,
   and physical device ownership remain platform or owner-session operations.
5. The preview gate remains intact. No default profile, device, mapper start
   setting, or persisted production configuration was changed to expose Flight
   Deck.

## Candidate handoff

The only new behavioral reconciliation is the test fixture's use of the
existing V2.4 single-device Automation scope. The source merge is ready for
normal code review after the final full CTest run, direct native-render review,
and repository status audit. It remains local until the remaining owner-session
boundaries are consciously accepted.
