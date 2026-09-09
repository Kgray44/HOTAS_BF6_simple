# Flight Deck Phase 12 parity and integration validation

## Decision and provenance

Phase 12 closes the app-owned Flight Deck parity gaps found while reconciling
the accepted Phase 11 source with the established Standard surfaces. The work
is isolated to `codex/flight-deck-phase12-parity-integration`.

| Source point | SHA | Meaning |
| --- | --- | --- |
| Accepted baseline | `275154a85142c601b348f8efe74602a91754f243` | Accepted Phase 11 source. |
| Phase 12 candidate and final product source | `5008c5a6fea9359ad6d3c1a7ef371cdbe3413936` | Complete app/QML/test/evidence candidate; no later commit changes product source. |

The candidate preserves the Flight Deck preview gate and uses the existing
`AppBackend`, `ThemeManager`, configuration, portability, controller, and
Adaptive Response authorities. It adds no DirectInput, MappingWorker, vJoy,
HidHide, predictor, or runtime-path behavior. No merge, tag, push, release,
or production-process intervention is part of this validation.

This document is evidence of a local acceptance candidate, not a release or
physical-device certification.

## Reconciliation method

The audit compared the phase baseline matrix, the Phase 10 and Phase 11
validation records, the actual QML import/resource graph, and the
`AppBackend` properties/commands consumed by established Standard pages.
It then exercised the Flight Deck through `app_qml_startup_tests` using actual
QQuickWindow pointer press/release paths where a native application control is
available. Bounded test fixtures configure only existing backend state; they
do not inject DirectInput reports, start the mapper, or replace any service.

The focused static backend comparison found the following existing properties
or commands not named by Flight Deck pages before this phase:

| Existing authority | Reconciliation |
| --- | --- |
| `inputLearning`, learn/retry/cancel/conflict commands, and quick targets | Native shared Input Learning now serves Axes, Buttons, POV, and Quick Map. |
| Controller-scoped calibration commands and state | Native Devices calibration dialog now serves the existing flow. |
| `resetButtonMappings` | Reachable through a deliberately two-step native confirmation. |
| Active controller/profile/layout names and bounded overview metrics | Already represented by Flight Deck readiness, overview, Devices, Profiles, and Diagnostics projections; no duplicate projection was added. |
| Tray theme, hide-to-tray, mapping toggle, updater status, and vJoy recommendation | Platform/global behavior remains owned by the application, native tray/menu, and established Settings/Overview routes. |

No app-owned orphan capability was left by the Phase 12 comparison. Windows
pickers, UAC, vJoy/HidHide configuration panels, the uninstaller, and actual
hardware remain intentional platform or owner-session boundaries.

## Capability and cross-workflow acceptance

| Area | Acceptance route | Result |
| --- | --- | --- |
| Preview gate and experience switching | `ThemeManager.presentationChoices`, command-line preview semantics, and Legacy/Standard/Top Gun lifecycle cycling | Flight Deck is still offered only as Preview; established experiences retain their own hosts and dialogs. |
| Overview, Devices, Axes, Buttons, Profiles, Automation, Adaptive Response, Diagnostics, Settings | Native page selection and authoritative snapshot checks in the Flight Deck QML startup suite | All Phase 1 primary destinations load and retain their existing backend authority. |
| Axis learn and quick map | Native Axes learn/Quick Map pointer entries route to shared Input Learning; Escape cancels without a configuration mutation | Covered with the existing bounded axis fixture; no synthetic input report is claimed. |
| Button learn, POV learn, and Quick Map | Native buttons/POV entry routes, existing control-plane state, and shared learning dialog | Covered; physical report completion remains hardware-owned. |
| Button-map reset | Shared dialog's explicit two-step confirmation invokes the existing reset command only after confirmation | Covered without a silent destructive action. |
| Calibration | Native Devices entry opens stage/status/range/history and invokes only existing start/center/save/reset commands | Covered without inventing an offline-controller success state. |
| Profiles, categories, import/export, and game association | Existing native dialogs and portability preview routes | Phase 11 behavior remains reachable; schema, compatibility, conflict, and calibration-opt-in authority remain in `ProfilePortability`. |
| Presentation/deep links | Devices, Axes, Buttons, Profiles, Automation, Adaptive Response, Diagnostics, and Settings page transitions | Selection context changes presentation only unless an explicit existing command is pressed. |
| Appearance and state consistency | Flight Deck Dark/Light matrices plus cross-experience lifecycle checks | Appearance is Flight Deck-owned presentation state; no mapper/profile/controller state is duplicated. |
| Adaptive Response | Axis/context selector, presets, advanced controls, static/live/interactive source, monitor, Test Lab, and preset workshop | Existing predictor/configuration authority retained; a Response Lab text-layout defect was fixed. |
| Day Ops coexistence | Source and baseline review | Day Ops is absent from the audited baseline. No destination, visual state, or integration claim was fabricated. |

Persistence is covered at the configuration/service level by the existing
backend and mapping-core test targets; this phase adds no storage or migration
format. The Flight Deck suite verifies that page/appearance and experience
transitions re-read the canonical backend state rather than a shell cache.

## Fixed integration defects

| Severity | Finding | Resolution |
| --- | --- | --- |
| P2 | Flight Deck exposed learning affordances that could fall through to embedded Standard lifecycle instead of retaining Flight Deck presentation ownership. | Added `FlightDeckInputLearningDialog.qml` and a narrow Standard-to-Flight-Deck dispatch bridge. Legacy, Standard, and Top Gun behavior remains unchanged outside the embedded Flight Deck route. |
| P2 | The native Devices surface named calibration but did not carry the full existing stage/status/history command flow. | Added a Flight Deck-native calibration dialog backed only by existing calibration commands/state. |
| P2 | Button/POV learning, Quick Map, and the existing button-map reset lacked complete native Flight Deck routes. | Added real entry points, shared learning operation routing, and a confirmation-scoped reset. |
| P2 | The embedded Standard surface retained an otherwise redundant background in Flight Deck's pointer stack. | Hide that background only when Standard is embedded; standalone Standard is unchanged. |
| P2 | Response Lab status text could collide with adjacent copy at constrained native window widths. | Added bounded layout widths and elision. |
| P2 | Adaptive preset buttons derived their width from an anchored `contentItem`, producing repeated native Windows QML polish-loop warnings. | Replaced the circular implicit-size dependency with a fixed responsive preset-control width; labels continue to wrap/elide within the Flow. |

## Visual and interaction evidence

- The offscreen visual matrix completed with **146 PNGs**: 73 Dark and 73
  Light. It covers the page matrix, dialogs, narrow/wide layouts, Adaptive
  states, Diagnostics, and Settings routes.
- A native Windows Qt run captured the Flight Deck Dark pointer route,
  including the native Axis Learning dialog. Typography rendered with the
  expected system and telemetry families; the offscreen plugin's boxed glyph
  rendering is therefore not treated as a product typography result.
- The native Windows run captured 73 Dark-route images after the adaptive
  preset sizing correction without a `FlightDeckAdaptiveResponse.qml:2010`
  polish-loop warning. Its native ComboBox-popup coordinate boundary is
  recorded below rather than being disguised as offscreen or product proof.
- Product screenshots and interactions use actual QQuickWindow pointer
  press/release events. Programmatic signals are used only where a physical
  report is a prerequisite, preserving the difference between UI routing proof
  and physical-device proof.

## Remaining P3 owner-session checks

1. The detached Adaptive Response monitor and native ComboBox popup interaction
   must receive a short owner-session check under the normal application event
   loop. The Windows test binary constructs its harness before that loop and
   its QTest pointer coordinates can be sensitive when a popup is promoted to
   a native window. This is not bypassed by a fake invocation, nor is it
   presented as physical or native product acceptance.
2. Windows file/folder pickers, UAC elevation, vJoy/HidHide configuration
   applications, and the uninstaller require a user-authorized OS session.
   Flight Deck correctly hands off before and resumes after those surfaces; it
   does not emulate them.
3. Physical DirectInput/controller behavior, exact device identity, and live
   vJoy/HidHide effects require the target hardware and an owner-controlled
   mapper session. Existing mapper processes were neither stopped nor used as
   substitute evidence.
4. Day Ops must be reconciled only when a real Day Ops implementation enters
   the source baseline; Phase 12 intentionally does not pre-claim it.
5. The exhaustive synthetic lifecycle still reports pre-existing Phase 11 QML
   reference diagnostics in Legacy, Standard, Diagnostics, and the established
   hat-editor component scope. The new POV Learn control reads its target
   directly from the hat card and does not add that scope dependency. Attempts
   to repair the established selector context changed its verified route
   interaction, so its owner must address it as a focused secondary-UI repair
   with dedicated behavioral coverage rather than fold it into this parity
   candidate.

## Validation record

The candidate was configured in a clean short build directory with Qt
`6.8.3 msvc2022_64` and the MSVC 2022 x64 environment. The final code build
completed for `app_qml_startup_tests` after the QML polish-loop correction.

`ctest --test-dir C:\\hotas-builds\\fd12 -C Release --output-on-failure`
passed **11/11** targets (about 94.94 seconds): configuration, mapping core,
input-learning, UI control plane, profile portability, theme manager,
controller management, devices, backend startup, QML startup, and theme
manager QML. The QML startup target completed in 81.60 seconds and exercises
the native Flight Deck page, dialog, lifecycle, cross-page, and pointer routes.

The final visual run used the same QML startup target with
`HOTAS_FLIGHT_DECK_VISUAL_OUTPUT_DIR` set to a fresh evidence directory; it
emitted 146 PNGs (73 per appearance); its final elapsed time is recorded with
the evidence directory (84.88 seconds).
The Windows-native typography/pointer run is supplemental evidence, not a
replacement for the passing offscreen CTest result. QML lint was not recorded
as a pass because a bare invocation cannot resolve this application's generated
module/resource imports; the compiled QML test target is the authoritative
validation for this change.

## Phase 13 scope boundary

Phase 13, if authorized, is limited to owner-session acceptance of the
secondary monitor and OS-owned surfaces, target-device identity/physical input
evidence, and future Day Ops reconciliation when its source exists. It must
not be treated as permission to change mapper hot paths, predictor behavior,
release state, or protected-main history.
