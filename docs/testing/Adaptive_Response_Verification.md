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
- `torture` adds 512 deterministic, physically continuous randomized pilot traces.
- `full` adds 5,000 deterministic randomized traces. It remains an offline manual command; it is not part of ordinary CI.

All commands accept `--seed 0xBFA62300`, `--scenario <substring>`, `--model <auto|velocity|alpha-beta|alpha-beta-gamma|off|all|presets>`, `--sample-rate <Hz>`, and `--output <directory>`. A rerun should use the recorded scenario id, configuration, and seed.

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

The V2.3.T result schema is `adaptiveVerificationSchemaVersion: 1`. The stored provenance includes source commit/branch, application version, campaign tier, seed, command shape, scenario counts, and sample counts.

## CI

`adaptive_response_verification_tests` self-validates the harness. `adaptive_response_verification_smoke` runs the smoke campaign as a CTest target, so existing CI remains compact while the manual tiers stay available for escalation.
