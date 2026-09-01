# V2.3.T — Adaptive Response Verification

V2.3.T is an internal, offline verification workstream for the V2.3.0 Adaptive Response estimator. It is not a product version, release, installer, tag, deployment, or preset-tuning pass. The harness uses the production `AdaptiveResponseProcessor` and runtime configuration data, but is a separate executable; it never runs from `MappingWorker` or the DirectInput-to-vJoy report path.

## Build and run

Configure a normal Release build, then build the `adaptive_response_verification` target. The target is enabled by `HOTAS_BUILD_ADAPTIVE_RESPONSE_VERIFICATION` (default `ON`).

```powershell
cmake --build build --config Release --target adaptive_response_verification
.\build\Release\adaptive_response_verification.exe --campaign smoke
```

The campaign tiers are deliberately staged:

- `smoke` is the CI-friendly core: stationary, slow movement, reversal, stop, sample-and-hold, noise, and timing probes.
- `canonical` runs the deterministic catalog, including the complete slow-motion rate matrix and source/mapper sample-hold matrix.
- `torture` adds 5,000 deterministic, physically continuous randomized pilot traces by default.
- `full` adds 50,000 deterministic randomized traces by default. It remains an offline manual command; it is not part of ordinary CI.

All commands accept `--seed 0xBFA62300`, `--scenario <substring>`, `--model <auto|velocity|alpha-beta|alpha-beta-gamma|off|all|presets>`, `--sample-rate <Hz>`, `--random-count <N>`, `--jobs <N>`, and `--output <directory>`. `--jobs 1` preserves single-threaded debugging; independent scenario/configuration runs may use more workers while retaining deterministic task order and seed identities. A rerun should use the recorded scenario id, configuration, and seed.

```powershell
.\build\Release\adaptive_response_verification.exe --campaign canonical --scenario slow/sweep-3.000000 --model auto --seed 0x4A91BF27
.\build\Release\adaptive_response_verification.exe --campaign torture --output artifacts\adaptive_response_validation\torture-review
```

To compare compatible completed campaigns:

```powershell
.\build\Release\adaptive_response_verification.exe --compare <baseline-directory> <candidate-directory> --output comparison.md
```

## Evidence boundaries

The runner proves deterministic synthetic estimator behavior and produces CPU-side performance measurements for the offline runner. It does not prove rendered GUI behavior or physical HOTAS/vJoy behavior. Production report-path allocation evidence remains the responsibility of `mapping_hot_path_benchmark`, which continues to be run in CI.

The current result schema is `adaptiveVerificationSchemaVersion: 2` with `adaptiveScenarioCatalogVersion: 2`. Stored provenance includes the source and harness commits/branches, application version, campaign tier, seed, configuration list, worker count, random-count override, OS/compiler, command shape, scenario counts, and sample counts.

Each completed campaign writes specialized, importable CSV evidence for sample-rate invariance, slow motion, source sample-hold, reversals, stops, noise, motion states, acceleration, multi-axis isolation, automation, lifecycle, bumpless transitions, performance, failures, and seeds. `worst_cases.csv` ranks the top ten scenarios independently for stale-lead area, peak lead, injected-noise amplification, dropout duration, false-stop duration, settling, overshoot, and output step. The matching traces are deterministically replayed and retained, as are up to ten deterministic triage traces per distinct failure category; all other successful and failing randomized runs remain summary-only to bound memory and artifact size. A Canonical run additionally writes `canonical_finding_clusters.csv`, which preserves the exact classification clusters and their model/preset, scenario, cadence, trajectory, human-intent, reversal-quality, dropout, false-stop, and noise dimensions.

The event chain has three explicit layers. **Human intent** is the authored continuous trajectory before cadence, sample-and-hold, quantization, and injected noise. **Observed device** is the held/noisy sample received by the mapper. **Predictor output** is the estimator telemetry under test. Reversal timing remains configuration-independent: an observed-device reversal needs two coherent, meaningful source updates and never consults the tested model or preset's reversal setting. Human reversals are then matched to observed-device reversals, so device-only noise and unreached intent are not mislabeled as predictor failure. `reversal_events.csv` records that linkage plus detection/reacquisition latency and the Immediate/Excellent/Acceptable/Poor/Failure quality bands. Canonical runs also emit `Adaptive_Response_V2.3.T_Canonical_Findings_Analysis.md`, with categories, model/preset/family/rate breakdowns, retained traces, root causes, the integration disposition gate, and next action for each category.

Slow-motion policy treats the full-scale `+100% -> 0%` command over three seconds as the strict requirement. Five- and ten-second sweeps are grace tiers: they may use direct behavior rather than continuous prediction at microscopic speed, but must recognize coherent motion, stay stable, and avoid false reversals, repeated chatter, and noise amplification.

Production-backed multi-axis, lifecycle, automation-composition, and V2.2 bumpless-transfer checks are campaign gates. Their hard failures are included in `summary.json`, `summary.csv`, `campaign.json`, `failures.csv`, and `integration_summary.json`; a campaign with an integration hard failure cannot report a passing disposition.

## CI

`adaptive_response_verification_tests` self-validates the harness. `adaptive_response_verification_smoke` runs the smoke campaign as a CTest target, so existing CI remains compact while the manual tiers stay available for escalation.
