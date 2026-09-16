# Whole-App Responsiveness Phase 1 — Non-blocking Configuration Persistence

**Candidate implementation commit:** `ca29bbaea091b56e06005d98ec1a9355abfe1965`  
**Stack base / Phase 0 head:** `f137c5405698989b6cb664aa039a8d4b9486026b`  
**`origin/main` observed before the worktree was created:** `5d5df526d93cccbef4b9e72068c6929ea7f11f7c`

This is a stacked Phase 1 candidate, not a release. It does not change the version, schema version, installer/updater manifests, tag, release assets, or protected `main`. Phase 0 remains unchanged on its own draft branch/PR.

## Decision and boundaries

Phase 0 measured QSettings `sync()` tails of 120.85 ms in its near-saturation sample and 46.98 ms in Combined. It did not prove a causal interactive stall, but it identified synchronous configuration persistence as a bounded control-plane experiment.

Phase 1 moves the *scheduling* of ordinary configuration persistence off the GUI thread. `ConfigStore`, its QSettings format, key, schema, migration behavior, and JSON compatibility remain unchanged. The DirectInput -> MappingWorker -> vJoy report path is not called, instrumented, synchronized, allocated, logged, or reprioritized by this work. No Signal Flow QML, graph behavior, or profile-specific runtime qualification is part of this campaign; its one immediate workspace-save barrier uses the generic serializer solely to preserve write ordering.

## Save-call audit

| Class | Callers / role | Phase 1 disposition |
| --- | --- | --- |
| A — ordinary interactive | `persistAndApply()` configuration editors, selected-axis selection, remembered-controller completion | Copy the current canonical `MapperConfiguration`, enqueue, then apply the canonical/runtime state immediately. No GUI wait. |
| B — burst-prone interactive | The existing Flight Deck Adaptive Response tuning slider and other repeated editor commits through `persistAndApply()` | Same asynchronous request path; only the newest pending snapshot is retained while one worker write may finish. |
| C — explicit transaction | Activation commit, exact controller verification/read-back, HidHide-backed forget transaction, Signal Flow presentation save | Enqueue then use a bounded durable-generation barrier before preserving the existing success/rollback contract. |
| D — handoff / shutdown | Launcher/updater handoff and `aboutToQuit` | Launcher start is blocked unless the latest requested snapshot is durable. Shutdown waits only for the latest already-requested generation, with a 2 s bound. |
| E — non-GUI/test utility | AppBackend fixture seams, QML fixture setup, ConfigStore migration during load | Synchronous `ConfigStore::save()` remains valid and unchanged for these isolated paths. |

The only remaining direct `ConfigStore::save()` production use is the coordinator's untimed worker callback when the opt-in probe is disabled. Direct AppBackend call sites are test-only fixtures. This is intentional: one serial owner prevents a direct transaction from being overwritten by an older pending snapshot.

## Architecture and ordering contract

```text
GUI configuration mutation
  -> canonical AppBackend m_configuration and MappingWorker update now
  -> bounded-by-value snapshot + generation request
  -> ConfigPersistenceCoordinator (one low-priority serial QThread)
  -> unchanged ConfigStore/QSettings writer
  -> durable generation / bounded transaction or shutdown barrier
```

The worker receives only a copied `MapperConfiguration`; it never reads `AppBackend`, QML, or mutable GUI state. Requested, in-flight, and durable generations are monotonic. An active write may finish, but an older pending request is discarded in favor of the newest request. A completion advances durability only forward, so it cannot make a newer generation appear stale or permit a stale snapshot to overwrite a later durable write.

Writer failure leaves the GUI/runtime configuration in place, does not advance the durable generation, is retained as state for a bounded barrier, and does not emit per-request UI error spam. A later request retries naturally. On a timeout, shutdown truthfully stops waiting; it never force-terminates a live QThread. The self-owned worker may finish later rather than causing an unsafe thread destruction or an unbounded join.

## Compatibility and observability

`ConfigStore::save()` remains the low-level synchronous writer. The new timed form is used only by the coordinator when `HOTAS_RESPONSIVENESS_PROBE=1`; with the probe off, the worker calls the pre-existing untimed save path. No configuration key, JSON field, schema, QSettings location, or migration behavior changed.

The opt-in probe now records bounded distributions for GUI snapshot capture and enqueue, worker queue wait, serialization, `setValue`, `sync`, and total write time. It also reports requests, actual writes, superseded requests, latest requested/durable generations, failures, and last failed generation. This instrumentation is absent from the mapping report path.

## Measured matrix

The existing `scripts/Invoke-ResponsivenessCharacterization.ps1` ran the native-QML test-safe Flight Deck Adaptive Response slider workload (100 real pointer updates) under the four scenarios below. The harness and probe JSON remain local at `C:\hotas-builds\responsiveness-phase1\evidence-phase1`.

| Scenario | Observed CPU / disk at load | Requests / writes / superseded | GUI snapshot p95 / p99 (ms) | GUI enqueue p95 / p99 (ms) | Worker total p95 / p99 (ms) | Failure / final durable |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Idle | 17% / 1,857,066 B/s | 124 / 124 / 0 | 0.0157 / 0.0175 | 0.0002 / 0.0003 | 9.6795 / 10.0396 | 0 / 124 |
| Near-Saturation CPU | 68% / 21,836,378 B/s | 124 / 124 / 0 | 0.0238 / 0.0279 | 0.0003 / 0.0003 | 14.2899 / 15.2570 | 0 / 124 |
| Disk | 30% / 148,764,012 B/s | 124 / 112 / 12 | 0.0185 / 0.0263 | 0.0004 / 0.0005 | 145.6120 / 170.8221 | 0 / 124 |
| Combined | 100% / 16,224,676 B/s | 124 / 124 / 0 | 0.0211 / 0.0356 | 0.0003 / 0.0005 | 112.3550 / 216.6212 | 1 / 124 |

The GUI persistence boundary met the practical target in every captured run: worst snapshot p95/p99 was 0.0238/0.0356 ms, well below 2/5 ms, and no GUI-side QSettings sync appears in the asynchronous rows. Disk and Combined retain worker-side tails by design. The Combined writer reported one failure, but a later request reached durable generation 124; this is evidence of truthful failure accounting and recovery, not a claim that storage is infallible.

The Phase 0 and Phase 1 workload shapes differ, so their save values are not a valid before/after performance comparison. The comparable conclusion is architectural: Phase 0's direct GUI persistence was synchronous (Near-Saturation save total/sync 123.32/120.85 ms; Combined 49.05/46.98 ms), while Phase 1 separately observes a sub-0.036 ms GUI snapshot/queue boundary and moves the same writer work to its serial worker. Phase 1 makes no claim that this change repaired Phase 0 heartbeat/frame outliers.

## Validation

- `config_persistence_coordinator_tests`: passed. It covers 500-request coalescing/newest-wins, generation 10/11/12 stale-pending suppression, injected failure/recovery, and bounded shutdown timeout followed by clean worker stop.
- `mapping_core_tests asynchronousPersistenceFlushRoundTripsThroughConfigStore`: passed. An asynchronous request plus durable flush reopens through the unchanged ConfigStore format.
- `app_qml_startup_tests` with `HOTAS_QML_LIVE_TELEMETRY_ONLY=1`: passed. It drives the existing Flight Deck Adaptive Response tuning slider with 100 pointer updates and exports the probe only after a bounded flush.
- Candidate mapping benchmark: three samples completed with zero hot-path allocations, zero stationary writes per report, and zero curve compiles during profile-control triggers. The benchmark executable does not link the coordinator and no MappingWorker/hot-path source changed. Throughput was host-variable (linear 2.018–2.228 M reports/s; Adaptive All 8 Axes 1.249–1.576 M reports/s); because part of that range is below Phase 0's separately collected baseline range, this evidence supports the allocation/control-flow boundary but does **not** establish a throughput improvement or a fresh no-regression verdict by itself.

The full candidate target build is recorded separately after this documentation commit. Signal Flow may compile incidentally in the application module but is not run or reported as Phase 1 runtime evidence.

## Limits and Phase 2 recommendation

This is synthetic/offscreen QML plus safe host-load evidence. It is not native owner-pointer smoothness proof, physical HOTAS-to-vJoy latency proof, or live-device qualification. The host's Near-Saturation run observed 68% CPU rather than the nominal 97% target, so it must not be presented as calibrated saturation.

Phase 2, if authorized, should first obtain owner-native pointer/scroll/resize/dialog evidence and a separate physical controller -> vJoy latency observation. It should not begin in this branch or expand into rendering, scroll, Signal Flow, controller, profile activation, or mapping-policy redesign.
