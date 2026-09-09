# Flight Deck Phase 11 secondary UI validation

## Scope and provenance

Phase 11 starts from accepted Phase 10 commit
`b26c0c14afca93700105ece04530c744e7262b85` on the isolated branch
`codex/flight-deck-phase11-secondary-ui`. It is local-only: no merge, push,
tag, release, promotion, or mapper process action is part of this phase.

The candidate changes presentation QML, its QML resource declarations, and
native-QML lifecycle coverage. `DirectInput`, `MappingWorker`, vJoy output,
HidHide logic, profile runtime, Automation runtime, Adaptive runtime,
game-detection algorithm, and portability serialization are unchanged.

## Inventory and ownership

The Phase 1 parity ledger now records every reachable Flight Deck secondary
surface identified in the target inventory:

- Profile/category forms and confirmations; game association; import/export.
- Axis and Button/POV route conflicts.
- Automation delete; Adaptive custom-preset rename.
- Devices setup repair/undo; Settings maintenance confirmation.
- Native page combo popups, Adaptive explanatory tooltip, existing inline
  empty/loading/unavailable/error states, and Profiles result banner.

Application-owned surfaces use Flight Deck components. Windows file pickers,
UAC, vJoy/HidHide configuration applications, and the uninstaller intentionally
remain platform-native. Flight Deck provides the explanatory entry and result
states around those operations. No standalone application toast service exists,
so Phase 11 does not invent one.

## Shared native components

- `FlightDeckDialog.qml`: rounded Light/Dark modal surface, semantic border,
  modal scrim, keyboard focus, Escape dismissal, bounded width, and standard
  action hierarchy. It is used by real Profile, Axes, Buttons, Automation,
  Devices, Settings, and Adaptive consumers.
- `FlightDeckTransferDialog.qml`: native portability presentation over the
  existing `AppBackend` commands. It preserves profile/category/pack options,
  validated preview, conflict choices, controller selection, calibration opt-in,
  and unchanged export/import commands and formats.
- `FlightDeckTooltip.qml`: shared application-owned explanatory tooltip.

All field writes still follow the existing command path. Profile and Adaptive
forms report rejection inline and do not close optimistically. Destructive copy
states the actual retained scope, with fault styling reserved for destructive
confirmation rather than ordinary actions.

## Game management and portability

Game association now presents a human-first choice between an existing running
application, the existing Windows executable picker, and an advanced manual
rule. The backend continues to store and validate its real executable-rule
model; browsing and selection do not activate a category.

Import/export remains profile, category, and pack capable. The import view
uses only metadata exposed by the existing preview, leaves configuration
untouched until the existing apply command succeeds, and reports backend
failure/status verbatim. Export invokes the same existing portable profile or
pack command only after the OS destination picker returns.

## Focused interaction and lifecycle coverage

`app_qml_startup_tests` now adds real pointer coverage for:

- opening the native transfer surface without switching to the classic
  Profile Library, then closing it with Escape;
- submitting an empty category form and verifying inline rejection with no
  authoritative category mutation;
- opening profile deletion, cancelling without mutation, reopening, and
  confirming the authoritative deletion.

The existing lifecycle test continues to exercise real pointer dropdown
selection, Flight Deck Light/Dark switching, native Settings actions,
cross-page navigation, repeated Legacy/Flight Deck/Top Gun/Standard
transitions, and snapshot isolation. Rendering the new import preview, dialog,
or menu does not call an import, export, game-association, or deletion command.

## Responsive, visual, and experience review

The native-QML suite renders both Flight Deck appearances and the established
900 x 650 and wide layouts. Transfer content uses an internal scroll surface
with bounded height; normal forms and confirmations use bounded modal widths.
Long names, paths, and validation text wrap within their available surfaces.

The completed offscreen review covers 142 Light/Dark captures, including the
new transfer, invalid-form, destructive-confirmation, repair-confirmation,
Automation empty/search, and Adaptive workshop states. These renders remain
geometry/state/contrast evidence only because the known host may substitute
box glyphs. They do not prove native desktop font rasterization, physical
controller behavior, vJoy output, UAC interaction, or owner acceptance. A
safe candidate launch remains gated manual validation.

Legacy, Standard, and Top Gun remain on their existing secondary surface
implementations. Day Ops is absent from this Phase 10 baseline; it was neither
invented nor exposed. Flight Deck remains reachable only through the existing
`--flight-deck-preview` gate.

## Local validation

- Final Release build completed with the repository Qt 6.8.3 MSVC toolchain
  after all Phase 11 source changes.
- Visual `app_qml_startup_tests` passed in 127.31 seconds and emitted 142
  scoped PNGs. Representative transfer, invalid-form, destructive-confirm,
  repair-confirmation, and Adaptive captures were inspected manually.
- Final `ctest --test-dir build-phase11 -C Release --output-on-failure` passed
  all 11 tests in 133.49 seconds; its QML lifecycle target passed in 119.82
  seconds.
- The lifecycle assertion was clarified to require suspended mapping, not a
  specific host vJoy-ready value. vJoy-driver availability is independent
  machine state and the test continues to prove that injected live telemetry
  reaches the Response Lab without activating mapping.

## Recommended Phase 12 scope

Use Phase 12 only for final integration/acceptance work after all native
secondary-surface visual evidence has been reviewed: candidate launch,
desktop typography, manual OS picker/UAC boundaries, broader owner acceptance,
and any authoritative Day Ops coexistence integration. Do not remove the
Flight Deck preview gate as part of this phase.
