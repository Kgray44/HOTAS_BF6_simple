# Flight Deck Phase 3 — Devices and setup validation

## Baseline and scope

- Source baseline: accepted Flight Deck Phase 2 commit
  `152981d1c7b5bf93dae250e827ede0bfee739f93`.
- Candidate branch: `codex/flight-deck-phase3-devices-setup` in its isolated
  worktree. The Phase 2 worktree, dirty unrelated checkout, and active mapper
  process were not changed.
- Day Ops is still absent from this baseline. Flight Deck remains preview-gated
  and keeps its experience shell independent of the Legacy, Standard, and Top
  Gun token themes; Day Ops coexistence remains pending integration.

## Native Devices composition

- `FlightDeckDevices` replaces only Flight Deck page 2 with Setup Health,
  physical controller cards, Virtual Output, Device Isolation, verification,
  and an advanced Diagnostics handoff.
- It consumes the existing `AppBackend.controllers` collection, so connected,
  offline, verified, unverified, ambiguous, selected, and active controllers
  remain distinct. The empty-device state has a real controller rescan action.
- Controller-card delegation indexes the backend `QVariantList` directly. This
  avoids treating map rows as QML roles and keeps the real controller fields
  available to each card.
- Flight Deck reuses `FlightDeckReadiness` and the existing readiness checks for
  its hero, Virtual Output, and Device Isolation; Overview and Devices have
  one presentation interpretation of the same backend state.

## Existing commands retained

- Verify Controller selects a new controller through `selectNewController`;
  verified devices use `setActiveController`; rescan uses `refreshControllers`.
- Verify Setup uses `verifyHotasSetup`. The scoped repair confirmation invokes
  `applyControllerReadiness`, and undo invokes `undoControllerReadiness`.
- Virtual Output opens the existing vJoy configuration utility. Device
  Isolation either invokes the existing scoped HidHide self-access repair or
  opens its existing configuration client. No decorative repair action was
  added.
- Overview recovery cards route to Devices with controller, virtual-output, or
  isolation context. Setup confirmation and undo dialogs are Flight Deck
  native; Diagnostics retains deep technical investigation.

## Validation

- The focused offscreen Flight Deck lifecycle test covers native Devices page
  loading, the empty state, four simultaneous controller states (active,
  verified, unverified with an extreme-length name, and offline), centralized
  readiness, vJoy and HidHide attention, the setup-repair confirmation,
  Overview-to-Devices deep links, and existing theme loading. It captures each
  state in both light and dark, including 900 × 650 and expanded multi-device
  layouts.
- Those arrangements use test-only QML presentation overrides. They do not
  mutate `AppBackend`, controller discovery, persisted configuration, mapper
  state, or invoke a repair action. Production Devices always reads
  `AppBackend.controllers` and the centralized readiness snapshot.
- Captures were inspected at
  `artifacts/flight-deck-phase3-visual/`. The safe offscreen path substitutes
  glyph boxes for text on this host, but the reviewed images establish card
  hierarchy, scrolling, constrained-width wrapping, reachable controls,
  attention styling, modal layering, focus boundaries, and light/dark
  contrast. They are not literal-text or physical-controller proof.
- The final source pass also corrected the Devices loader's structural QML
  brace and the controller-card `QVariantList` binding before the Release QML
  compilation and visual run.
- Release build: passed with the repository Qt 6.8.3 MSVC toolchain, including
  QML ahead-of-time compilation and deployment of `HOTAS BF6.exe`.
- Full CTest: **11/11 passed**, including controller readiness, release
  contracts, all established theme lifecycle checks, and Flight Deck QML.
  No HOTPATH or mapper files are changed, so no mapper benchmark is required.

## Recommended Phase 4

Redesign the Axes workspace or Profiles as the next native Flight Deck page,
reusing the established presentation-only fixture seam where visual state
coverage is needed. Do not expose Flight Deck as a production experience
selector yet.
