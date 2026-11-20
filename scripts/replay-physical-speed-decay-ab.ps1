[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureDirectory,
    [string]$OutputDirectory = (Join-Path (Get-Location) 'artifacts\adaptive_response_validation\v2.3.T-physical-ab'),
    [string]$BaselineExe = (Join-Path (Get-Location) 'build-v2.3.T-local\Release\adaptive_physical_trace_replay_baseline.exe'),
    [string]$CandidateExe = (Join-Path (Get-Location) 'build-v2.3.T-local\Release\adaptive_physical_trace_replay_candidate.exe'),
    [string]$QtBin = 'C:\Users\kkids\Documents\HOTAS_BF6\.toolchain\Qt\6.8.3\msvc2022_64\bin'
)

$ErrorActionPreference = 'Stop'

foreach ($path in @($CaptureDirectory, $BaselineExe, $CandidateExe, $QtBin)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required path is missing: $path" }
}
$env:Path = "$QtBin;$env:Path"

$captures = @(Get-ChildItem -LiteralPath $CaptureDirectory -File -Filter '*.physical.csv' | Sort-Object Name)
if ($captures.Count -eq 0) { throw "No *.physical.csv traces were found in $CaptureDirectory" }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$presets = @('Balanced', 'Fast', 'Aggressive')
$summaryRows = [System.Collections.Generic.List[object]]::new()
$turnRows = [System.Collections.Generic.List[object]]::new()
$safetyObservations = [System.Collections.Generic.List[object]]::new()

function Read-OneRow([string]$Path) {
    $rows = @(Import-Csv -LiteralPath $Path)
    if ($rows.Count -ne 1) { throw "Expected one replay summary row in $Path" }
    return $rows[0]
}

function To-Double($Value) {
    return [double]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Export-CsvRows([System.Collections.IEnumerable]$Rows, [string]$Path, [string]$Header) {
    $materialized = @($Rows)
    if ($materialized.Count -gt 0) {
        $materialized | Export-Csv -NoTypeInformation -LiteralPath $Path
    } else {
        Set-Content -NoNewline -LiteralPath $Path -Value $Header
    }
}

foreach ($capture in $captures) {
    foreach ($preset in $presets) {
        $traceStem = [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($capture.Name))
        $baselineDir = Join-Path $OutputDirectory (Join-Path 'baseline' (Join-Path $preset $traceStem))
        $candidateDir = Join-Path $OutputDirectory (Join-Path 'candidate' (Join-Path $preset $traceStem))
        & $BaselineExe --input $capture.FullName --preset $preset --variant baseline --output $baselineDir
        if ($LASTEXITCODE -ne 0) { throw "Baseline replay failed for $($capture.Name) / $preset ($LASTEXITCODE)" }
        & $CandidateExe --input $capture.FullName --preset $preset --variant candidate --output $candidateDir
        if ($LASTEXITCODE -ne 0) { throw "Candidate replay failed for $($capture.Name) / $preset ($LASTEXITCODE)" }
        $baseline = Read-OneRow (Join-Path $baselineDir 'replay_summary.csv')
        $candidate = Read-OneRow (Join-Path $candidateDir 'replay_summary.csv')
        $safetySensitive = $baseline.trace_label -match '(?i)(jitter|hold|precision|slow)'
        $summaryRows.Add([pscustomobject]@{
            trace_label = $baseline.trace_label
            trace_file = $capture.Name
            preset = $preset
            safety_sensitive_label = [int]$safetySensitive
            samples = $baseline.samples
            duration_ms = $baseline.duration_ms
            median_source_cadence_hz = $baseline.median_source_cadence_hz
            local_physical_turns = $baseline.local_physical_turns
            baseline_future_mae = $baseline.mean_absolute_future_position_error
            candidate_future_mae = $candidate.mean_absolute_future_position_error
            future_mae_delta = (To-Double $candidate.mean_absolute_future_position_error) - (To-Double $baseline.mean_absolute_future_position_error)
            baseline_future_p95 = $baseline.p95_future_position_error
            candidate_future_p95 = $candidate.p95_future_position_error
            baseline_future_max = $baseline.max_future_position_error
            candidate_future_max = $candidate.max_future_position_error
            baseline_shape_fidelity = $baseline.shape_time_shift_fidelity
            candidate_shape_fidelity = $candidate.shape_time_shift_fidelity
            shape_fidelity_delta = (To-Double $candidate.shape_time_shift_fidelity) - (To-Double $baseline.shape_time_shift_fidelity)
            baseline_artificial_step = $baseline.max_predictor_only_artificial_step
            candidate_artificial_step = $candidate.max_predictor_only_artificial_step
            artificial_step_delta = (To-Double $candidate.max_predictor_only_artificial_step) - (To-Double $baseline.max_predictor_only_artificial_step)
            baseline_settling_ms = $baseline.settling_ms
            candidate_settling_ms = $candidate.settling_ms
            baseline_false_reversals = $baseline.false_reversals
            candidate_false_reversals = $candidate.false_reversals
            candidate_detector_activation_samples = $candidate.detector_activation_samples
            candidate_detector_activation_events = $candidate.detector_activation_events
            baseline_output = $baselineDir
            candidate_output = $candidateDir
        })
        $baselineTurns = @(Import-Csv -LiteralPath (Join-Path $baselineDir 'replay_turns.csv'))
        $candidateTurns = @{}
        foreach ($turn in @(Import-Csv -LiteralPath (Join-Path $candidateDir 'replay_turns.csv'))) {
            $candidateTurns[[string]$turn.turn_index] = $turn
        }
        foreach ($turn in $baselineTurns) {
            $candidateTurn = $candidateTurns[[string]$turn.turn_index]
            if ($null -eq $candidateTurn) { throw "Candidate turn $($turn.turn_index) is missing for $($capture.Name) / $preset" }
            $turnRows.Add([pscustomobject]@{
                trace_label = $turn.trace_label
                trace_file = $capture.Name
                preset = $preset
                turn_index = $turn.turn_index
                physical_turn_ms = $turn.physical_turn_ms
                previous_direction = $turn.previous_direction
                next_direction = $turn.next_direction
                physical_peak_at_turn = $turn.physical_peak_at_turn
                baseline_prediction_at_turn = $turn.predicted_at_turn
                candidate_prediction_at_turn = $candidateTurn.predicted_at_turn
                baseline_predicted_peak_value = $turn.predicted_peak_value
                candidate_predicted_peak_value = $candidateTurn.predicted_peak_value
                baseline_predicted_peak_ms = $turn.predicted_peak_ms
                candidate_predicted_peak_ms = $candidateTurn.predicted_peak_ms
                baseline_stale_lead = $turn.lead_at_turn
                candidate_stale_lead = $candidateTurn.lead_at_turn
                baseline_peak_amplitude_error = $turn.peak_amplitude_error
                candidate_peak_amplitude_error = $candidateTurn.peak_amplitude_error
                peak_amplitude_error_delta = (To-Double $candidateTurn.peak_amplitude_error) - (To-Double $turn.peak_amplitude_error)
                baseline_peak_timing_advance_ms = $turn.peak_timing_advance_ms
                candidate_peak_timing_advance_ms = $candidateTurn.peak_timing_advance_ms
                candidate_detector_activation_ms = $candidateTurn.detector_activation_ms
                candidate_warning_before_turn_ms = $candidateTurn.warning_before_turn_ms
                baseline_stale_lead_collapse_ms = $turn.stale_lead_collapse_ms
                candidate_stale_lead_collapse_ms = $candidateTurn.stale_lead_collapse_ms
                baseline_opposite_reacquisition_ms = $turn.opposite_lead_reacquisition_ms
                candidate_opposite_reacquisition_ms = $candidateTurn.opposite_lead_reacquisition_ms
            })
        }
        if ($safetySensitive) {
            foreach ($event in @(Import-Csv -LiteralPath (Join-Path $candidateDir 'replay_detector_events.csv'))) {
                $safetyObservations.Add([pscustomobject]@{
                    trace_label = $baseline.trace_label
                    trace_file = $capture.Name
                    preset = $preset
                    reason = 'Safety-sensitive label: review whether this gate activation was a true braking event.'
                    time_ms = $event.time_ms
                    physical_calibrated = $event.physical_calibrated
                    source_changed = $event.source_changed
                    velocity = $event.velocity
                    acceleration = $event.acceleration
                    lead = $event.lead
                    active_horizon_ms = $event.active_horizon_ms
                    braking_reduction_factor = $event.braking_reduction_factor
                    previous_sample_hold = $event.previous_sample_hold
                })
            }
        }
    }
}

Export-CsvRows $summaryRows (Join-Path $OutputDirectory 'physical_ab_summary.csv') 'trace_label,trace_file,preset,safety_sensitive_label,samples,duration_ms,median_source_cadence_hz,local_physical_turns,baseline_future_mae,candidate_future_mae,future_mae_delta,baseline_future_p95,candidate_future_p95,baseline_future_max,candidate_future_max,baseline_shape_fidelity,candidate_shape_fidelity,shape_fidelity_delta,baseline_artificial_step,candidate_artificial_step,artificial_step_delta,baseline_settling_ms,candidate_settling_ms,baseline_false_reversals,candidate_false_reversals,candidate_detector_activation_samples,candidate_detector_activation_events,baseline_output,candidate_output'
Export-CsvRows $turnRows (Join-Path $OutputDirectory 'physical_ab_turns.csv') 'trace_label,trace_file,preset,turn_index,physical_turn_ms,previous_direction,next_direction,physical_peak_at_turn,baseline_prediction_at_turn,candidate_prediction_at_turn,baseline_predicted_peak_value,candidate_predicted_peak_value,baseline_predicted_peak_ms,candidate_predicted_peak_ms,baseline_stale_lead,candidate_stale_lead,baseline_peak_amplitude_error,candidate_peak_amplitude_error,peak_amplitude_error_delta,baseline_peak_timing_advance_ms,candidate_peak_timing_advance_ms,candidate_detector_activation_ms,candidate_warning_before_turn_ms,baseline_stale_lead_collapse_ms,candidate_stale_lead_collapse_ms,baseline_opposite_reacquisition_ms,candidate_opposite_reacquisition_ms'
Export-CsvRows $safetyObservations (Join-Path $OutputDirectory 'physical_ab_potential_false_activations.csv') 'trace_label,trace_file,preset,reason,time_ms,physical_calibrated,source_changed,velocity,acceleration,lead,active_horizon_ms,braking_reduction_factor,previous_sample_hold'

$turnImprovements = @($turnRows | Where-Object { (To-Double $_.peak_amplitude_error_delta) -lt 0 })
$turnRegressions = @($turnRows | Where-Object { (To-Double $_.peak_amplitude_error_delta) -gt 0 })
$falseGateEvents = $safetyObservations.Count
$bestTurn = $turnRows | Sort-Object { To-Double $_.peak_amplitude_error_delta } | Select-Object -First 1
$worstTurn = $turnRows | Sort-Object { To-Double $_.peak_amplitude_error_delta } -Descending | Select-Object -First 1
$traceScores = @($turnRows | Group-Object trace_label, preset | ForEach-Object {
    $deltas = @($_.Group | ForEach-Object { To-Double $_.peak_amplitude_error_delta })
    [pscustomobject]@{
        trace_label = $_.Group[0].trace_label
        preset = $_.Group[0].preset
        turns = $_.Count
        mean_peak_amplitude_error_delta = ($deltas | Measure-Object -Average).Average
    }
})
Export-CsvRows $traceScores (Join-Path $OutputDirectory 'physical_ab_trace_scores.csv') 'trace_label,preset,turns,mean_peak_amplitude_error_delta'
$bestTrace = $traceScores | Sort-Object mean_peak_amplitude_error_delta | Select-Object -First 1
$worstTrace = $traceScores | Sort-Object mean_peak_amplitude_error_delta -Descending | Select-Object -First 1

$report = [System.Text.StringBuilder]::new()
[void]$report.AppendLine('# V2.3.T Physical HOTAS Trace Replay — A/B Evidence')
[void]$report.AppendLine()
[void]$report.AppendLine('Offline replay only. Raw physical timestamp/cadence is preserved; this script never opens DirectInput, MappingWorker, or vJoy.')
[void]$report.AppendLine()
[void]$report.AppendLine("- Physical trace files: $($captures.Count)")
[void]$report.AppendLine("- Preset replays: $($summaryRows.Count)")
[void]$report.AppendLine("- Local physical turns: $($turnRows.Count)")
[void]$report.AppendLine("- Candidate peak-amplitude-error improvements / regressions: $($turnImprovements.Count) / $($turnRegressions.Count)")
[void]$report.AppendLine("- Potential false-activation observations on jitter/hold/precision/slow labels: $falseGateEvents (requires trace review)")
[void]$report.AppendLine()
[void]$report.AppendLine('## Per-trace A/B summary')
[void]$report.AppendLine()
[void]$report.AppendLine('| Trace | Preset | Turns | Future MAE B → C | Fidelity B → C | Artificial step B → C | Candidate detector events |')
[void]$report.AppendLine('|---|---|---:|---:|---:|---:|---:|')
foreach ($row in $summaryRows) {
    [void]$report.AppendLine("| $($row.trace_label) | $($row.preset) | $($row.local_physical_turns) | $($row.baseline_future_mae) → $($row.candidate_future_mae) | $($row.baseline_shape_fidelity) → $($row.candidate_shape_fidelity) | $($row.baseline_artificial_step) → $($row.candidate_artificial_step) | $($row.candidate_detector_activation_events) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Turning points')
[void]$report.AppendLine()
[void]$report.AppendLine('| Trace | Preset | Turn ms | Physical peak | Baseline lead → Candidate lead | Peak amplitude error B → C | Candidate warning ms | Reacquire B → C ms |')
[void]$report.AppendLine('|---|---|---:|---:|---:|---:|---:|---:|')
foreach ($row in $turnRows) {
    [void]$report.AppendLine("| $($row.trace_label) | $($row.preset) | $($row.physical_turn_ms) | $($row.physical_peak_at_turn) | $($row.baseline_stale_lead) → $($row.candidate_stale_lead) | $($row.baseline_peak_amplitude_error) → $($row.candidate_peak_amplitude_error) | $($row.candidate_warning_before_turn_ms) | $($row.baseline_opposite_reacquisition_ms) → $($row.candidate_opposite_reacquisition_ms) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Best and worst observed turns')
[void]$report.AppendLine()
if ($bestTurn) { [void]$report.AppendLine("- Best candidate peak-amplitude-error delta: $($bestTurn.trace_label) / $($bestTurn.preset) turn $($bestTurn.turn_index): $($bestTurn.peak_amplitude_error_delta).") }
if ($worstTurn) { [void]$report.AppendLine("- Worst candidate peak-amplitude-error delta: $($worstTurn.trace_label) / $($worstTurn.preset) turn $($worstTurn.turn_index): $($worstTurn.peak_amplitude_error_delta).") }
if ($bestTrace) { [void]$report.AppendLine("- Best observed trace by mean turn peak-amplitude-error delta: $($bestTrace.trace_label) / $($bestTrace.preset): $($bestTrace.mean_peak_amplitude_error_delta).") }
if ($worstTrace) { [void]$report.AppendLine("- Worst observed trace by mean turn peak-amplitude-error delta: $($worstTrace.trace_label) / $($worstTrace.preset): $($worstTrace.mean_peak_amplitude_error_delta).") }
[void]$report.AppendLine()
[void]$report.AppendLine('## Disposition')
[void]$report.AppendLine()
[void]$report.AppendLine('INCONCLUSIVE until the evidence owner reviews the labelled raw traces, verifies the capture set covers normal/slow motion and reversals, and confirms whether any safety-sensitive detector events are true false activations. Do not integrate or tune from this generated report alone.')

$report.ToString() | Set-Content -NoNewline -LiteralPath (Join-Path $OutputDirectory 'V2.3.T_Physical_Trace_AB_Report.md')
Write-Output "Offline A/B evidence written to $OutputDirectory"
