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
| Axis routing and processing | Axes workspace; shared mapping/configuration commands | Axes; a control-to-game-output overview before advanced editor controls | Primary; unavailable/fixed inputs, route conflict, disabled/unsupported output, live/calibrated/output state, learn/quick-map |
| Axis calibration | Calibration workspace and controller-scoped calibration state | Devices & setup with a contextual calibration entry point | Primary/contextual; range/center stages, success/failure, history, selected/offline controller |
| Response curves | Curve editor; shared curve state and compiled LUT configuration | Axes deep link / advanced Curve editor | Advanced; preset/custom/point editing, undo/redo, comparison, live graph, validation and profile scope |
| Button and POV routing | Buttons workspace; shared route and native-POV commands | Buttons; physical-control-to-output overview | Primary; press/live output, disabled, destination conflict, learn/quick-map, hat directions/native continuous or discrete routes |
| Profiles and categories | Profile Library, `ProfileModel`, trigger runtime | Profiles; activation, automatic source, and category context primary | Primary; create/clone/rename/delete/move/duplicate, enabled/default/last active category, hold/toggle override, controller/layout compatibility |
| Import and export | Profile Library portability flows and `ProfilePortability` | Profiles > Import/export, with preview before apply | Advanced/contextual; `.hbf6profile`/`.hbf6pack` validation, dependency preview, conflict handling, calibration opt-in |
| Game association / detection | Profile Library and low-frequency trigger runtime | Profiles > Automatic activation | Automatic; automatic detection enabled/disabled, executable match transitions, manual base profile and runtime hold/toggle precedence |
| Automation | Automation workspace and `AutomationEngine` | Automation; readable active rules first, editor through deliberate entry | Advanced; master switch, invalid-rule health, draft/unsaved/delete states, priorities, timing, axis/button/POV/profile conditions, mapping/profile/vJoy/axis/Adaptive effects |
| Adaptive Response | Adaptive Response workspace and shared adaptive configuration | Adaptive Response; scope, enabled state, preset, and live health primary | Primary/advanced; global/category/profile contexts, safe-off state, custom preset dependencies, live/static lab source, telemetry, validation |
| Diagnostics and event log | Diagnostics workspace and `AppBackend` snapshots | Diagnostics; unhealthy or unusual state first, raw detail progressive | Diagnostic; raw/calibrated/virtual axes, buttons/POVs, event log, update age, capacity, HidHide, Automation timing, Adaptive telemetry, warnings |
| Global application and mapper settings | Settings; configuration store and launcher handoff | Settings; grouped application, mapping, controller, device-hiding, update, and maintenance controls | Advanced; tray behavior, start on launch, output layout, disabled-axis value, curve transition smoothing, update state, destructive confirmations |
| System tray and close behavior | `AppBackend`, native tray menu, Main window close handler | Remains platform-owned; Flight Deck does not replace it | Automatic/contextual; open, mapping toggle, close-to-tray, exit, tray availability |
| Dialogs, flyouts, confirmations, and notifications | Standard/Legacy shells and page components | Native setup-repair confirmation and undo confirmation within Flight Deck Devices; preserve other established behavior | Contextual; learning, conflicts, create/rename/delete, setup/recovery, import preview, tooltips, empty/loading/error/validation states |

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
