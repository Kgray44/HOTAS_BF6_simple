# Whole-App Responsiveness & Smoothness — Phase 0 Characterization

**Status:** characterization and instrumentation candidate; not a release.

**Branch:** `codex/whole-app-responsiveness-phase0`
**Protected-main base:** `origin/main` at `5d5df526d93cccbef4b9e72068c6929ea7f11f7c`
**Measured instrumentation commit:** `fb1d4194497fb28ccdf5fd37f2ed9f7d58fe89b3`
**External build directory:** `C:\hotas-builds\responsiveness-phase0`
**Evidence directory:** `C:\hotas-builds\responsiveness-phase0\evidence\matrix-ambient-saturated`

This is an evidence and observability phase. It does not change mapping policy, render architecture, scheduling policy, profile behavior, application version, release assets, installer, tag, manifest, or protected `main`.

## Scope and hard boundaries

The measured navigation harness contains exactly these ten pages, in two passes:

| Page | Route identifier |
| --- | --- |
| Overview | 8 |
| Devices | 2 |
| Axes | 0 |
| Buttons | 1 |
| Curve Editor | 6 |
| Profiles | 5 |
| Adaptive Response | 9 |
| Automation | 7 |
| Diagnostics | 3 |
| Settings | 4 |

**Signal Flow is entirely excluded.** Page 11 is not requested by this harness, not marked by the probe, and not reported in any result below. Its existing QML can be compiled incidentally as part of the application module, but it was neither driven nor measured.

The DirectInput -> `MappingWorker` -> vJoy report path was not instrumented or otherwise changed. No per-report mapping, locking, allocation, string formatting, or QML work was added. Mapping evidence remains the existing benchmark and telemetry system.

## Opt-in probe design

`HOTAS_RESPONSIVENESS_PROBE=1` creates `ResponsivenessProbe` during application startup. Without that variable the object, application event filter, timer, frame connection, and sample storage do not exist. The QML page hosts resolve a single disabled binding and do not cross into the backend on loader lifecycle callbacks. Persistence hooks are compiled only into the application and probe-aware presentation-test targets; mapper/core benchmark targets retain the original `ConfigStore::save()` implementation and do not link the probe.

The probe is in-memory and bounded:

- 4,096 retained samples per distribution; counters continue after that bound.
- 512 pending GUI input events, 256 navigation records, 128 save records, and 64 largest stall records.
- A single JSON file is written only at explicit export or clean shutdown, via `QSaveFile`. `HOTAS_RESPONSIVENESS_PROBE_OUTPUT` can override its path; the default is the app-local `responsiveness/responsiveness-probe.json`.
- The JSON contains aggregate timing, page labels, scenario/load-evidence path, and save timings. It contains no profile data, mapping values, controller input, or user configuration contents.

Instrumentation captures the following control-plane signals:

| Signal | Definition and limits |
| --- | --- |
| Event-loop heartbeat | A 16-ms GUI-thread timer records observed interval minus 16 ms, plus p50/p95/p99/max and >16/33/50/100/250/500/1000/5000-ms counters. |
| Input to presentation boundary | Supported GUI mouse press/release, wheel, keyboard, and touch events to the next `QQuickWindow::frameSwapped`. Mouse move is deliberately not sampled to avoid high-rate probe noise. This is not semantic command completion or physical display scanout latency. |
| Frame pacing | Natural consecutive `frameSwapped` intervals. No artificial render loop is introduced. |
| Navigation | Request -> loader activation -> QML object ready -> first subsequent `frameSwapped` for the ten in-scope pages. |
| Persistence | `ConfigStore::save()` total, JSON serialization, `QSettings::setValue`, and `QSettings::sync`; success/failure, GUI-thread count, bounded recent save records, and back-to-back save count. |
| Major-stall context | Bounded records at >=250 ms for heartbeat delay, frame interval, input-to-frame, navigation-to-frame, and save time. Records share monotonic-time origin with save records so manual correlation is possible. |

Qt documents `frameSwapped` as a frame queued for presentation and notes that the signal is emitted from the scene-graph render thread. The probe therefore uses a direct connection whose callback touches only mutex-protected probe state; it does not call QML or GUI APIs from that thread. This boundary is useful but is not a proof of semantic completion, compositor presentation, or panel scanout. See the [QQuickWindow documentation](https://doc.qt.io/qt-6.8/qquickwindow.html) and [Qt Quick scene-graph documentation](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html).

## Background-work inventory (source inspection)

This is an inventory, not a causality finding. The Phase 0 isolated-presentation runs intentionally suppress normal mapping/device startup, so it does not prove the live scheduling effect of these components.

| Component | Current execution model | Phase 0 conclusion / Phase 1 experiment |
| --- | --- | --- |
| `MappingWorker` | Dedicated `QThread`, started `HighPriority` (explicitly below time-critical); UI delivery is queued. | Protected and unchanged. Pair physical report cadence with GUI event/frame data only in a device-owner run. |
| Controller inventory | `m_controllerDiscoveryTimer` invokes `refreshControllerInventory()` from `AppBackend`; it is also scheduled shortly after normal startup. | Measure direct enumeration duration and UI overlap before moving any work. |
| Setup Truth inspection | Existing worker thread returns results to the UI through queued work. | Time individual inspection phases and publish only bounded summary evidence. |
| HidHide Health | Separate cancellable/read-only `QThread`, started `LowPriority`; completion returns to UI. | Trace launch/completion vs. interaction without altering MappingWorker. |
| Game/process detection | Existing GUI timers schedule detection and foreground-context sampling. | Measure source enumeration and callback duration; test a worker-bound snapshot only if measured UI impact warrants it. |
| Update check | Scheduled 500 ms after startup; asynchronous network reply with a bounded timeout on the UI event loop. | Record reply/callback duration and cold-start overlap, then defer non-critical presentation if evidence supports it. |

No scheduler priority, affinity, worker ownership, rendering pipeline, or background feature was changed in this phase.

## Safe hostile-load harness

`scripts/Invoke-ResponsivenessCharacterization.ps1` provides bounded `Idle`, `ModerateCpu`, `HeavyCpu`, `SevereCpu`, `NearSaturationCpu`, `Disk`, and `Combined` runs. It caps duration (8–120 seconds), CPU jobs (default maximum 8), and disk churn (32–512 MiB; default 128 MiB). Jobs and the dedicated temporary directory are stopped/removed in `finally`. The script records Windows processor load and physical-disk throughput before, during, and after each run, writes the load-evidence path into the probe report, and never writes application configuration or release artifacts.

The test host was already substantially busy. The harness correctly declined to add CPU workers when ambient processor load already met the requested target; this is why Moderate and Heavy CPU runs have zero added workers. Severe and Near-Saturation used eight bounded workers, but their actual sampled load did not land exactly on the intended calibration range. Results therefore report sampled conditions, not nominal labels as if they were calibrated facts.

## Measured isolated-presentation matrix

Each row ran the ten-page QML fixture twice with the probe enabled. It is a real Qt Quick render/process capture, but it is not a native human-pointer or physical-controller capture. No `MappingWorker` device runtime was started, and the fixture's programmatic page selection intentionally produced zero supported GUI input events. Consequently, interaction-to-frame latency has **no verdict** for this matrix.

Values are milliseconds. Heartbeat is `observed interval - 16 ms`; frame is natural `frameSwapped` interval. Every report had 20 navigation records and zero Signal Flow records.

| Scenario | CPU before -> under | Disk under (B/s) | CPU workers / ambient-target flag | Heartbeat p50 / p95 / p99 / max | Frame p50 / p95 / p99 / max | Largest navigation first-frame | Save total / sync max |
| --- | ---: | ---: | --- | --- | --- | ---: | --- |
| Idle | 27 -> 48% | 63,437 | 0 / false | 12.92 / 55.46 / 304.34 / 304.34 | 21.23 / 65.99 / 73.07 / 73.07 | 72.64 | 8.45 / 6.50 |
| ModerateCpu | 79 -> 71% | 13,354,697 | 0 / true | 25.72 / 59.79 / 331.57 / 331.57 | 26.18 / 59.82 / 64.28 / 64.28 | 63.82 | 10.08 / 8.21 |
| HeavyCpu | 87 -> 86% | 0 | 0 / true | 16.05 / 48.34 / 308.28 / 308.28 | 25.14 / 54.87 / 61.80 / 61.80 | 61.50 | 10.95 / 9.20 |
| SevereCpu | 68 -> 99% | 0 | 8 / false | 17.53 / 66.88 / 389.31 / 389.31 | 26.03 / 73.85 / 82.88 / 82.88 | 82.51 | 13.41 / 11.49 |
| NearSaturationCpu | 68 -> 94% | 4,644,874 | 8 / false | 32.27 / 91.58 / 551.44 / 551.44 | 32.05 / 94.50 / 102.54 / 102.54 | 101.78 | 123.32 / 120.85 |
| Disk | 42 -> 50% | 149,516,244 | 0 / false | 20.01 / 70.70 / 343.51 / 343.51 | 25.60 / 68.91 / 77.01 / 77.01 | 76.45 | 8.54 / 6.48 |
| Combined | 51 -> 94% | 48,045,393 | 8 / false | 19.84 / 69.77 / 704.10 / 704.10 | 28.72 / 78.73 / 85.83 / 85.83 | 85.43 | 49.05 / 46.98 |

### Interpretation

- The largest measured event-loop delay was **704.10 ms** in Combined. The record starts at process startup with no current page, so it is not evidence of an interactive multi-second freeze.
- Near-Saturation was the worst persistence case: **123.32 ms** total, with **120.85 ms** in synchronous settings flush. Combined was next at 49.05/46.98 ms. This is a measured candidate for a later persistence experiment, not a decision to redesign save semantics now.
- The largest first-frame navigation was **101.78 ms** (Near-Saturation), and the largest frame interval was **102.54 ms** in the same run. No measured navigation or frame interval exceeded one second.
- The reports do not establish a direct causal relationship between a save and a heartbeat/frame outlier. Their shared monotonic timestamps allow a targeted future overlap experiment; Phase 0 does not claim attribution.
- No application process CPU time or working-set series was captured in these probe files. The safe harness recorded host CPU/load and disk throughput; a process-level ETW/PerfMon capture is a Phase 1 option if it becomes necessary.

## Mapping hot-path regression guard

The opt-in probe is absent from the mapping report loop. The existing mapping benchmark was run three times before the change and three times on the candidate. All samples reported zero hot-path allocations.

| Workload | Baseline range | Candidate range | Tail allocation result |
| --- | --- | --- | --- |
| Broadly idle linear | 2.135–2.349 M reports/s; p95 0.5–0.7 us | 2.338–2.370 M reports/s; p95 0.5 us, p99 0.5–0.6 us | 0 allocations |
| Adaptive, all 8 axes | 1.504–1.656 M reports/s; p95 0.6–1.0 us | 1.599–1.661 M reports/s; p95 0.6–0.8 us, p99 0.8–1.0 us | 0 allocations |
| Profile activation / automation temporal | baseline within normal run variance | candidate p95 2.0–2.1 us / 2.1 us; p99 2.2–2.4 us / 2.3–2.4 us | 0 allocations; 0 curve compiles |

This supports **no observed mapping benchmark regression** for this candidate. It does not substitute for a physical controller -> vJoy latency measurement.

## Validation performed

- Clean x64 external CMake configuration/build using Qt 6.8.3 and Visual Studio Build Tools; all 456 registered Release-target build actions completed successfully after the guard fix.
- `responsiveness_probe_disabled`: passes; confirms no probe object when the environment opt-in is absent.
- `responsiveness_probe_aggregation`: passes; exercises bounded aggregation and threshold counters using delays through 20,000 ms.
- Isolated ten-page QML Phase 0 navigation test: passes; produces 20 in-scope navigation records, one persistence sample, zero Signal Flow records.
- The same isolated ten-page QML path also passes with the probe disabled and writes no probe JSON, validating the disabled initialization/callback boundary.
- The complete registered target set is compiled for this candidate. Signal Flow source is compiled incidentally as part of the application module, but no Signal Flow runtime test is run or reported by this campaign.
- Broad legacy test suites are intentionally outside this runtime qualification because some exercise Signal Flow. They are not used as Phase 0 evidence.

## Native and hardware qualification limits

A task-owned native `HOTAS BF6.exe --isolated-presentation` instance was launched with the probe enabled. The available computer-use surface could not enumerate or target that window, so no pointer, keyboard, wheel, resize, scroll, or dialog automation was sent. The task-owned process was stopped afterward; it did not produce a normal-shutdown capture. No mapped physical controller, vJoy output, or mapping activation was exercised.

Therefore Phase 0 makes **no claim** of native interactive smoothness, native input-to-presentation latency, physical controller-to-vJoy latency, or profile/automation behavior under live input. Those remain owner/device qualification work.

## Repeatable owner sequence and Phase 1 candidates

For a native capture, start the candidate with:

```powershell
$env:HOTAS_RESPONSIVENESS_PROBE = '1'
$env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT = "$env:LOCALAPPDATA\HOTAS BF6\responsiveness\owner-native.json"
& 'C:\hotas-builds\responsiveness-phase0\HOTAS BF6.exe'
```

Drive only the ten in-scope routes with real mouse/keyboard/touchpad interaction; perform repeated Settings saves; capture the JSON on clean exit; and compare event-loop, input-to-next-frame, frame, navigation, and save distributions. Do not use Signal Flow in this campaign.

Phase 1 should be evidence-led and narrowly scoped:

1. Reproduce a real native pointer/keyboard/scroll sequence and, with explicit owner hardware authority, a physical controller/mapping observation.
2. Align probe outliers with bounded traces of controller inventory, Setup Truth, HidHide Health, game detection, and update callbacks.
3. Repeat the near-saturation persistence condition to determine whether delayed flushing/debouncing or another control-plane change is justified.
4. Only after a measured culprit is repeatable, make one isolated improvement and re-run this same matrix plus the mapping benchmark.

No remediation is selected by this Phase 0 report.
