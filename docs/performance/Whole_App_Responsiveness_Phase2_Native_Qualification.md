# Whole-App Responsiveness Phase 2 - Native and Physical Qualification

**Status:** partial autonomous qualification. This is not a release and is **not
Phase 2 completion**: native pointer/keyboard/wheel evidence, live mapping, and
physical controller-to-vJoy evidence require an owner-run capture on this host.

**Phase 0 head:** `f137c5405698989b6cb664aa039a8d4b9486026b`
**Phase 1 implementation base:** `4325fdac929f0a59fb5769b3c43a2e709937ef66`
**Phase 2 initial evidence commit:** `0e466250bd6e19f6c1397feed02aeed3c74032d9`
**`origin/main` observed before Phase 2:** `5d5df526d93cccbef4b9e72068c6929ea7f11f7c`
**Phase 2 worktree:** `C:\Users\kkids\Documents\HOTAS_BF6-responsiveness-phase2`
**Phase 2 Release build:** `C:\hotas-builds\responsiveness-phase2`

This branch begins exactly at the verified Phase 1 head. Its only committed
candidate change is this evidence report. It makes no version, schema,
installer, updater, tag, release, protected-main, mapping-policy, scheduling,
or Signal Flow change.

## Scope and hard exclusions

The Phase 0 ten-page scope remains Overview, Devices, Axes, Buttons, Curve,
Profiles, Adaptive Response, Automation, Diagnostics, and Settings. Signal
Flow was not opened, requested, navigated, measured, profiled, or changed.
Its QML was compiled incidentally by the monolithic application target only.

The DirectInput -> MappingWorker -> vJoy report loop remains allocation-free
and has no Phase 2 instrumentation. The existing synthetic mapper benchmark
and existing runtime telemetry remain the measurement boundaries. Source
inspection confirms that `mapping_hot_path_benchmark` links only the mapping
core sources and Qt Core; it contains neither `ConfigPersistenceCoordinator`
nor `ResponsivenessProbe`.

## Environment

| Item | Observed value |
| --- | --- |
| OS | Windows 11 Home, 10.0.26200, 64-bit |
| CPU | AMD Ryzen AI 7 350 with Radeon 860M, 8 cores / 16 logical processors |
| Memory | 33,413,779,456 bytes reported by Windows |
| Toolchain | MSVC 19.44.35228.0 via Visual Studio 2022 Build Tools 17.14.39 |
| Qt | 6.8.3, `msvc2022_64` |
| Physical-controller indication | One present HID-compliant game controller (`VID_06A3`, no unique serial recorded) |
| Virtual-output indication | `vJoy Driver` and `vJoy Device` present and OK |
| HidHide state | Not queried; no Phase 2 repair or configuration action occurred |

Presence is not proof of a mapped controller, an active profile, vJoy writes,
or end-to-end latency.

## Phase 1 integrity baseline

The Phase 2 Release configuration enabled tests and the existing performance
benchmark. The first build invocation omitted the Visual C++ developer
environment and therefore could not locate standard headers; it was not a
source failure. The corrected Build Tools invocation passed the focused suite:

| Check | Result |
| --- | --- |
| `mapping_core_tests` | PASS, 28.96 s in the final focused run |
| `config_persistence_coordinator_tests` | PASS, 0.14 s |
| `responsiveness_probe_disabled` | PASS, 0.05 s |
| `responsiveness_probe_aggregation` | PASS, 0.04 s |
| `app_qml_startup_tests` with `HOTAS_QML_LIVE_TELEMETRY_ONLY=1` | PASS, 12.57 s |

The full native `HOTASMapper` Release target also built and deployed its Qt
runtime successfully. Final all-target build remains pending the owner-native
qualification rather than being repeated before it.

## Controlled interleaved mapping A/B

`A` is the Phase 0 source head in a fresh external Release build.
`B` is the Phase 1/2 head in the prescribed Phase 2 Release build. Both used
the same Qt 6.8.3, MSVC 19.44, Release settings, benchmark command, host
session, and PATH. Order was `A1, B1, A2, B2, A3, B3`, not batched by side.
Raw verbose CTest output is retained outside the repository at
`C:\hotas-builds\responsiveness-phase2\evidence-phase2\mapping-ab`.

| Workload | Side | reports/s samples | median reports/s | median p95 / p99 us | largest per-report max us | allocations | output decisions / run |
| --- | --- | --- | ---: | --- | ---: | ---: | ---: |
| Linear | A | 1,945,085; 2,265,627; 2,276,201 | 2,265,627 | 0.6 / 0.7 | 64.3 | 0 | 2,133,853 |
| Linear | B | 2,316,358; 2,028,568; 2,188,778 | 2,188,778 | 0.7 / 0.8 | 198.4 | 0 | 2,133,853 |
| Adaptive All 8 Axes | A | 1,522,177; 1,598,899; 1,476,627 | 1,522,177 | 1.0 / 1.1 | 180.1 | 0 | 2,133,853 |
| Adaptive All 8 Axes | B | 1,498,159; 1,592,938; 1,384,840 | 1,498,159 | 1.0 / 1.3 | 351.4 | 0 | 2,133,853 |

The complete benchmark suites each passed. End-to-end suite durations were
8.46, 9.88, and 8.73 seconds for A; and 8.69, 8.74, and 8.80 seconds for B.
Every profile-control sample on both sides also recorded zero hot-path
allocations and zero curve compiles during triggers.

### A/B verdict

The linear and adaptive throughput distributions overlap. With three
interleaved samples, this is **no measured material throughput regression**;
it is not a claim of statistical significance or an improvement. B's adaptive
p99 and one-off maximum were higher, but the benchmark source and link set are
unchanged from Phase 1 and the samples retain zero allocations, fixed output
decision counts, and zero trigger-time curve compiles. The tails are an
observation to retain during live qualification, not grounds to change mapper
priority or the hot path.

This benchmark intentionally excludes DirectInput polling and the vJoy driver
call. It does not prove physical controller-to-vJoy latency.

## Native interaction and scrolling matrix

**No verdict.** The native Windows computer-use surface on this host exposed
no targetable app windows, so no synthetic mouse, wheel, keyboard, resize,
dialog, tab, slider, dropdown, or text input was sent. That limitation is not
substituted with offscreen QML evidence.

The existing probe is ready to capture supported native input to the next
`frameSwapped`, GUI heartbeat, natural frame pacing, navigation lifecycle, and
persistence telemetry. `frameSwapped` remains a queue-for-presentation
boundary, not display scanout or semantic-command completion. No owner-driven
native capture exists yet, so there are no input-to-frame p50/p95/p99/max,
input counts, scroll labels, navigation-first-frame values, owner smoothness
observations, or native persistence records to report.

No repeated multi-second ordinary interaction was observed because no ordinary
native interaction was driven. This is not evidence that such a stall cannot
occur.

## Physical mapping and contention matrix

**No verdict.** Read-only device discovery found a game-controller candidate
and vJoy present, but it cannot establish that a controller is recognized by
this mapper, an approved safe profile is active, axes are changing, mapping is
active, or vJoy is receiving writes.

Three user-owned `HOTAS BF6.exe` processes were already active: the installed
mapper and two separate task builds. To preserve their configurations and live
mapping state, this campaign did not start a competing full mapper process,
modify a profile, toggle mapping, or attach hostile CPU/disk load to them.
Consequently all of the following remain unmeasured: report cadence, mapping
latency p50/p95/p99, vJoy writes per second, missed/late reports, worker
backlog, mapping-off versus mapping-on scheduler comparison, and physical
controller-to-vJoy timing.

The Idle, CPU, Disk, and Combined native/physical matrix is therefore **not
run**, rather than represented as a failed or passing stress result. No CPU or
disk contention was launched against the active user-owned mappers.

## Native persistence

The focused QML workload and Phase 1 matrix retain their synthetic evidence
that ordinary persistence captures and queues on the GUI side while the writer
runs on its serial worker. That architecture was preserved: current Phase 2
source equals the Phase 1 head before this report. A real native configuration
edit, durability catch-up, and clean-exit flush have not been performed in
this campaign, so native persistence is **not yet qualified**.

## Owner-assisted completion sequence

Do this only after the owner has chosen an inactive test window and a safe test
profile, with any user-owned mapper process explicitly left alone or stopped by
the owner. Launch the Phase 2 build with:

```powershell
$env:HOTAS_RESPONSIVENESS_PROBE = '1'
$env:HOTAS_RESPONSIVENESS_SCENARIO = 'phase2-owner-native'
$env:HOTAS_RESPONSIVENESS_COMMIT = '4325fdac929f0a59fb5769b3c43a2e709937ef66'
$env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT = 'C:\hotas-builds\responsiveness-phase2\evidence-phase2\owner-native.json'
& 'C:\hotas-builds\responsiveness-phase2\HOTAS BF6.exe'
```

With the probe enabled, make two real mouse/keyboard/wheel passes through only
the ten in-scope pages. On Settings/Overview, medium Axes/Buttons/Profiles,
and heavy Adaptive Response/Devices/Diagnostics, record the owner labels
smooth, minor hitch, choppy, or unusable. Exercise a safe slider or dropdown,
a non-destructive dialog, a harmless text check, resize from minimum to normal
to large, and one safe test-profile edit followed by clean exit. Do not open
Signal Flow.

If explicitly approved for live hardware, repeat under Idle, bounded CPU,
bounded disk, and combined load with mapping off then on; confirm controller
recognition, active safe profile, axis movement, mapping state, and vJoy
output. Record owner observations beside the probe. Do not claim USB-to-game
or scanout latency from these measurements.

## Verdict by subsystem and Phase 3 selection

| Subsystem | Verdict | Basis |
| --- | --- | --- |
| Mapper hot path | qualified for allocation/control-flow guard; no measured material throughput regression | Interleaved A/B; zero allocations, zero trigger-time curve compiles, overlapping throughput distributions |
| Phase 1 persistence architecture | no new issue measured | Focused regression suite passes; native edit/exit still unqualified |
| Native interaction, navigation, scrolling, resize | no verdict | No targetable native automation and no owner capture |
| Physical controller to vJoy | no verdict | Device/vJoy presence only; no live mapper observation |
| Mapping-on scheduler balance | no verdict | Deliberately not tested against user-owned live processes |

No remediation target is selected from this partial run. The ranked Phase 3
prerequisite is an owner-authorized, probe-enabled native and physical
qualification capture. Only after that capture identifies a repeatable
GUI-thread blocker, controller-enumeration cost, invalidation fanout, scrolling
problem, construction cost, or scheduling effect may a Phase 3 remediation be
chosen. No mapper-priority, rendering, scrolling, loader, or Signal Flow
change is authorized by this report.
