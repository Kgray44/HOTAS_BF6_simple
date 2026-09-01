# Adaptive Response Verification Metrics

V2.3.T reports dimensions independently. It does not compute a single score, and a favorable aggregate cannot hide a hard invariant failure.

## Hard invariants

- Non-finite telemetry: any `NaN` or `Inf` position, lead, velocity, acceleration, horizon, or confidence.
- Illegal output: a predicted axis value outside the applicable centered `[-1, 1]` or one-sided `[0, 1]` domain.
- Stationary drift: meaningful predictive lead while the trace is stationary or noise-only.
- Determinism: a scenario generated from the same identity and seed must reproduce byte-for-byte samples.

## Motion and prediction

`meanLead`, `medianLead`, `p95Lead`, `peakLead`, `rmsLead`, `meanHorizonMs`, `medianHorizonMs`, `p95HorizonMs`, `peakHorizonMs`, and `meanConfidence` characterize prediction without assuming that more lead is better. `maximumOutputStep` is retained as a discontinuity signal. Dropout and false-stop intervals retain count, total duration, and longest duration.

## Reversal and stopping

Ground-truth reversal is derived from a meaningful continuous-trajectory velocity sign, deliberately rejecting micro-glitches used by false-reversal bait. Event-level evidence records every true reversal, detection, scaled source/rate-aware latency, stale-lead cancellation, opposite-direction reacquisition, peak stale lead, and wrong-direction lead area. `wrongDirectionLeadArea` is the time integral of lead remaining in the old direction after a real reversal; it captures severity as well as duration.

For known physical stops, `physicalStopTimeMs`, `leadAtPhysicalStop`, `horizonAtPhysicalStopMs`, `stopRecognitionMs`, and `settlingMs` distinguish the physical event from the estimator response. Target overshoot is measured relative to the intended stop target, including peak, duration, and integrated area. `falseStops`, `stableChatter`, and `dropouts` identify premature motion loss during an intended movement.

Physical noise is `physical - intended`; predicted noise is `predicted - intended`. The reported amplification ratio divides predicted noise RMS by physical noise RMS only when input noise is nonzero. Sample-hold metrics record independent source cadence, effective source rate, cadence error, horizon/confidence oscillation, and actual interval-weighted state durations.

## Classifications

- `HARD`: safety/correctness invariant failed. The campaign disposition is `FAIL`.
- `BEHAVIOR`: an expected behavior was violated, such as a false reversal, missed reversal, false stop, or continuous-motion dropout. The disposition is at least `PASS WITH FINDINGS`.
- `FINDING`: an outlier needing review, such as elevated stale-direction lead area; it is not silently treated as a pass.

Metrics are aggregate artifacts, not product tuning instructions. Preset rows are informational reference runs only.
