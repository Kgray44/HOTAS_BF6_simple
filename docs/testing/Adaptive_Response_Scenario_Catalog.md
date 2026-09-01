# Adaptive Response V2.3.T Scenario Catalog

Every trajectory is defined as a continuous, time-parameterized piecewise-linear path before the harness samples it. Mapper rate, physical source rate, noise, and timing variation are then applied independently. Randomized scenarios use deterministic child seeds derived from master seed, campaign, family, scenario index, and configuration.

| Family | Coverage |
|---|---|
| Stationary and precision | Center/off-center holds and 1%, 2%, and 5% micro-corrections. |
| Slow and fast movement | Full/partial centered sweeps from 50 ms through 10 s, including the critical 3 s, 5 s, and 10 s deliberate sweeps. |
| Reversal and stop | Cross-center, same-side, repeated reversals, then abrupt endpoint/center/off-center stops. |
| Oscillation | 1–30 Hz sine-based movement for control fighting and human wobble exposure. |
| Sample-and-hold | Mapper rates 125/200/250/500/1000 Hz crossed with source rates 30/60/100/125/200/250/500 Hz. |
| Noise | Bounded jitter, quantization, single positive/opposite spikes, bursts, and drift. |
| Noise while moving and human wobble | Noise overlays on deliberate motion plus 0.05%–1% low-amplitude 3–30 Hz wobble over a three-second sweep. |
| Timing | Deterministic variable report intervals while retaining one physical continuous trajectory. |
| One-sided | Throttle-style 0–1 sweeps and endpoint behavior, isolated from centered-axis assumptions. |
| Acceleration and personas | Braking/acceleration fixtures plus jerk-limited Precision Pilot, Fixed-Wing, Helicopter Landing, Combat Helicopter, Space Sim, and Noisy/Older Sensor trajectories. Torture and Full expand the randomized family. |

The catalog's `sample-hold`, slow-motion, noise, and randomized families are intended to expose false stationary classification, repeated derivative reset, false reversal chatter, and prediction dropout. The generated traces are not independent random positions per report.

## Artifacts and trace retention

By default, results are written under `artifacts/adaptive_response_validation/<campaign-id>/`, which is ignored by Git. Each directory contains campaign/summary JSON, aggregate CSVs by scenario family and model, individual scenario results, seed identities, failure and worst-case tables, sample-rate/sample-hold/reversal/stop/noise metric views, performance measurements, and comparison-ready schema metadata.

Full time-series CSV traces are retained for representative canonical cases and every failed scenario. Successful randomized samples are otherwise summarized, preventing a campaign from becoming a raw-sample data landfill.

The real HOTAS replay format is CSV with the header `timestamp_us,axis_x,axis_y,axis_z,axis_rx,axis_ry,axis_rz,slider0,slider1`. It requires strictly increasing timestamps and finite normalized values. The harness can export, import, and convert an axis from this format into a deterministic replay scenario without requiring physical hardware.
