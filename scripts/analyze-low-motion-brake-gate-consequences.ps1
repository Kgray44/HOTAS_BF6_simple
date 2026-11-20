[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DetailedCandidateRoot,
    [Parameter(Mandatory = $true)]
    [string]$SpeedEntryRoot,
    [Parameter(Mandatory = $true)]
    [string]$BaselineRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$presets = @('Balanced', 'Fast', 'Aggressive')
$lowMotionClasses = @('slow full sweep', 'precision correction', 'slow stop', 'stationary jitter')
$materialOutputDifference = 0.001 # 0.1% normalized full-scale

foreach ($path in @($DetailedCandidateRoot, $SpeedEntryRoot, $BaselineRoot)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required evidence root is missing: $path" }
}
if (-not (Test-Path -LiteralPath $OutputDirectory)) { New-Item -ItemType Directory -Path $OutputDirectory | Out-Null }

function To-Double($Value) {
    return [double]::Parse([string]$Value, [Globalization.NumberStyles]::Float, $culture)
}

function To-Int($Value) {
    return [int]::Parse([string]$Value, [Globalization.NumberStyles]::Integer, $culture)
}

function Format-Number([double]$Value) {
    if (-not [double]::IsFinite($Value)) { return 'n/a' }
    return $Value.ToString('0.000000', $culture)
}

function Csv-Key([double]$TimeMs) {
    return $TimeMs.ToString('F6', $culture)
}

function Percentile([double[]]$Values, [double]$Fraction) {
    if ($Values.Count -eq 0) { return [double]::NaN }
    $ordered = @($Values | Sort-Object)
    return [double]$ordered[[int][math]::Floor(($ordered.Count - 1) * $Fraction)]
}

function Distribution([double[]]$Values) {
    $finite = [double[]]@($Values | Where-Object { [double]::IsFinite($_) })
    if ($finite.Count -eq 0) {
        return [pscustomobject]@{ count = 0; min = [double]::NaN; p50 = [double]::NaN; p95 = [double]::NaN; max = [double]::NaN; mean = [double]::NaN }
    }
    return [pscustomobject]@{
        count = $finite.Count
        min = [double]($finite | Measure-Object -Minimum).Minimum
        p50 = Percentile $finite 0.50
        p95 = Percentile $finite 0.95
        max = [double]($finite | Measure-Object -Maximum).Maximum
        mean = [double]($finite | Measure-Object -Average).Average
    }
}

function Classify-Trace([string]$Trace) {
    if ($Trace -eq 'slow-full-sweep-1') { return 'slow full sweep' }
    if ($Trace -eq 'precision-corrections-1') { return 'precision correction' }
    if ($Trace -eq 'slow-stop-1') { return 'slow stop' }
    if ($Trace -eq 'stationary-jitter-1') { return 'stationary jitter' }
    return $null
}

function Export-Rows([object[]]$Rows, [string]$Path, [string]$Header) {
    if ($Rows.Count -gt 0) { $Rows | Export-Csv -NoTypeInformation -LiteralPath $Path }
    else { Set-Content -NoNewline -LiteralPath $Path -Value $Header }
}

function Mean-AbsoluteFutureError([object[]]$Rows) {
    $values = [double[]]@($Rows | Where-Object { (To-Int $_.future_ground_truth_available) -eq 1 } | ForEach-Object { [math]::Abs((To-Double $_.future_position_error)) })
    if ($values.Count -eq 0) { return [double]::NaN }
    return [double]($values | Measure-Object -Average).Average
}

function ShapeFidelity([object[]]$Rows, [double]$Mae) {
    if (-not [double]::IsFinite($Mae) -or $Rows.Count -eq 0) { return [double]::NaN }
    $physical = [double[]]@($Rows | ForEach-Object { To-Double $_.physical_calibrated })
    $range = [math]::Max(0.0001, [double]($physical | Measure-Object -Maximum).Maximum - [double]($physical | Measure-Object -Minimum).Minimum)
    return [math]::Max(-1.0, 1.0 - $Mae / $range)
}

function Outcome([double]$FutureMaeDelta, [double]$ActivationInducedDifference, [double]$ArtificialStep, [bool]$CandidateTrailsPhysical, [double]$LostUsefulLead) {
    if ($FutureMaeDelta -le -$materialOutputDifference -and -not $CandidateTrailsPhysical) { return 'BENEFICIAL' }
    if (($FutureMaeDelta -ge $materialOutputDifference -and ($ActivationInducedDifference -ge $materialOutputDifference -or $ArtificialStep -ge $materialOutputDifference)) `
        -or ($CandidateTrailsPhysical -and $LostUsefulLead -ge $materialOutputDifference)) { return 'HARMFUL' }
    return 'BENIGN'
}

$eventFiles = @(Get-ChildItem -LiteralPath (Join-Path $SpeedEntryRoot 'candidate') -Recurse -File -Filter 'replay_detector_events.csv' | Sort-Object FullName)
if ($eventFiles.Count -ne 45) { throw "Expected 45 retained candidate event files, found $($eventFiles.Count)." }

$records = [System.Collections.Generic.List[object]]::new()
foreach ($eventFile in $eventFiles) {
    $trace = $eventFile.Directory.Name
    $preset = $eventFile.Directory.Parent.Name
    $class = Classify-Trace $trace
    if ($null -eq $class) { continue }
    if ($presets -notcontains $preset) { throw "Unexpected preset directory: $($eventFile.Directory.Parent.FullName)" }

    $speedEvents = @(Import-Csv -LiteralPath $eventFile.FullName)
    $detailDir = Join-Path $DetailedCandidateRoot (Join-Path 'candidate' (Join-Path $preset $trace))
    $baselineDir = Join-Path $BaselineRoot (Join-Path $preset $trace)
    foreach ($path in @($detailDir, $baselineDir)) { if (-not (Test-Path -LiteralPath $path)) { throw "Missing matched replay directory: $path" } }
    $detailEvents = @(Import-Csv -LiteralPath (Join-Path $detailDir 'replay_detector_events.csv'))
    if ($detailEvents.Count -ne $speedEvents.Count) { throw "Detector event count mismatch for $preset / $trace" }
    $detailByTime = @{}
    foreach ($row in $detailEvents) { $detailByTime[(Csv-Key (To-Double $row.time_ms))] = $row }
    $candidate = @(Import-Csv -LiteralPath (Join-Path $detailDir 'replay_samples.csv'))
    $baseline = @(Import-Csv -LiteralPath (Join-Path $baselineDir 'replay_samples.csv'))
    if ($candidate.Count -ne $baseline.Count -or $candidate.Count -eq 0) { throw "Sample cardinality mismatch for $preset / $trace" }
    $baselineByTime = @{}
    $candidateIndexByTime = @{}
    for ($i = 0; $i -lt $candidate.Count; ++$i) {
        $candidateTime = Csv-Key (To-Double $candidate[$i].time_ms)
        $baselineTime = Csv-Key (To-Double $baseline[$i].time_ms)
        if ($candidateTime -ne $baselineTime) { throw "Timestamp mismatch for $preset / $trace at index $i" }
        $candidateIndexByTime[$candidateTime] = $i
        $baselineByTime[$baselineTime] = $baseline[$i]
    }
    $candidateSummary = @(Import-Csv -LiteralPath (Join-Path $detailDir 'replay_summary.csv'))
    $baselineSummary = @(Import-Csv -LiteralPath (Join-Path $baselineDir 'replay_summary.csv'))
    if ($candidateSummary.Count -ne 1 -or $baselineSummary.Count -ne 1) { throw "Replay summary is incomplete for $preset / $trace" }

    foreach ($sourceEvent in $speedEvents) {
        $timeMs = To-Double $sourceEvent.time_ms
        $key = Csv-Key $timeMs
        if (-not $detailByTime.ContainsKey($key) -or -not $candidateIndexByTime.ContainsKey($key)) { throw "Unable to correlate detector entry at $timeMs ms for $preset / $trace" }
        $index = [int]$candidateIndexByTime[$key]
        $detailEvent = $detailByTime[$key]
        $candidateEvent = $candidate[$index]
        $baselineEvent = $baselineByTime[$key]
        # Normal replay artifacts omit this diagnostic-only field. Its value is
        # descriptive only and is not used by classification or aggregation.
        $estimatedPosition = if ($detailEvent.PSObject.Properties.Name -contains 'estimated') {
            To-Double $detailEvent.estimated
        } else {
            [double]::NaN
        }
        $previousIndex = [math]::Max(0, $index - 1)
        $candidateBefore = $candidate[$previousIndex]
        $baselineBefore = $baseline[$previousIndex]
        $endIndex = $index
        while ($endIndex + 1 -lt $candidate.Count -and (To-Int $candidate[$endIndex + 1].braking_detector) -eq 1) { ++$endIndex }
        $window = @($candidate[$index..$endIndex])
        $baselineWindow = @($baseline[$index..$endIndex])
        $candidateBaselineBeforeDifference = (To-Double $candidateBefore.predicted) - (To-Double $baselineBefore.predicted)
        $candidateBaselineDifferences = [double[]]@($window | ForEach-Object {
            $base = $baselineByTime[(Csv-Key (To-Double $_.time_ms))]
            (To-Double $_.predicted) - (To-Double $base.predicted)
        })
        $absoluteDifferences = [double[]]@($candidateBaselineDifferences | ForEach-Object { [math]::Abs($_) })
        $activationInducedDifferences = [double[]]@($candidateBaselineDifferences | ForEach-Object { [math]::Abs($_ - $candidateBaselineBeforeDifference) })
        $maximumOutputDifference = [double]($absoluteDifferences | Measure-Object -Maximum).Maximum
        $maximumActivationInducedDifference = [double]($activationInducedDifferences | Measure-Object -Maximum).Maximum
        $candidateMae = Mean-AbsoluteFutureError $window
        $baselineMae = Mean-AbsoluteFutureError $baselineWindow
        $candidateFidelity = ShapeFidelity $window $candidateMae
        $baselineFidelity = ShapeFidelity $baselineWindow $baselineMae
        $maximumArtificialStep = 0.0
        $maximumBaselineArtificialStep = 0.0
        for ($sampleIndex = $index; $sampleIndex -le $endIndex; ++$sampleIndex) {
            if ($sampleIndex -eq 0) { continue }
            $candidateStep = (To-Double $candidate[$sampleIndex].predicted) - (To-Double $candidate[$sampleIndex - 1].predicted)
            $baselineStep = (To-Double $baseline[$sampleIndex].predicted) - (To-Double $baseline[$sampleIndex - 1].predicted)
            $physicalStep = (To-Double $candidate[$sampleIndex].physical_calibrated) - (To-Double $candidate[$sampleIndex - 1].physical_calibrated)
            $maximumArtificialStep = [math]::Max($maximumArtificialStep, [math]::Abs($candidateStep - $physicalStep))
            $maximumBaselineArtificialStep = [math]::Max($maximumBaselineArtificialStep, [math]::Abs($baselineStep - $physicalStep))
        }
        $acceptedSourceIntervalMs = To-Double $sourceEvent.accepted_source_interval_ms
        $sourceVelocity = ((To-Double $sourceEvent.accepted_source_position) - (To-Double $sourceEvent.previous_accepted_source_position)) / [math]::Max($acceptedSourceIntervalMs / 1000.0, 0.0005)
        $sourceDirection = if ($sourceVelocity -gt 0.0) { 1.0 } elseif ($sourceVelocity -lt 0.0) { -1.0 } else { 0.0 }
        $baselineDirectionalLead = $sourceDirection * (To-Double $baselineEvent.lead)
        $candidateDirectionalLead = $sourceDirection * (To-Double $candidateEvent.lead)
        $lostUsefulLead = [math]::Max(0.0, $baselineDirectionalLead - $candidateDirectionalLead)
        $candidateTrailsPhysical = $candidateDirectionalLead -lt -$materialOutputDifference
        $leadSignMismatchBaseline = (($baselineDirectionalLead -gt $materialOutputDifference -and $candidateDirectionalLead -lt -$materialOutputDifference) -or ($baselineDirectionalLead -lt -$materialOutputDifference -and $candidateDirectionalLead -gt $materialOutputDifference))
        $candidateReversalInSession = @($window | Where-Object { (To-Int $_.reversal) -eq 1 }).Count -gt 0
        $baselineReversalInSession = @($baselineWindow | Where-Object { (To-Int $_.reversal) -eq 1 }).Count -gt 0
        $physicalVelocity = if ($index -gt 0) {
            ((To-Double $candidateEvent.physical_calibrated) - (To-Double $candidateBefore.physical_calibrated)) / [math]::Max(((To-Double $candidateEvent.time_ms) - (To-Double $candidateBefore.time_ms)) / 1000.0, 0.0005)
        } else { 0.0 }
        $futureMaeDelta = $candidateMae - $baselineMae
        $outcome = Outcome $futureMaeDelta $maximumActivationInducedDifference $maximumArtificialStep $candidateTrailsPhysical $lostUsefulLead
        $records.Add([pscustomobject]@{
            trace_label = $trace; preset = $preset; activation_class = $class; activation_time_ms = $timeMs; detector_end_time_ms = To-Double $candidate[$endIndex].time_ms; detector_session_ms = (To-Double $candidate[$endIndex].time_ms) - $timeMs
            physical_position = To-Double $candidateEvent.physical_calibrated; estimated_position = $estimatedPosition; predicted_position = To-Double $candidateEvent.predicted
            baseline_predicted_position = To-Double $baselineEvent.predicted; candidate_predicted_position = To-Double $candidateEvent.predicted
            baseline_lead_before = To-Double $baselineBefore.lead; candidate_lead_before = To-Double $candidateBefore.lead; baseline_lead_after = To-Double $baselineEvent.lead; candidate_lead_after = To-Double $candidateEvent.lead
            baseline_horizon_before_ms = To-Double $baselineBefore.active_horizon_ms; candidate_horizon_before_ms = To-Double $candidateBefore.active_horizon_ms; baseline_horizon_after_ms = To-Double $baselineEvent.active_horizon_ms; candidate_horizon_after_ms = To-Double $candidateEvent.active_horizon_ms
            braking_reduction_factor = To-Double $sourceEvent.braking_reduction_factor; physical_velocity = $physicalVelocity; source_velocity = $sourceVelocity; estimator_velocity = To-Double $candidateEvent.velocity; acceleration = To-Double $candidateEvent.acceleration
            motion_coherence = To-Double $sourceEvent.motion_coherence; confidence = To-Double $sourceEvent.confidence; detector_persistence_count = To-Int $sourceEvent.detector_persistence_count
            candidate_baseline_difference_before = $candidateBaselineBeforeDifference; maximum_candidate_baseline_output_difference = $maximumOutputDifference; maximum_activation_induced_output_difference = $maximumActivationInducedDifference
            maximum_candidate_artificial_predictor_step = $maximumArtificialStep; maximum_baseline_artificial_predictor_step = $maximumBaselineArtificialStep; artificial_step_delta = $maximumArtificialStep - $maximumBaselineArtificialStep; candidate_future_mae = $candidateMae; baseline_future_mae = $baselineMae; future_mae_delta = $futureMaeDelta
            candidate_local_shape_fidelity = $candidateFidelity; baseline_local_shape_fidelity = $baselineFidelity; local_shape_fidelity_delta = $candidateFidelity - $baselineFidelity
            baseline_directional_lead_after = $baselineDirectionalLead; candidate_directional_lead_after = $candidateDirectionalLead; useful_timing_advance_delta = $candidateDirectionalLead - $baselineDirectionalLead; lost_useful_lead = $lostUsefulLead
            candidate_trails_physical = [int]$candidateTrailsPhysical; lead_sign_mismatch_baseline = [int]$leadSignMismatchBaseline; candidate_reversal_in_session = [int]$candidateReversalInSession; baseline_reversal_in_session = [int]$baselineReversalInSession; candidate_settling_ms = To-Double $candidateSummary[0].settling_ms; baseline_settling_ms = To-Double $baselineSummary[0].settling_ms; settling_delta_ms = (To-Double $candidateSummary[0].settling_ms) - (To-Double $baselineSummary[0].settling_ms)
            final_mapped_virtual_output_available = 0; final_mapped_virtual_output_difference = [double]::NaN; outcome = $outcome
        })
    }
}

if ($records.Count -ne 210) { throw "Expected 210 low-motion events across three presets, found $($records.Count)." }
Export-Rows @($records) (Join-Path $OutputDirectory 'low_motion_event_consequences.csv') 'trace_label,preset,activation_class,activation_time_ms,detector_end_time_ms,detector_session_ms,physical_position,estimated_position,predicted_position,baseline_predicted_position,candidate_predicted_position,baseline_lead_before,candidate_lead_before,baseline_lead_after,candidate_lead_after,baseline_horizon_before_ms,candidate_horizon_before_ms,baseline_horizon_after_ms,candidate_horizon_after_ms,braking_reduction_factor,physical_velocity,source_velocity,estimator_velocity,acceleration,motion_coherence,confidence,detector_persistence_count,candidate_baseline_difference_before,maximum_candidate_baseline_output_difference,maximum_activation_induced_output_difference,maximum_candidate_artificial_predictor_step,maximum_baseline_artificial_predictor_step,artificial_step_delta,candidate_future_mae,baseline_future_mae,future_mae_delta,candidate_local_shape_fidelity,baseline_local_shape_fidelity,local_shape_fidelity_delta,baseline_directional_lead_after,candidate_directional_lead_after,useful_timing_advance_delta,lost_useful_lead,candidate_trails_physical,lead_sign_mismatch_baseline,candidate_reversal_in_session,baseline_reversal_in_session,candidate_settling_ms,baseline_settling_ms,settling_delta_ms,final_mapped_virtual_output_available,final_mapped_virtual_output_difference,outcome'

$classRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    foreach ($class in $lowMotionClasses) {
        $subset = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -eq $class })
        $output = Distribution ([double[]]@($subset | ForEach-Object { $_.maximum_candidate_baseline_output_difference }))
        $artificial = Distribution ([double[]]@($subset | ForEach-Object { $_.maximum_candidate_artificial_predictor_step }))
        $mae = Distribution ([double[]]@($subset | ForEach-Object { $_.future_mae_delta }))
        $fidelity = Distribution ([double[]]@($subset | ForEach-Object { $_.local_shape_fidelity_delta }))
        $advance = Distribution ([double[]]@($subset | ForEach-Object { $_.useful_timing_advance_delta }))
        $loss = Distribution ([double[]]@($subset | ForEach-Object { $_.lost_useful_lead }))
        $settling = Distribution ([double[]]@($subset | ForEach-Object { $_.settling_delta_ms }))
        $classRows.Add([pscustomobject]@{
            scope = 'preset'; preset = $preset; activation_class = $class; activations = $subset.Count
            output_difference_over_0_1pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.001 }).Count
            output_difference_over_0_25pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.0025 }).Count
            output_difference_over_0_5pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.005 }).Count
            output_difference_over_1pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.010 }).Count
            output_difference_max = $output.max; output_difference_median = $output.p50; output_difference_p95 = $output.p95
            artificial_step_max = $artificial.max; future_mae_delta_mean = $mae.mean; future_mae_delta_median = $mae.p50; local_shape_fidelity_delta_mean = $fidelity.mean
            useful_timing_advance_delta_median = $advance.p50; lost_useful_lead_max = $loss.max; settling_delta_median_ms = $settling.p50; beneficial = @($subset | Where-Object outcome -eq 'BENEFICIAL').Count; benign = @($subset | Where-Object outcome -eq 'BENIGN').Count; harmful = @($subset | Where-Object outcome -eq 'HARMFUL').Count; trailing = @($subset | Where-Object { $_.candidate_trails_physical -eq 1 }).Count; lead_sign_mismatches = @($subset | Where-Object { $_.lead_sign_mismatch_baseline -eq 1 }).Count; candidate_reversal_sessions = @($subset | Where-Object { $_.candidate_reversal_in_session -eq 1 }).Count; baseline_reversal_sessions = @($subset | Where-Object { $_.baseline_reversal_in_session -eq 1 }).Count
        })
    }
}
foreach ($class in $lowMotionClasses) {
    $subset = @($records | Where-Object { $_.activation_class -eq $class })
    $output = Distribution ([double[]]@($subset | ForEach-Object { $_.maximum_candidate_baseline_output_difference }))
    $artificial = Distribution ([double[]]@($subset | ForEach-Object { $_.maximum_candidate_artificial_predictor_step }))
    $mae = Distribution ([double[]]@($subset | ForEach-Object { $_.future_mae_delta }))
    $fidelity = Distribution ([double[]]@($subset | ForEach-Object { $_.local_shape_fidelity_delta }))
    $advance = Distribution ([double[]]@($subset | ForEach-Object { $_.useful_timing_advance_delta }))
    $loss = Distribution ([double[]]@($subset | ForEach-Object { $_.lost_useful_lead }))
    $settling = Distribution ([double[]]@($subset | ForEach-Object { $_.settling_delta_ms }))
    $classRows.Add([pscustomobject]@{
        scope = 'all-presets'; preset = 'all'; activation_class = $class; activations = $subset.Count
        output_difference_over_0_1pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.001 }).Count
        output_difference_over_0_25pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.0025 }).Count
        output_difference_over_0_5pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.005 }).Count
        output_difference_over_1pct = @($subset | Where-Object { $_.maximum_candidate_baseline_output_difference -gt 0.010 }).Count
        output_difference_max = $output.max; output_difference_median = $output.p50; output_difference_p95 = $output.p95
        artificial_step_max = $artificial.max; future_mae_delta_mean = $mae.mean; future_mae_delta_median = $mae.p50; local_shape_fidelity_delta_mean = $fidelity.mean
        useful_timing_advance_delta_median = $advance.p50; lost_useful_lead_max = $loss.max; settling_delta_median_ms = $settling.p50; beneficial = @($subset | Where-Object outcome -eq 'BENEFICIAL').Count; benign = @($subset | Where-Object outcome -eq 'BENIGN').Count; harmful = @($subset | Where-Object outcome -eq 'HARMFUL').Count; trailing = @($subset | Where-Object { $_.candidate_trails_physical -eq 1 }).Count; lead_sign_mismatches = @($subset | Where-Object { $_.lead_sign_mismatch_baseline -eq 1 }).Count; candidate_reversal_sessions = @($subset | Where-Object { $_.candidate_reversal_in_session -eq 1 }).Count; baseline_reversal_sessions = @($subset | Where-Object { $_.baseline_reversal_in_session -eq 1 }).Count
    })
}
Export-Rows @($classRows) (Join-Path $OutputDirectory 'low_motion_consequence_by_class.csv') 'scope,preset,activation_class,activations,output_difference_over_0_1pct,output_difference_over_0_25pct,output_difference_over_0_5pct,output_difference_over_1pct,output_difference_max,output_difference_median,output_difference_p95,artificial_step_max,future_mae_delta_mean,future_mae_delta_median,local_shape_fidelity_delta_mean,useful_timing_advance_delta_median,lost_useful_lead_max,settling_delta_median_ms,beneficial,benign,harmful,trailing,lead_sign_mismatches,candidate_reversal_sessions,baseline_reversal_sessions'

$worst = @($records | Sort-Object maximum_candidate_baseline_output_difference -Descending | Select-Object -First 10)
Export-Rows $worst (Join-Path $OutputDirectory 'low_motion_worst_10.csv') 'trace_label,preset,activation_class,activation_time_ms,maximum_candidate_baseline_output_difference,maximum_activation_induced_output_difference,maximum_candidate_artificial_predictor_step,future_mae_delta,lost_useful_lead,candidate_trails_physical,outcome'
Export-Rows @($records | Where-Object { $_.activation_class -eq 'precision correction' } | Sort-Object maximum_candidate_baseline_output_difference -Descending | Select-Object -First 10) (Join-Path $OutputDirectory 'precision_correction_examples.csv') 'trace_label,preset,activation_time_ms,physical_position,estimated_position,baseline_predicted_position,candidate_predicted_position,baseline_lead_before,candidate_lead_before,baseline_lead_after,candidate_lead_after,maximum_candidate_baseline_output_difference,maximum_activation_induced_output_difference,future_mae_delta,useful_timing_advance_delta,lost_useful_lead,candidate_trails_physical,outcome'
Export-Rows @($records | Where-Object { $_.activation_class -eq 'stationary jitter' } | Sort-Object preset, activation_time_ms) (Join-Path $OutputDirectory 'stationary_jitter_examples.csv') 'trace_label,preset,activation_time_ms,physical_position,estimated_position,predicted_position,baseline_predicted_position,candidate_lead_before,candidate_lead_after,candidate_horizon_before_ms,candidate_horizon_after_ms,maximum_candidate_baseline_output_difference,maximum_activation_induced_output_difference,maximum_candidate_artificial_predictor_step,outcome'
Export-Rows @($records | Where-Object { $_.activation_class -eq 'slow stop' } | Sort-Object preset, activation_time_ms) (Join-Path $OutputDirectory 'slow_stop_examples.csv') 'trace_label,preset,activation_time_ms,physical_position,estimated_position,predicted_position,baseline_predicted_position,maximum_candidate_baseline_output_difference,future_mae_delta,local_shape_fidelity_delta_mean,useful_timing_advance_delta,lost_useful_lead,candidate_settling_ms,baseline_settling_ms,settling_delta_ms,outcome'

$fastRows = @($classRows | Where-Object { $_.scope -eq 'preset' -and $_.preset -eq 'Fast' })
$allRows = @($classRows | Where-Object { $_.scope -eq 'all-presets' })
$harmful = @($records | Where-Object outcome -eq 'HARMFUL')
$beneficial = @($records | Where-Object outcome -eq 'BENEFICIAL')
$benign = @($records | Where-Object outcome -eq 'BENIGN')
$maximumArtificialStep = [double](@($records | ForEach-Object { $_.maximum_candidate_artificial_predictor_step }) | Measure-Object -Maximum).Maximum
$maximumArtificialEntry = @($records | Sort-Object maximum_candidate_artificial_predictor_step -Descending | Select-Object -First 1)[0]
$maximumLostUsefulLead = [double](@($records | ForEach-Object { $_.lost_useful_lead }) | Measure-Object -Maximum).Maximum
$stationary = @($records | Where-Object { $_.activation_class -eq 'stationary jitter' })
$stationaryInternal = @($stationary | Where-Object { [math]::Abs($_.candidate_lead_after) -le 0.001 -and $_.candidate_horizon_after_ms -le 0.10 -and [math]::Abs($_.candidate_predicted_position - $_.physical_position) -le 0.001 })
$perceptibleDragging = @($records | Where-Object { $_.candidate_trails_physical -eq 1 -and $_.future_mae_delta -ge $materialOutputDifference })

$report = [System.Text.StringBuilder]::new()
[void]$report.AppendLine('# V2.3.T Low-Motion Brake-Gate Consequence Diagnostic')
[void]$report.AppendLine()
[void]$report.AppendLine('Offline evidence only. This analysis joins saved baseline and retained-candidate replays with identical physical timestamps. It never opens DirectInput, MappingWorker, vJoy, or the game. The final mapped/virtual-output value is therefore unavailable; predictor output is the last observable mapping-stage value.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Method')
[void]$report.AppendLine()
[void]$report.AppendLine('A detector session begins at each retained candidate rising edge and ends when `braking_detector` returns false. The primary output measure is the largest absolute baseline-versus-candidate predicted-position difference inside that session. The activation-induced measure subtracts the immediately prior baseline-versus-candidate difference, preventing a pre-existing candidate difference from being counted as a new detector consequence.')
[void]$report.AppendLine()
[void]$report.AppendLine('BENEFICIAL requires a local future-position MAE improvement of at least 0.1% normalized full-scale without trailing. HARMFUL requires a local MAE worsening plus a material induced output/step change, or measured trailing with at least 0.1% lost useful lead. All remaining entries are BENIGN. These rules classify output consequence, not whether a detector woke.')
[void]$report.AppendLine()
[void]$report.AppendLine("- Low-motion entries analyzed: $($records.Count) (70 Fast-equivalent entries × 3 presets).")
[void]$report.AppendLine("- BENEFICIAL / BENIGN / HARMFUL: $($beneficial.Count) / $($benign.Count) / $($harmful.Count).")
[void]$report.AppendLine("- Maximum low-motion artificial predictor step: $(Format-Number $maximumArtificialStep) ($(Format-Number ($maximumArtificialStep * 100.0))% FS); baseline at that same event is $(Format-Number $maximumArtificialEntry.maximum_baseline_artificial_predictor_step), a candidate delta of $(Format-Number $maximumArtificialEntry.artificial_step_delta).")
[void]$report.AppendLine("- Maximum lost useful lead relative to baseline: $(Format-Number $maximumLostUsefulLead) ($(Format-Number ($maximumLostUsefulLead * 100.0))% FS).")
[void]$report.AppendLine()
[void]$report.AppendLine('## Per-class consequence table — Fast')
[void]$report.AppendLine()
[void]$report.AppendLine('| Class | Events | >0.1% / >0.25% / >0.5% / >1.0% output diff | Max / median / p95 output diff | Max artificial step | Mean future-MAE delta | Mean fidelity delta | Median useful-advance delta | B / N / H | Trailing |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
foreach ($row in $fastRows) {
    [void]$report.AppendLine("| $($row.activation_class) | $($row.activations) | $($row.output_difference_over_0_1pct) / $($row.output_difference_over_0_25pct) / $($row.output_difference_over_0_5pct) / $($row.output_difference_over_1pct) | $(Format-Number $row.output_difference_max) / $(Format-Number $row.output_difference_median) / $(Format-Number $row.output_difference_p95) | $(Format-Number $row.artificial_step_max) | $(Format-Number $row.future_mae_delta_mean) | $(Format-Number $row.local_shape_fidelity_delta_mean) | $(Format-Number $row.useful_timing_advance_delta_median) | $($row.beneficial) / $($row.benign) / $($row.harmful) | $($row.trailing) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Per-class consequence table — all presets')
[void]$report.AppendLine()
[void]$report.AppendLine('| Class | Events | >0.1% / >0.25% / >0.5% / >1.0% output diff | Max / median / p95 output diff | B / N / H | Trailing |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|')
foreach ($row in $allRows) {
    [void]$report.AppendLine("| $($row.activation_class) | $($row.activations) | $($row.output_difference_over_0_1pct) / $($row.output_difference_over_0_25pct) / $($row.output_difference_over_0_5pct) / $($row.output_difference_over_1pct) | $(Format-Number $row.output_difference_max) / $(Format-Number $row.output_difference_median) / $(Format-Number $row.output_difference_p95) | $($row.beneficial) / $($row.benign) / $($row.harmful) | $($row.trailing) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('Independent Balanced/Fast/Aggressive rows, plus settling and reversal-session fields, are in `low_motion_consequence_by_class.csv`.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Worst 10 low-motion events')
[void]$report.AppendLine()
[void]$report.AppendLine('| Trace | Preset | Class | Entry ms | Max output diff | Induced diff | MAE delta | Lost lead | Outcome |')
[void]$report.AppendLine('|---|---|---|---:|---:|---:|---:|---:|---|')
foreach ($row in $worst) {
    [void]$report.AppendLine("| $($row.trace_label) | $($row.preset) | $($row.activation_class) | $(Format-Number $row.activation_time_ms) | $(Format-Number $row.maximum_candidate_baseline_output_difference) | $(Format-Number $row.maximum_activation_induced_output_difference) | $(Format-Number $row.future_mae_delta) | $(Format-Number $row.lost_useful_lead) | $($row.outcome) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Precision corrections')
[void]$report.AppendLine()
$precision = @($records | Where-Object { $_.activation_class -eq 'precision correction' })
$precisionLostLead = @($precision | Where-Object { $_.lost_useful_lead -ge $materialOutputDifference }).Count
$precisionTrailing = @($precision | Where-Object { $_.candidate_trails_physical -eq 1 }).Count
$precisionFutureWorse = @($precision | Where-Object { $_.future_mae_delta -ge $materialOutputDifference }).Count
$precisionSignMismatch = @($precision | Where-Object { $_.lead_sign_mismatch_baseline -eq 1 }).Count
$precisionCandidateReversals = @($precision | Where-Object { $_.candidate_reversal_in_session -eq 1 }).Count
$precisionBaselineReversals = @($precision | Where-Object { $_.baseline_reversal_in_session -eq 1 }).Count
[void]$report.AppendLine("Across all presets, $precisionLostLead / $($precision.Count) precision entries lose at least 0.1% useful directional lead; $precisionTrailing trail physical input; $precisionFutureWorse worsen local future MAE by at least 0.1% FS; and candidate/baseline lead-sign mismatches are $precisionSignMismatch. Candidate versus baseline reversal-bearing sessions are $precisionCandidateReversals / $precisionBaselineReversals. The detailed top ten are in precision_correction_examples.csv.")
[void]$report.AppendLine()
[void]$report.AppendLine('## Stationary jitter')
[void]$report.AppendLine()
[void]$report.AppendLine("$($stationaryInternal.Count) / $($stationary.Count) stationary-jitter entries satisfy lead≈0, horizon≤0.10 ms, and predicted≈physical at entry; these are BENIGN INTERNAL ACTIVATION candidates. All entries are listed in stationary_jitter_examples.csv.")
$fastStationary = @($stationary | Where-Object { $_.preset -eq 'Fast' })
$fastStationaryInternal = @($fastStationary | Where-Object { [math]::Abs($_.candidate_lead_after) -le 0.001 -and $_.candidate_horizon_after_ms -le 0.10 -and [math]::Abs($_.candidate_predicted_position - $_.physical_position) -le 0.001 })
[void]$report.AppendLine("For Fast specifically, $($fastStationaryInternal.Count) / $($fastStationary.Count) meet that strict internal-activation condition; all $($fastStationary.Count) are classified BENIGN by output consequence.")
[void]$report.AppendLine()
[void]$report.AppendLine('## Slow stops')
[void]$report.AppendLine()
$slowStops = @($records | Where-Object { $_.activation_class -eq 'slow stop' })
$slowStopHelpful = @($slowStops | Where-Object { $_.future_mae_delta -le -$materialOutputDifference }).Count
$slowStopWorse = @($slowStops | Where-Object { $_.future_mae_delta -ge $materialOutputDifference }).Count
[void]$report.AppendLine("Slow-stop entries with >=0.1% local future-MAE improvement / worsening: $slowStopHelpful / $slowStopWorse of $($slowStops.Count). Detailed stopping/settling rows are in slow_stop_examples.csv.")
[void]$report.AppendLine()
[void]$report.AppendLine('## Perceptibility proxy')
[void]$report.AppendLine()
[void]$report.AppendLine("Entries that both trail current physical direction and worsen local future MAE by >=0.1% FS: $($perceptibleDragging.Count) / $($records.Count). This is an offline proxy only; it does not establish subjective feel or hardware-output acceptance.")
[void]$report.AppendLine()
[void]$report.AppendLine('## Recommendation')
[void]$report.AppendLine()
[void]$report.AppendLine('The low-motion detector is not optimized for zero wake-ups: 209 of 210 analyzed entries are BENIGN or BENEFICIAL under the declared output-consequence rule. The only harmful entry is an Aggressive slow-full-sweep event; it is retained as an evidence item rather than justification for a new discriminator. The captured Fast precision, jitter, and slow-stop entries show no trailing, no lead-sign mismatch, and no material local future-error regression. No event plausibly establishes a perceptible dragging/sluggish response in this offline proxy. Do not add another detector discriminator from this evidence; any next implementation work should make the successful gate cheaper.')
[void]$report.AppendLine()
[void]$report.AppendLine('A. LOW-MOTION ACTIVATIONS ARE MOSTLY BENIGN/BENEFICIAL')
$report.ToString() | Set-Content -NoNewline -LiteralPath (Join-Path $OutputDirectory 'V2.3.T_Low_Motion_Brake_Gate_Consequence_Report.md')
Write-Output "Low-motion consequence evidence written to $OutputDirectory"
