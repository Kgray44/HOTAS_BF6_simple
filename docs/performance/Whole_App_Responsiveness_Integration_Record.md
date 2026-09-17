# Whole-App Responsiveness Integration Record

## Provenance

| Item | Value |
| --- | --- |
| Current-main base | `bdedc52848ec18ddc4f6a387776526dcde1c7ce5` |
| Responsiveness source | `codex/whole-app-responsiveness-phase7` at `4660ae4f06351e682ff9638c70f7fa6b3baf4ed4` |
| Integration branch | `codex/whole-app-responsiveness-integration` |
| Merge strategy | One `--no-ff` cumulative Phase 7 merge into a fresh worktree from current `origin/main` |
| Target release | **V2.6.7** |

This is an owner-acceptance candidate, not a release. No protected-main merge,
tag, installer/updater publication, or public release is authorized here.

## Meaningful conflict resolutions

| File | Main change | Responsiveness change | Resolution |
| --- | --- | --- | --- |
| `src/app_backend.cpp` | V2.6.6 added a local `PresentationQos` timer policy plus current foreground-game polling and device/virtual-output behavior. | Phase 4 added `ContentionResilienceController` as the central cadence owner, with scaled visible/minimized/tray polling. | Kept current-main foreground-game polling and all unrelated V2.6.6 behavior. Removed the competing local timer policy so the controller is the single cadence owner; lifecycle timers, foreground polling, and adaptive history use the controller policy. The compatibility QoS label now projects controller state rather than scheduling independently. |
| `src/app_backend.h` | V2.6.6 added the durable virtual-output descriptor-edit seam, protecting active vJoy ownership from implicit reacquisition. | Phase 1 added asynchronous persistence/coordinator declarations; Phase 4 added controller ownership. | Kept both interfaces. Descriptor edits remain current-main controlled; ordinary persistence keeps the coordinator boundary; controller ownership remains outside MappingWorker. A nearby existing Signal Flow comment/declaration was untouched and received no behavioral change. |

No broad `ours` or `theirs` resolution was used. Automatically merged files are subject to the source and runtime checks recorded below.

## Qualification record

Results and any integration-only repair will be appended after the clean build
and focused/current-main regression suite. The intended owner handoff is
documented separately in `Whole_App_Responsiveness_Owner_Acceptance.md`.
