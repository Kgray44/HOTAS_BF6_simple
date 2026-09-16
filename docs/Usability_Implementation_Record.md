# Pass A usability implementation record

## Boundary

- Governing map: `HOTAS_BF6_Usability_Implementation_Map_v1.0.md` (owner-supplied 2026-09-16).
- Baseline: `origin/main` / `5d5df526d93cccbef4b9e72068c6929ea7f11f7c`.
- Candidate: `codex/usability-setup-guidance` in `C:\Users\kkids\Documents\HOTAS_BF6-usability-setup-guidance`.
- Scope: Pass A (A-01 through A-08) only. Passes B and C, release, install, tags, merges, and machine-wide driver actions are excluded.

## Reused ownership

| Concern | Current owner | Pass A use |
| --- | --- | --- |
| Editor versus active/effective profile context | `AppBackend` selected/active/effective projections | Preserve and show it consistently; browsing remains non-activating. |
| Coherent activation | `ActivationResolver`, `AppBackend::activateProfileResult`, `AppBackend::activateDeviceRigResult` | Route every activation entry through these existing transactions. |
| Rig/profile/output association | `AppBackend` canonical configuration mutations and Device Rig model | Do not create a parallel setup model. |
| Readiness, physical identity, repair | `ControllerReadinessService` and its existing setup-health session | Present scoped results; do not add driver work or probes in QML. |
| Passive physical input evidence | mapper-published telemetry and existing setup live test | Offer read-only access without new competing acquisition. |

## A-01 fixture journey and observed gaps

The ordinary pedals journey was exercised with the existing isolated startup fixtures: detected controller inventory, `verifyController`, `createDeviceRigResult`, profile/rig association, and `activateDeviceRigResult`. The fixture path keeps the pre-existing HOTAS record and its output descriptor intact; it does not use a real controller, vJoy, or HidHide operation.

The useful ownership chain is:

`controller inventory -> verifyController -> createDeviceRigResult -> ActivationResolver / activateDeviceRigResult -> profile and rig-owned output`

Two concrete context defects were found and corrected: Signal Flow's profile chooser activated the selected profile, and each Curve Editor profile chooser did the same. These now call `selectProfileForEditing`; activation remains an explicit resolver transaction. The legacy Device Rig detail action now also consumes `activateDeviceRigResult`, so its outcome has the resolved profile, rig, output, and failure explanation rather than a generic boolean claim.

## Overlap and dependency boundary

`codex/v2.6.6-polish-resilience`, Device Rig ownership, Signal Flow 6.75, and responsiveness worktrees contain unmerged or dirty work. They are not incorporated by this campaign. The accepted `origin/main` baseline already includes the merged v2.6.5 HidHide Health work.

## A-item status and evidence

| Item | Status | Evidence / remaining work |
| --- | --- | --- |
| A-01 | Complete in isolated fixtures | Baseline, isolation, workflow owners, and fixture journey are recorded above. Native hardware was not used. |
| A-02 | Complete | New shared Flight Deck context strip distinguishes editing, currently using, and effective context. Signal Flow and all Curve Editor selectors are editor-only, with a focused native QML regression. |
| A-03 | Complete | Flight Deck cards/details and legacy detail action use existing result-bearing resolver APIs; no silent profile is created. |
| A-04 | Complete | Existing structured readiness/ownership projections are retained; primary Flight Deck wording no longer presents implementation details as user status. |
| A-05 | Complete (ownership preserved) | Existing rich output-choice and conflict routes remain the single owner; this pass does not alter analog, button, POV, or output safety semantics. |
| A-06 | Complete | Existing four-area Flight Deck Overview is preserved and adds a direct physical-input test handoff. |
| A-07 | Complete | Coverage inventory below records the direct context/copy/action changes and preserved page ownership; the full native QML lifecycle suite passes. |
| A-08 | Complete in fixtures | Read-only test is available from Overview, Flight Deck Devices cards, and legacy physical detail. It reads only existing mapper evidence and gives an honest unavailable/offline state. |

## A-07 page coverage inventory

| Surface | Pass A outcome |
| --- | --- |
| Overview | Retains current setup, attention, connection, and live-control areas; offers `TEST PHYSICAL INPUT` handoff. |
| Devices and setup | Controller cards, rig detail, and legacy physical detail expose scoped actions; test input is separate from verification and mapped output. |
| Axes and Buttons/POVs | Existing editor/ownership and targeted setup handoffs retained; shell context now identifies editor versus runtime configuration. |
| Curve Editor | Profile chooser is explicitly `EDITING PROFILE` and no longer activates runtime state. |
| Profiles/Categories | Existing explicit profile activation and rig association remain the canonical route; browsing remains passive. |
| Adaptive Response and Automation | Existing focused context and processing/automation semantics retained; shared shell context exposes any editor/runtime difference. |
| Signal Flow | Profile context chooser is editor-only; advanced graph, route, and return-state ownership remain intact. |
| Diagnostics and Settings | Existing scoped evidence and repair/maintenance boundaries retained; shared shell context remains visible. |
| Global selectors and dialogs | Flight Deck selector keeps activation explicit; legacy rig action now shows resolver feedback; input-test dialog explains its read-only scope. |

## Verification evidence and boundary

- `app_backend_startup_tests`: passed with `ctest -C Release -R '^app_backend_startup_tests$' --output-on-failure` (14.70 seconds). The read-only input fixture verifies that active and selected Profile, active and editing Device Rig, vJoy identity, and mapping request remain unchanged while a test starts and stops.
- `app_qml_startup_tests`: passed with `ctest -C Release -R '^app_qml_startup_tests$' --output-on-failure` (159.14 seconds). Focused Signal Flow and Flight Deck visual runs also passed in both supported themes.
- Candidate mapper benchmark: every reported mapping, profile-control, and automation case reported `hot_path_allocations=0`; typical mapping p95 was 0.4–0.6 microseconds. This is a candidate measurement, not a freshly re-run baseline comparison. The Pass A diff does not modify `MappingWorker` or mapper-core sources.
- Mapping-off native review uses the shipped `--isolated-presentation` route. It test-scopes settings and does not start the DirectInput-to-vJoy worker, controller discovery, game detection, or update checks. No vJoy or HidHide mutation is requested or performed.
- The complete native candidate at `C:\hotas-builds\usability-setup-guidance\Release\HOTAS BF6.exe` was launched with `--isolated-presentation` and remained resident for owner review. The available desktop automation inventory did not expose that window for scripted pointer review, so process residence is not claimed as direct interaction evidence.

Physical hardware qualification remains **not performed**. Fixture, benchmark, and isolated-presentation evidence must not be read as proof of a connected controller, a real vJoy output, or owner acceptance.
