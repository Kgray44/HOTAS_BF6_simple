# Adaptive Response Verification Metrics

V2.3.T reports dimensions independently. It does not compute a single score, and a favorable aggregate cannot hide a hard invariant failure.

## Hard invariants

- Non-finite telemetry: any `NaN` or `Inf` position, lead, velocity, acceleration, horizon, or confidence.
- Illegal output: a predicted axis value outside the applicable centered `[-1, 1]` or one-sided `[0, 1]` domain.
- Stationary drift: meaningful predictive lead while the trace is stationary or noise-only.
- Determinism: a scenario generated from the same identity and seed must reproduce byte-for-byte samples.

## Motion and prediction

`peakLead`, `rmsLead`, `meanHorizonMs`, `peakHorizonMs`, and `meanConfidence` characterize prediction without assuming that more lead is better. `maximumOutputStep` is retained as a discontinuity signal. The harness records physical measurement noise RMS separately from prediction/lead RMS.

## Reversal and stopping

Ground-truth reversal is derived from the continuous trajectory's velocity sign. The harness records true, detected, false, and missed reversals, plus first detection latency. `wrongDirectionLeadArea` is the time integral of lead remaining in the old direction after a real reversal; it captures severity as well as duration.

For known physical stops, `stopRecognitionMs` is the time to `Stable` and `settlingMs` is the time to near-zero lead. `falseStops`, `stableChatter`, and `dropouts` identify premature motion loss during an intended movement.

## Classifications

- `HARD`: safety/correctness invariant failed. The campaign disposition is `FAIL`.
- `BEHAVIOR`: an expected behavior was violated, such as a false reversal, missed reversal, false stop, or continuous-motion dropout. The disposition is at least `PASS WITH FINDINGS`.
- `FINDING`: an outlier needing review, such as elevated stale-direction lead area; it is not silently treated as a pass.

Metrics are aggregate artifacts, not product tuning instructions. Preset rows are informational reference runs only.
