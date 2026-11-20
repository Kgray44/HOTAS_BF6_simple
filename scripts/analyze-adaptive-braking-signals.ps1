param(
    [string]$TraceDirectory = 'artifacts/adaptive_response_validation/v2.3.T-braking-signal-20260901/braking_signal_traces',
    [string]$ResultsCsv = 'artifacts/adaptive_response_validation/v2.3.T-braking-signal-20260901/turning_point_results.csv',
    [string]$OutputDirectory = 'artifacts/adaptive_response_validation/v2.3.T-braking-signal-20260901/braking_signal_analysis',
    [string[]]$ControlDirectories = @(
        'artifacts/adaptive_response_validation/v2.3.T-braking-signal-sample-hold-coherence-30',
        'artifacts/adaptive_response_validation/v2.3.T-braking-signal-sample-hold-coherence-60',
        'artifacts/adaptive_response_validation/v2.3.T-braking-signal-jitter-coherence',
        'artifacts/adaptive_response_validation/v2.3.T-braking-signal-opposite-spike-coherence',
        'artifacts/adaptive_response_validation/v2.3.T-braking-signal-false-bait-coherence'
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Deliberately diagnostic-only thresholds.  They are not wired into the model.
$velocityFloor = 0.20
$coherenceFloor = 0.62
$oneUpdateDecayRatio = 0.985
$fourUpdateDecayRatio = 0.95
$warmupMs = 200.0
$selectedPresets = @('Balanced', 'Fast', 'Aggressive')
$candidateNames = @('abg_opposing', 'source_decay_1', 'source_decay_2', 'source_decay_4')

function Number([object]$value) { return [double]::Parse([string]$value, [Globalization.CultureInfo]::InvariantCulture) }
function Flag([object]$value) { return [string]$value -eq '1' -or [string]$value -eq 'true' }
function Median([double[]]$values) {
    if ($values.Count -eq 0) { return $null }
    $ordered = @($values | Sort-Object)
    $middle = [int]($ordered.Count / 2)
    if ($ordered.Count % 2) { return $ordered[$middle] }
    return ($ordered[$middle - 1] + $ordered[$middle]) / 2.0
}
function Percentile([double[]]$values, [double]$fraction) {
    if ($values.Count -eq 0) { return $null }
    $ordered = @($values | Sort-Object)
    $index = [int][Math]::Ceiling($fraction * $ordered.Count) - 1
    return $ordered[[Math]::Max(0, [Math]::Min($index, $ordered.Count - 1))]
}
function Average([double[]]$values) {
    if ($values.Count -eq 0) { return $null }
    return ($values | Measure-Object -Average).Average
}
function TextNumber([object]$value) {
    if ($null -eq $value) { return '' }
    return ([double]$value).ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
}
function BrakeStartMs([string]$speed, [string]$brake) {
    $duration = @{ slow = 1.20; medium = 0.60; fast = 0.30; 'very-fast' = 0.10 }[$speed]
    $brakeDistance = if ($brake -eq 'gentle') { 0.55 } else { 0.20 }
    $brakeSeconds = $duration / (1.0 + (1.0 / $brakeDistance - 1.0) / 2.0)
    return 1000.0 * (0.20 + $duration - $brakeSeconds)
}
function Add-Ring([System.Collections.Generic.List[object]]$items, [object]$entry) {
    $items.Add($entry)
    if ($items.Count -gt 4) { $items.RemoveAt(0) }
}
function New-CandidateState {
    return [ordered]@{
        abg_opposing = $false
        source_decay_1 = $false
        source_decay_2 = $false
        source_decay_4 = $false
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$metadataByKey = @{}
foreach ($result in Import-Csv -LiteralPath $ResultsCsv) {
    if ($selectedPresets -notcontains $result.preset) { continue }
    $key = "$($result.trajectory)|$($result.preset)|$($result.rate_hz)"
    $metadataByKey[$key] = $result
}

$details = [System.Collections.Generic.List[object]]::new()
$turnTelemetry = [System.Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem -LiteralPath $TraceDirectory -Filter '*.csv' -File | Sort-Object Name) {
    if ($file.Name -notmatch '^(?<trajectory>.+)-(?<preset>Balanced|Fast|Aggressive)-(?<rate>\d+)hz\.csv$') { continue }
    $key = "$($Matches.trajectory)|$($Matches.preset)|$($Matches.rate)"
    if (-not $metadataByKey.ContainsKey($key)) { throw "No matrix metadata for $($file.Name)" }
    $meta = $metadataByKey[$key]
    $brakeStart = BrakeStartMs $meta.speed $meta.brake
    $turn = Number $meta.physical_turn_ms
    $first = @{}; $preBrake = @{}; $tStop = @{}
    foreach ($candidate in $candidateNames) { $first[$candidate] = $null; $preBrake[$candidate] = 0; $tStop[$candidate] = $null }
    $previousSource = $null
    $previousMagnitude = $null
    $previousSourceVelocity = $null
    $previousOne = $false
    $history = [System.Collections.Generic.List[object]]::new()
    foreach ($row in Import-Csv -LiteralPath $file.FullName) {
        if (-not (Flag $row.source_sample_updated)) { continue }
        $time = Number $row.time_ms
        $position = Number $row.physical
        if ($null -eq $previousSource) { $previousSource = [pscustomobject]@{ time = $time; position = $position }; continue }
        $dt = ($time - $previousSource.time) / 1000.0
        if ($dt -le 0.0) { continue }
        $sourceVelocity = ($position - $previousSource.position) / $dt
        $magnitude = [Math]::Abs($sourceVelocity)
        $coherent = (Number $row.motion_coherence) -ge $coherenceFloor
        $one = $false
        if ($null -ne $previousMagnitude) {
            $one = $coherent -and $previousMagnitude -ge $velocityFloor -and $magnitude -le ($previousMagnitude * $oneUpdateDecayRatio)
        }
        $two = $one -and $previousOne
        Add-Ring $history ([pscustomobject]@{ time = $time; magnitude = $magnitude })
        $four = $false
        if ($history.Count -eq 4) {
            $four = $coherent -and $history[0].magnitude -ge $velocityFloor -and $magnitude -le ($history[0].magnitude * $fourUpdateDecayRatio)
        }
        $abg = $coherent -and [Math]::Abs((Number $row.estimator_velocity)) -ge $velocityFloor `
            -and ((Number $row.estimator_velocity) * (Number $row.estimator_acceleration) -lt 0.0)
        $signals = [ordered]@{ abg_opposing = $abg; source_decay_1 = $one; source_decay_2 = $two; source_decay_4 = $four }
        $sourceDeltaVelocity = if ($null -ne $previousSourceVelocity) { $sourceVelocity - $previousSourceVelocity } else { $null }
        $shortTrend = $null
        $dAbsVelocityDt = $null
        $tStopVelocityMs = $null
        if ($history.Count -eq 4) {
            $shortTrend = $magnitude - $history[0].magnitude
            $trendDt = ($time - $history[0].time) / 1000.0
            if ($trendDt -gt 0.0) {
                $dAbsVelocityDt = $shortTrend / $trendDt
                if ($dAbsVelocityDt -lt -0.000001) { $tStopVelocityMs = $magnitude / -$dAbsVelocityDt * 1000.0 }
            }
        }
        if ($time -ge ($brakeStart - 20.0) -and $time -le ($turn + 100.0)) {
            $turnTelemetry.Add([pscustomobject]@{
                trajectory = $meta.trajectory
                preset = $meta.preset
                rate_hz = [int]$meta.rate_hz
                brake_start_ms = $brakeStart
                physical_turn_ms = $turn
                time_ms = $time
                physical = $position
                ground_truth_velocity = Number $row.physical_velocity
                ground_truth_acceleration = Number $row.physical_acceleration
                accepted_source_velocity = $sourceVelocity
                accepted_source_delta_velocity = $sourceDeltaVelocity
                source_abs_velocity_trend_4_updates = $shortTrend
                d_abs_velocity_dt = $dAbsVelocityDt
                time_to_stop_velocity_ms = $tStopVelocityMs
                estimator_velocity = Number $row.estimator_velocity
                estimator_acceleration = Number $row.estimator_acceleration
                motion_coherence = Number $row.motion_coherence
                confidence = Number $row.confidence
                source_cadence_hz = Number $row.source_cadence_hz
                source_hold = Flag $row.source_hold
                predictor_state = $row.predictor_state
                reversal = Flag $row.reversal
                active_horizon_ms = Number $row.active_horizon_ms
                lead = Number $row.lead
                source_decay_1 = [int]$one
                source_decay_2 = [int]$two
                source_decay_4 = [int]$four
                abg_opposing = [int]$abg
            })
        }
        foreach ($candidate in $candidateNames) {
            if (-not $signals[$candidate]) { continue }
            if ($time -ge $warmupMs -and $time -lt $brakeStart) { ++$preBrake[$candidate] }
            if ($null -eq $first[$candidate] -and $time -ge $brakeStart -and $time -le $turn) {
                $first[$candidate] = $time
                if ($candidate -eq 'source_decay_4' -and $history.Count -eq 4) {
                    $dMagnitudeDt = ($magnitude - $history[0].magnitude) / (($time - $history[0].time) / 1000.0)
                    if ($dMagnitudeDt -lt -0.000001) { $tStop[$candidate] = $magnitude / -$dMagnitudeDt * 1000.0 }
                } elseif ($null -ne $previousMagnitude) {
                    $dMagnitudeDt = ($magnitude - $previousMagnitude) / $dt
                    if ($dMagnitudeDt -lt -0.000001) { $tStop[$candidate] = $magnitude / -$dMagnitudeDt * 1000.0 }
                }
            }
        }
        $previousSource = [pscustomobject]@{ time = $time; position = $position }
        $previousMagnitude = $magnitude
        $previousSourceVelocity = $sourceVelocity
        $previousOne = $one
    }
    foreach ($candidate in $candidateNames) {
        $detected = $null -ne $first[$candidate]
        $details.Add([pscustomobject]@{
            trajectory = $meta.trajectory
            speed = $meta.speed
            amplitude = $meta.amplitude
            topology = $meta.topology
            symmetry = $meta.symmetry
            brake = $meta.brake
            preset = $meta.preset
            rate_hz = [int]$meta.rate_hz
            candidate = $candidate
            brake_start_ms = $brakeStart
            physical_turn_ms = $turn
            detected = [int]$detected
            first_signal_ms = if ($detected) { $first[$candidate] } else { $null }
            detection_latency_ms = if ($detected) { $first[$candidate] - $brakeStart } else { $null }
            warning_before_zero_ms = if ($detected) { $turn - $first[$candidate] } else { $null }
            pre_brake_signal_samples = $preBrake[$candidate]
            time_to_stop_velocity_ms_at_signal = $tStop[$candidate]
        })
    }
}
$details | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'candidate_details.csv')
$turnTelemetry | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'turn_braking_telemetry.csv')

function Summarize-Details([System.Collections.IEnumerable]$items, [string]$group, [string]$value) {
    $records = @($items | ForEach-Object { $_ })
    foreach ($candidate in $candidateNames) {
        $rows = @($records | Where-Object candidate -eq $candidate)
        $detected = @($rows | Where-Object detected -eq 1)
        [pscustomobject]@{
            group = $group
            group_value = $value
            candidate = $candidate
            cases = $rows.Count
            detected = $detected.Count
            missed = $rows.Count - $detected.Count
            detection_rate_percent = if ($rows.Count) { 100.0 * $detected.Count / $rows.Count } else { 0.0 }
            median_latency_ms = Median @($detected.detection_latency_ms | ForEach-Object { Number $_ })
            p95_latency_ms = Percentile @($detected.detection_latency_ms | ForEach-Object { Number $_ }) 0.95
            median_warning_ms = Median @($detected.warning_before_zero_ms | ForEach-Object { Number $_ })
            min_warning_ms = if ($detected.Count) { (@($detected.warning_before_zero_ms | ForEach-Object { Number $_ }) | Measure-Object -Minimum).Minimum } else { $null }
            pre_brake_signal_samples = (@($rows.pre_brake_signal_samples | ForEach-Object { [int]$_ }) | Measure-Object -Sum).Sum
            median_t_stop_ms = Median @($detected.time_to_stop_velocity_ms_at_signal | Where-Object { $null -ne $_ } | ForEach-Object { Number $_ })
        }
    }
}

$summary = [System.Collections.Generic.List[object]]::new()
foreach ($row in (Summarize-Details -items $details -group 'all' -value 'all')) { $summary.Add($row) }
foreach ($preset in $selectedPresets) {
    $presetItems = @($details | Where-Object preset -eq $preset)
    foreach ($row in (Summarize-Details -items $presetItems -group 'preset' -value $preset)) { $summary.Add($row) }
}
foreach ($rate in 30, 60, 125, 250) {
    $rateItems = @($details | Where-Object rate_hz -eq $rate)
    foreach ($row in (Summarize-Details -items $rateItems -group 'rate_hz' -value ([string]$rate))) { $summary.Add($row) }
}
$summary | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'candidate_summary.csv')

$controls = [System.Collections.Generic.List[object]]::new()
foreach ($directory in $ControlDirectories) {
    $scenario = Split-Path -Leaf $directory
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $directory 'traces') -Filter '*.csv' -File) {
        if ($file.Name -notmatch 'Preset_(?<preset>Balanced|Fast|Aggressive)\.csv$') { continue }
        $previousSource = $null; $previousMagnitude = $null; $previousOne = $false
        $history = [System.Collections.Generic.List[object]]::new()
        $last = New-CandidateState
        $events = New-CandidateState
        foreach ($row in Import-Csv -LiteralPath $file.FullName) {
            if (-not (Flag $row.source_sample_updated)) { continue }
            # The mapper may observe an accepted source sample on a later tick;
            # derive delta velocity using the source timestamps, not tick spacing.
            $time = 1000.0 * (Number $row.time_seconds)
            $sourceTime = 1000.0 * (Number $row.source_sample_time_seconds)
            $position = Number $row.observed_device
            if ($null -eq $previousSource) { $previousSource = [pscustomobject]@{ time = $sourceTime; position = $position }; continue }
            $dt = ($sourceTime - $previousSource.time) / 1000.0
            if ($dt -le 0.0) { continue }
            $sourceVelocity = ($position - $previousSource.position) / $dt
            $magnitude = [Math]::Abs($sourceVelocity)
            $coherent = (Number $row.motion_coherence) -ge $coherenceFloor
            $one = $null -ne $previousMagnitude -and $coherent -and $previousMagnitude -ge $velocityFloor -and $magnitude -le ($previousMagnitude * $oneUpdateDecayRatio)
            $two = $one -and $previousOne
            Add-Ring $history ([pscustomobject]@{ time = $time; magnitude = $magnitude })
            $four = $history.Count -eq 4 -and $coherent -and $history[0].magnitude -ge $velocityFloor -and $magnitude -le ($history[0].magnitude * $fourUpdateDecayRatio)
            $abg = $coherent -and [Math]::Abs((Number $row.predictor_velocity)) -ge $velocityFloor -and ((Number $row.predictor_velocity) * (Number $row.predictor_acceleration) -lt 0.0)
            $current = [ordered]@{ abg_opposing = $abg; source_decay_1 = $one; source_decay_2 = $two; source_decay_4 = $four }
            foreach ($candidate in $candidateNames) {
                if ($current[$candidate] -and -not $last[$candidate]) { $events[$candidate] = [int]$events[$candidate] + 1 }
                $last[$candidate] = $current[$candidate]
            }
            $previousSource = [pscustomobject]@{ time = $sourceTime; position = $position }
            $previousMagnitude = $magnitude
            $previousOne = $one
        }
        foreach ($candidate in $candidateNames) {
            $controls.Add([pscustomobject]@{ control = $scenario; preset = $Matches.preset; candidate = $candidate; activation_events = $events[$candidate] })
        }
    }
}
$controls | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'control_activation_events.csv')
$controls | Group-Object control, candidate | ForEach-Object {
    $parts = $_.Name -split ', '
    [pscustomobject]@{ control = $parts[0]; candidate = $parts[1]; presets = $_.Count; total_activation_events = ($_.Group.activation_events | Measure-Object -Sum).Sum }
} | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'control_summary.csv')

Write-Output "Wrote $($details.Count) turning-case rows and $($controls.Count) control rows to $OutputDirectory"
