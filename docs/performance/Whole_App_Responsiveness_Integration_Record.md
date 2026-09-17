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

### Build and regression

The clean Release configuration at
`C:\hotas-builds\responsiveness-integration` used Qt 6.8.3, `BUILD_TESTING=ON`,
and `HOTAS_BUILD_PERFORMANCE_BENCHMARK=ON`. The final candidate rebuild embeds
`HOTAS_BF6_VERSION "2.6.7"`.

`ctest --output-on-failure -C Release` completed all **18/18** registered
tests with no failures after the candidate rebuild. This includes mapping core,
configuration persistence, V2.6.6 controller-readiness/device/axis and
Virtual-Output contracts, updater/documentation release contracts, app backend
startup, and the native QML lifecycle suite. The mapping benchmark's one
recorded candidate sample passed with `hot_path_allocations=0` on every
reported idle, profile-control, and Automation row. It is a synthetic
report-to-output-decision measurement; it does not make a physical
DirectInput/vJoy claim.

Native QML qualification passed in Dark and Light appearance matrices (79
captured screens each). The qualification fixes were limited to presentation
lifecycle safety and test synchronization: a recycled Profiles delegate no
longer references a page-owned ID; deferred profile work checks that its page
still exists; adaptive-section scrolling keeps a fitting compact section
visible; Flight Deck dialog/header and fixture Boolean bindings tolerate the
existing theme/fixture shapes; and the test harness settles a scrolled native
hit target before its existing pointer input. No Signal Flow behavior or
qualification was added.

### Controlled load qualification

Evidence is retained outside the repository at
`C:\hotas-builds\responsiveness-integration\evidence-integration`.

| Case | Result | Recorded load evidence | Native interaction result |
| --- | --- | --- | --- |
| Initial CPU-only, 15 workers, requested 90–95% CPU | **NO STABLE CPU-ONLY VERDICT** | mean 92.4%; only 73.7% inside the 88–97% acceptance envelope; `stable=false` | workload exit 0; no native failures |
| CPU-only rerun, 15 workers, requested 90–95% CPU | **PASS** | mean 92.5%; 91.7% inside envelope; 57 s longest continuous envelope; `stable=true` | workload exit 0; no failures; Severe controller state; 100 slider and 100 isolated-persistence edits; read-only vJoy Device 1 state `FREE` |
| Combined CPU plus 128 MiB bounded disk, requested 90–95% CPU | **PASS** | mean 93.0%; 83.1% inside envelope; 48 s longest continuous envelope; `stable=true` | workload exit 0; no native failures; Severe controller state; same bounded interaction and read-only inspection coverage |

The first CPU-only run is retained rather than hidden; its unstable host-load
control is not treated as a product verdict. The stable rerun and stable
combined run exercise controlled native-window synthetic interaction only.
They neither discover nor drive a physical HOTAS or pedals, and the vJoy query
is read-only.

### V2.6.7 candidate preparation

The candidate metadata deliberately prepares **V2.6.7** without publishing it:

- `HOTAS_VERSION` is `2.6.7`, regenerated documentation advertises v2.6.7,
  and the V2.6.7 catalog entry explicitly marks the build as an
  owner-acceptance candidate.
- The locally staged, non-published runtime is
  `C:\hotas-builds\responsiveness-integration\V2.6.7-owner-acceptance`.
  Its `VERSION` file is `2.6.7`; mapper, launcher, Qt runtime, and Windows and
  offscreen platform plugins are present.
- `V2.6.7_Owner_Acceptance.md` records the single consolidated manual pass.

No `v2.6.7` tag, GitHub Release, installer/update publication, production
updater-manifest change, or protected-main merge has been made. The intended
owner handoff is documented separately in `V2.6.7_Owner_Acceptance.md`.
