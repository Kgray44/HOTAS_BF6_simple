# Flight Deck Phase 2 — Overview shell and validation

## Candidate and baseline

Phase 2 was built in the isolated
`codex/flight-deck-phase2-overview-shell` worktree from the accepted Flight
Deck Phase 1 foundation:

- Phase 1 foundation: `70227445b6f01cb050040b0b61a71f24fb358025`.
- Refreshed `origin/main` at inspection: `7bfa244e98d06219163c9c9cab4f562b60e0cf47`.
- The Phase 1 foundation is not an ancestor of that `origin/main` commit.

No concurrent feature branch was merged, cherry-picked, or modified. The
separately running mapper process and unrelated worktrees were left alone.

## Day Ops baseline correction

The Phase 1 repository snapshot exposes Legacy, Standard, and Top Gun as its
three selectable token themes. Product-roadmap truth is different: Day Ops is
the fourth existing theme under concurrent V2.4.0 implementation, and Flight
Deck is intended to be the fifth experience.

After fetching and inspecting the candidate, `origin` refs, and the relevant
V2.4.0 worktree, no authoritative Day Ops implementation was present in this
candidate baseline. This change therefore does not create, reproduce, merge,
or special-case Day Ops. Flight Deck stays an alternate `currentExperience`
shell, independent of the existing token-theme selector, so a real Day Ops
theme can coexist when its implementation reaches a shared baseline.

Regression coverage in this candidate is Legacy, Standard, Top Gun, and the
Flight Deck preview. Day Ops coexistence and regression validation are pending
integration of the concurrent V2.4.0 Day Ops implementation.

## Delivered Phase 2 surface

- A native, responsive Flight Deck Overview replaces only Overview page
  composition; all other pages remain in the established Standard page host.
- The shell has persistent left navigation, a preview label, system-readiness
  rail card, light/dark Flight Deck tokens, keyboard-focus treatment, and a
  constrained 900 × 650 scroll path.
- `FlightDeckReadiness` centrally interprets existing published UI state for
  controller readiness, selected controller count/name, vJoy status and output
  layout, HidHide readiness checks, profile source, and the existing
  low-frequency application snapshot. It neither polls the mapper hot path nor
  replaces backend authority.
- Overview presents current setup, health cards, active profile, game
  detection, vJoy, HidHide, mapper state, and the existing bounded axis UI
  snapshot. Actions use the existing Devices & setup, Profiles, Axes,
  Diagnostics, vJoy configuration, and HidHide configuration workflows.

No DirectInput acquisition, `MappingWorker`, transforms, vJoy write path,
Adaptive Response runtime, controller identity, game detection authority, or
profile persistence behavior was changed.

## Validation evidence

- Release build: completed with the repository Qt 6.8.3 MSVC toolchain,
  including QML ahead-of-time compilation for all new Flight Deck components.
- Full CTest: **11/11 passed**, including mapping, automation, readiness,
  startup, release-contract, theme-manager, and Flight Deck QML lifecycle
  coverage.
- Flight Deck lifecycle checks prove the Existing shell remains selected for
  the snapshot token themes; both Flight Deck appearances render the native
  Overview; real setup/profile routes remain reachable; semantic readiness
  coverage includes ready, no-controller, output-unavailable, attention,
  partially-ready, multi-device, no-profile, no-game, detected-game, and
  HidHide-attention cases.
- Reviewed screenshots are under
  `artifacts/flight-deck-phase2-visual-final/`: Dark/Light normal, minimum,
  and expanded views, plus Dark/Light ready-normal views. The ready-normal
  images use a startup-test-only deterministic presentation state and are not
  physical-device proof. The attention images use the real test backend state.

## Follow-up boundary

Keep Flight Deck preview-gated. A later phase can make a user-facing selector
decision, redesign additional page bodies, and rerun Day Ops coexistence
coverage only after the authoritative Day Ops integration exists. Neither this
candidate nor its visual evidence proves physical HOTAS capture or release
readiness.
