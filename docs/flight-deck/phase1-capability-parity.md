# Flight Deck Phase 1 capability parity matrix

## Scope and baseline

Flight Deck is an alternate presentation of the existing HOTAS BF6 state,
commands, services, and validation. It must not introduce a parallel mapper,
profile store, controller manager, Automation engine, or Adaptive Response
engine.

The protected snapshot inspected for Phase 1 exposes three selectable theme
labels: Legacy, Standard, and Top Gun. Day Ops is the fourth existing product
theme under concurrent V2.4.0 implementation and was not present in this
baseline. This record deliberately does not invent its absent implementation;
Flight Deck remains a separately gated alternate UX shell rather than a member
of the established theme family.

| Capability | Current location / authority | Flight Deck destination or strategy | Class and variants that must remain reachable |
| --- | --- | --- | --- |
| Mapper health and active configuration | Overview; `AppBackend` mapping, controller, vJoy, profile, and telemetry properties | Overview primary status and persistent readiness card | Primary; mapping live/off/suspended, controller absent/stale, vJoy ready/offline/capacity warning, active profile |
| Controller inventory and setup | Settings, controller setup dialog, `ControllerManager` and readiness model | Native Flight Deck Devices & setup; health summary first, repair and details contextual | Primary; connected/new/verified/offline/ambiguous device, calibration required, HidHide/vJoy attention, repair/progress/rollback/reconnect |
| Axis routing and processing | Axes workspace; shared mapping/configuration commands | Native Flight Deck Axes scan view with expanded routing, response, limits, advanced, Adaptive Response handoff, and the shared learning/quick-map entry | Primary; unavailable/fixed inputs, route conflict, disabled/unsupported output, live/calibrated/output state, learn/quick-map |
| Axis calibration | Calibration workspace and controller-scoped calibration state | Devices & setup with a contextual native calibration dialog over the existing controller-scoped commands | Primary/contextual; range/center stages, success/failure, history, selected/offline controller |
| Response curves | Curve editor; shared curve state and compiled LUT configuration | Axes deep link / advanced Curve editor | Advanced; preset/custom/point editing, undo/redo, comparison, live graph, validation and profile scope |
| Button and POV routing | Buttons workspace; shared route and native-POV commands | Native Flight Deck Buttons scan view with expanded physical-button and hat-direction configuration plus shared learning/quick-map entry points | Primary; press/live output, disabled, destination conflict, learn/quick-map, profile hold/toggle controls, mapping controls, Automation relationships, hat directions/native continuous or discrete routes |
| Profiles and categories | Profile Library, `ProfileModel`, trigger runtime | Profiles; activation, automatic source, and category context primary | Primary; create/clone/rename/delete/move/duplicate, enabled/default/last active category, hold/toggle override, controller/layout compatibility |
| Import and export | Profile Library portability flows and `ProfilePortability` | Profiles > Import/export, with preview before apply | Advanced/contextual; `.hbf6profile`/`.hbf6pack` validation, dependency preview, conflict handling, calibration opt-in |
| Game association / detection | Profile Library and low-frequency trigger runtime | Profiles > Automatic activation | Automatic; automatic detection enabled/disabled, executable match transitions, manual base profile and runtime hold/toggle precedence |
| Automation | Automation workspace and `AutomationEngine` | Automation; readable active rules first, editor through deliberate entry | Advanced; master switch, invalid-rule health, draft/unsaved/delete states, priorities, timing, axis/button/POV/profile conditions, mapping/profile/vJoy/axis/Adaptive effects |
| Adaptive Response | Adaptive Response workspace and shared adaptive configuration | Native Flight Deck Adaptive Response: scope and axis context, enable/preset decisions, explanatory static preview/A-B comparison, grouped advanced tuning, prioritized telemetry, unified live/interactive analysis, custom preset workshop, read-only monitor, and Test Lab | Primary/advanced; global/category/profile/preset contexts, safe-off state, all current tuning controls, custom preset dependencies, live/static source, bounded history/trace inspection, telemetry, simulation, validation |
| Diagnostics and event log | Diagnostics workspace and `AppBackend` snapshots | Native Flight Deck Diagnostics: centralized readiness, real signal path, live inspection, performance, and progressive technical detail | Diagnostic; raw/calibrated/virtual axes, buttons/POVs, event log, update age, capacity, HidHide, Automation timing, Adaptive telemetry, warnings, and redacted controller diagnostics export |
| Global application and mapper settings | Settings; configuration store and launcher handoff | Settings; grouped application, mapping, controller, device-hiding, update, and maintenance controls | Advanced; tray behavior, start on launch, output layout, disabled-axis value, curve transition smoothing, update state, destructive confirmations |
| System tray and close behavior | `AppBackend`, native tray menu, Main window close handler | Remains platform-owned; Flight Deck does not replace it | Automatic/contextual; open, mapping toggle, close-to-tray, exit, tray availability |
| Dialogs, flyouts, confirmations, and notifications | Standard/Legacy shells and page components | Native setup-repair confirmation and undo confirmation, controller calibration, and shared Input Learning within Flight Deck; preserve other established behavior | Contextual; learning, conflicts, create/rename/delete, setup/recovery, import preview, tooltips, empty/loading/error/validation states |

## Cross-cutting preservation rules

- Existing command and validation ownership stays in `AppBackend` and the
  associated models/services. Flight Deck may reshape display only.
- Live UI reads the existing published snapshots. No Flight Deck observer,
  timer, allocation, lock, serialization, or UI dispatch belongs in the
  DirectInput-to-vJoy report path.
- Multiple connected controllers and a selected controller are already
  represented by the backend; no shell-level one-device assumption is allowed.
- Color never carries the whole status: labels such as `LIVE`, `STANDBY`,
  `WAITING`, and `OFFLINE` remain visible with semantic lamps.

## Phase 4 axis presentation coverage

- Flight Deck Axes reads the existing `AppBackend.axes` bounded presentation
  snapshot and `virtualAxisChoices` model. It does not retain a second mapping
  or output-value model.
- The scan view exposes physical source, configured virtual destination,
  normalized input, final virtual output when the existing snapshot safely
  publishes it, disabled/fixed/unavailable state, curve/deadzone/inversion
  summary, and per-axis Adaptive Response state.
- Expanded cards use the established immediate-apply commands for route,
  inversion, deadzone, range mode, hysteresis, output limits, labels, curve,
  learning, and the full Curve Editor. The static preview is a bounded
  settings-change calculation; it does not predict the live Adaptive Response
  overlay.
- This baseline's mapper axis model represents the selected physical
  controller. Flight Deck presents that scope honestly and directs users to
  Devices & setup to select another connected controller; it does not invent
  simultaneous multi-controller routing.

## Phase 5 button and POV presentation coverage

- Flight Deck Buttons reads the existing bounded `AppBackend.buttons`,
  `povs`, and `povInputs` presentation snapshots plus authoritative vJoy,
  profile-trigger, mapping-control, native-POV, and Automation models. It
  does not retain a second binding or action model.
- Assigned physical buttons use compact, expandable cards with live
  pressed/released state, a physical source label, configured output/profile/
  mapping-control/Automation summary, and the existing immediate-apply
  configuration commands. Unassigned physical buttons remain compact chips
  that open the same focused editor.
- The editor preserves the current custom button name, vJoy route and conflict
  choices, profile target with Hold/Toggle behavior, mapping-control action,
  and clear/unassign paths. It intentionally does not invent a generalized
  action type or new press/release semantics.
- Profile and Automation entries are relationships to the existing systems.
  Their deep links carry presentation selection only; they do not activate a
  profile, execute an Automation rule, or simulate a button press.
- Hats stay explicit as existing discrete POV directions, including diagonals
  when the authoritative `povInputs` model exposes them. Their selected
  direction can use the existing vJoy route/profile trigger controls; optional
  native continuous or discrete vJoy POV output remains a separate existing
  configuration path.
- Like Axes, the current Button/POV model is the selected-controller scope.
  Flight Deck directs controller selection and recovery to Devices & setup
  rather than claiming simultaneous multi-controller mapping.

## Phase 6 profiles and categories presentation coverage

- Flight Deck Profiles is a native category-first configuration surface over
  the existing `AppBackend.profiles`, `profileCategories`, `profileDetail`,
  running-application snapshot, and immediate-apply profile/category commands.
  It does not retain a second profile store, activation state, game detector,
  or inheritance implementation.
- The Active Now hero reads the existing authoritative active category/profile
  and profile-source values. Opening a category or profile sets presentation
  selection only; the explicit Activate Profile and Activate Category actions
  alone invoke the established activation commands.
- Category cards and detail expose the actual executable rules, configured but
  not-running state, automatic-detection switch, enabled state, and the two
  existing category behaviors: restore the last-used profile or select a
  category default. The existing portability flow remains reachable through a
  deliberate handoff rather than a partial import/export rewrite.
- Profile cards and detail use low-frequency configuration summaries for axes,
  buttons, POV routes, Automation relationships, output context, and Adaptive
  Response override sources. Axes and Buttons stay honest about their current
  active-profile editor constraint; Adaptive Response accepts a presentation
  profile target without activating it.
- Profile create, duplicate, rename, move, enable/disable, and delete retain
  existing model rules. Delete remains unavailable for an active or protected
  profile; category delete remains limited to an empty, inactive category that
  leaves another category in place.

## Phase 7 Automation presentation coverage

- Flight Deck Automation reads only the existing bounded `AppBackend`
  Automation projection and uses the established create, duplicate, delete,
  save, and enable commands. `AutomationEngine` remains the sole compiler and
  execution authority.
- Rule cards distinguish actual active, enabled, disabled, incomplete, and
  needs-attention states from the authoritative rule health and summaries.
  Search and All/Enabled/Disabled filters are presentation-only.
- The deliberate editor exposes every existing condition and action enum,
  one-to-four `WHEN` and `DO` entries, all/any matching, activation lifetime,
  priority, and the existing condition/action-specific fields. It does not add
  game conditions, category actions, new trigger semantics, or a parallel
  rule model.
- Button and profile links carry selection context into the native surfaces;
  they neither activate a profile nor execute a rule. Day Ops was absent from
  this baseline and remains a coexistence integration item rather than a
  claimed Phase 7 theme implementation.

## Phase 9 Diagnostics presentation coverage

- Flight Deck Diagnostics reads the established `AppBackend` bounded
  presentation snapshots and the shared `FlightDeckReadiness` model. It does
  not introduce a second health calculation, device model, telemetry store,
  report observer, timer, or backend command.
- The health-first summary names Physical Input, Mapping Runtime, Virtual
  Output, Isolation, Profile, Game, Automation, and Adaptive Response using
  the existing readiness and diagnostic state. The signal path renders the
  selected-controller scope and the actual route data published for each
  axis; it does not claim a simultaneous multi-controller route.
- Details retain raw/calibrated/final-output axis values, buttons, POVs,
  event history, update and latency metrics, vJoy capacity, HidHide state,
  Automation health, and Adaptive telemetry. Redacted Copy Diagnostics keeps
  the existing backend export rather than creating a second report format.
- All recovery controls are presentation handoffs to the native Devices,
  Axes, Profiles, Automation, or Adaptive Response workspaces. They do not
  write configuration, toggle mapping, start output, change isolation, or
  mutate a profile.
- Day Ops was absent from this baseline. Its later coexistence and launch
  integration remains explicitly pending; this native Diagnostics destination
  neither replaces nor simulates it.

## Phase 10 Settings presentation coverage

- `FlightDeckSettings.qml` is the native Settings destination only while the
  preview-gated Flight Deck experience is active. Standard, Legacy, and Top
  Gun retain their established Settings page; the preview gate remains owned
  by `ThemeManager` and the existing command-line preview path.
- General controls use the existing tray, remembered-controller selection, and
  verified-controller auto-switch commands. Devices & setup is a navigation
  handoff, while verified controller records retain their existing individual
  Forget action in native Devices.
- Appearance has one central `ThemeManager.presentationChoices` model: Legacy,
  Standard, and Top Gun are existing themes; Flight Deck is marked Preview and
  offered only when preview is enabled. Flight Deck's actual Light and Dark
  appearances remain separate presentation storage. This baseline provides no
  System color mode and no Day Ops choice, so neither is simulated.
- Startup and game controls retain Start Mapping on Launch, Automatic Game
  Detection, and the real Profiles handoff. Update status/check/install uses
  the existing update commands. Mapping defaults retain disabled-axis value,
  curve-transition smoothing, and duration behavior including the disabled
  duration control.
- Virtual Output retains selected vJoy device, configuration launch,
  five-axis output layout creation, layout selection, and exact adopted
  visibility identity. HidHide retains status refresh and configuration
  launch. Maintenance retains separate confirmation-scoped commands for
  controller records, device calibration, application configuration, and the
  uninstaller.
- The Settings surface introduces no configuration store, mapper mutation
  path, device polling, telemetry observer, or DirectInput-to-vJoy hot-path
  work. Its display-only exceptional-state property exists solely for
  deterministic QML visual fixtures and is empty in production.

## Phase 11 secondary UI presentation coverage

| Secondary surface | Existing authority and trigger | Flight Deck treatment | Ownership / validation state |
| --- | --- | --- | --- |
| Profile/category create, rename, duplicate, move, delete | `AppBackend` profile/category commands from `FlightDeckProfiles.qml` | Shared `FlightDeckDialog`; backend rejection stays inline and the dialog closes only after success | Native; actual pointer tests cover rejected form and delete cancel/confirm |
| Profile import/export | `ProfilePortability` through `AppBackend` portability commands | `FlightDeckTransferDialog` presents profile/category/pack selection, preview, conflicts, device choice, calibration opt-in, and result banner | Native; schema, extension, compatibility, and contents unchanged; Windows picker is platform-owned |
| Game association | Existing category executable-rule commands and running-application snapshot | Native dialog offers detected running application, existing executable picker, or manual executable rule | Native; detection algorithm and automatic-activation semantics unchanged |
| Axis route conflict | Existing route command result in `FlightDeckAxes.qml` | Shared attention dialog with cancel / deliberate override | Native; no route changes until confirmation |
| Button/POV output conflict | Existing Button/POV route commands | Shared attention dialog with existing Share / Replace choices | Native; existing selector command path retained |
| Automation deletion | Existing `deleteAutomation` command | Shared restrained destructive confirmation | Native; referenced profiles and controls are not deleted |
| Setup repair/undo | Existing controller-readiness commands | Shared attention dialogs retain scoped-plan explanation and authoritative apply / undo actions | Native; UAC remains platform-owned |
| Settings maintenance | Existing forget, calibration reset, configuration reset, and uninstall commands | Shared confirmation with fault treatment only for configuration reset/uninstall | Native; uninstaller and configuration panels stay platform-owned |
| Adaptive preset rename | Existing adaptive-preset command | Shared form dialog with inline backend rejection | Native; predictor, preset, and runtime behavior unchanged |
| Combo popups and explanatory tooltip | Existing QML selector models and preset descriptions | Token-based rounded popups across native pages; shared `FlightDeckTooltip` for application-owned preset help | Native; selection values remain existing commands |
| Loading, empty, unavailable, validation, and result states | Existing bounded presentation snapshots and command status | Existing page-specific states remain; Profiles adds an in-page result banner rather than a new global notification service | Native where app-owned; there is no existing standalone toast service |

`FlightDeckDialog.qml` is the common modal surface for the real consumers
above. `FlightDeckTransferDialog.qml` remains deliberately specialized because
the existing portability service exposes broad preview and pack-selection
state. Neither contains a mapper, profile store, portability format, game
detector, or validation engine.

Intentional exceptions are Windows file/folder pickers, UAC elevation,
vJoy/HidHide configuration applications, and the uninstaller. Flight Deck
styles the application surface before and after those platform operations; it
does not emulate them. The established Curve Editor remains a separately
routed advanced primary workspace, not a secondary modal. Legacy, Standard,
and Top Gun retain their prior dialog/popup implementations. Day Ops was
absent from this baseline and remains a later coexistence integration item.

## Phase 12 parity integration coverage

- `FlightDeckInputLearningDialog.qml` is a Flight Deck-native presentation of
  the established `AppBackend.inputLearning` transaction. Axis, button, POV,
  and quick-map entry points dispatch to the existing start/retry/cancel and
  conflict-resolution commands; it does not sample hardware reports or retain
  a competing mapping model. The destructive button-map reset remains a
  deliberately confirmed existing backend command.
- Devices & setup now presents the existing calibration stages, ranges, and
  history with the existing start, center, save, and reset commands. It makes
  no claim that an unavailable controller can be calibrated.
- The Flight Deck embedding bridge transfers only a learning request into its
  presentation-owned dialog. Standard, Legacy, and Top Gun preserve their
  established dialog lifecycles, while an embedded Standard background is not
  left in Flight Deck's pointer stack.
- The Phase 12 validation record in
  `phase12-parity-integration-validation.md` is the source-of-truth evidence
  for cross-workflow coverage, native visual limits, fixes, and the remaining
  owner-session checks. Day Ops remains absent from this source baseline.
