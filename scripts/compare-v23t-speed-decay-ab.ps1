param(
    [string]$BeforeCsv = 'artifacts/adaptive_response_validation/v2.3.T-braking-signal-full-20260901/turning_point_results.csv',
    [string]$AfterCsv = 'artifacts/adaptive_response_validation/v2.3.T-speed-decay-ab-after-continuity-20260901/turning_point_results.csv',
    [string]$OutputDirectory = 'artifacts/adaptive_response_validation/v2.3.T-speed-decay-ab-after-continuity-20260901/ab_comparison'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$identityColumns = @('trajectory', 'speed', 'amplitude', 'topology', 'symmetry', 'brake', 'preset', 'rate_hz')
$beforeRows = @(Import-Csv -LiteralPath $BeforeCsv)
$afterRows = @(Import-Csv -LiteralPath $AfterCsv)
$beforeByKey = @{}
foreach ($row in $beforeRows) {
    $key = ($identityColumns | ForEach-Object { [string]$row.$_ }) -join '|'
    if ($beforeByKey.ContainsKey($key)) { throw "Duplicate baseline key: $key" }
    $beforeByKey[$key] = $row
}
if ($beforeRows.Count -ne $afterRows.Count) { throw "Run counts differ: before=$($beforeRows.Count), after=$($afterRows.Count)" }

function Number([object]$value) { return [double]::Parse([string]$value, [Globalization.CultureInfo]::InvariantCulture) }
function Mean([double[]]$values) { if ($values.Count -eq 0) { return 0.0 }; return ($values | Measure-Object -Average).Average }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$commonMetrics = @($beforeRows[0].PSObject.Properties.Name | Where-Object { $identityColumns -notcontains $_ })
$afterOnlyMetrics = @($afterRows[0].PSObject.Properties.Name | Where-Object { $commonMetrics -notcontains $_ -and $identityColumns -notcontains $_ })
$matched = [System.Collections.Generic.List[object]]::new()
foreach ($after in $afterRows) {
    $key = ($identityColumns | ForEach-Object { [string]$after.$_ }) -join '|'
    if (-not $beforeByKey.ContainsKey($key)) { throw "No baseline match: $key" }
    $before = $beforeByKey[$key]
    $record = [ordered]@{}
    foreach ($column in $identityColumns) { $record[$column] = $after.$column }
    foreach ($metric in $commonMetrics) {
        $beforeValue = Number $before.$metric
        $afterValue = Number $after.$metric
        $record["before_$metric"] = $beforeValue
        $record["after_$metric"] = $afterValue
        $record["delta_$metric"] = $afterValue - $beforeValue
    }
    foreach ($metric in $afterOnlyMetrics) { $record["after_$metric"] = Number $after.$metric }
    $matched.Add([pscustomobject]$record)
}
if ($matched.Count -ne 288) { throw "Expected 288 matched runs, got $($matched.Count)" }
$matched | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'matched_before_after.csv')

$summaryMetrics = @('peak_error', 'lead_at_physical_velocity_zero', 'future_position_mae', 'future_position_p95',
    'future_position_max', 'turn_window_future_mae', 'shape_time_shift_fidelity',
    'max_predictor_only_artificial_step', 'opposite_direction_reacquisition_latency_ms',
    'useful_pre_braking_lead_ms', 'peak_lead_before_braking')
$summary = [System.Collections.Generic.List[object]]::new()
foreach ($dimension in @('preset', 'rate_hz', 'speed')) {
    foreach ($group in ($matched | Group-Object $dimension)) {
        $record = [ordered]@{ dimension = $dimension; value = $group.Name; cases = $group.Count }
        foreach ($metric in $summaryMetrics) {
            if ($metric -in @('peak_error', 'lead_at_physical_velocity_zero')) {
                $record["before_abs_$metric"] = Mean @($group.Group | ForEach-Object { [Math]::Abs([double]$_."before_$metric") })
                $record["after_abs_$metric"] = Mean @($group.Group | ForEach-Object { [Math]::Abs([double]$_."after_$metric") })
            } elseif ($metric -eq 'useful_pre_braking_lead_ms' -or $metric -eq 'peak_lead_before_braking') {
                $record["after_$metric"] = Mean @($group.Group | ForEach-Object { [double]$_."after_$metric" })
            } else {
                $record["before_$metric"] = Mean @($group.Group | ForEach-Object { [double]$_."before_$metric" })
                $record["after_$metric"] = Mean @($group.Group | ForEach-Object { [double]$_."after_$metric" })
            }
        }
        $detected = @($group.Group | Where-Object { [double]$_.after_braking_detector_activation_ms -ge 0.0 })
        $record['after_detector_activations'] = $detected.Count
        $record['after_mean_detector_warning_ms'] = Mean @($detected | ForEach-Object { [double]$_.after_braking_warning_before_zero_ms })
        $record['after_max_reduction_change'] = ($group.Group | ForEach-Object { [double]$_.after_max_braking_reduction_change } | Measure-Object -Maximum).Maximum
        $record['after_max_reduction_derivative'] = ($group.Group | ForEach-Object { [double]$_.after_max_braking_reduction_derivative } | Measure-Object -Maximum).Maximum
        $summary.Add([pscustomobject]$record)
    }
}
$summary | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'aggregate_before_after.csv')

$limits = $matched | Where-Object { $_.rate_hz -eq '30' -and $_.speed -eq 'very-fast' } | ForEach-Object {
    [pscustomobject]@{
        trajectory = $_.trajectory
        preset = $_.preset
        physical_turn_ms = $_.after_physical_turn_ms
        detector_activation_ms = $_.after_braking_detector_activation_ms
        detector_warning_ms = $_.after_braking_warning_before_zero_ms
        observation_limited = ([double]$_.after_braking_detector_activation_ms -lt 0.0)
        before_peak_error = $_.before_peak_error
        after_peak_error = $_.after_peak_error
        before_lead_at_zero = $_.before_lead_at_physical_velocity_zero
        after_lead_at_zero = $_.after_lead_at_physical_velocity_zero
    }
}
$limits | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'rate_30_very_fast_observation_limit.csv')

Write-Output "Wrote 288 matched rows, $($summary.Count) aggregate rows, and $(@($limits).Count) 30 Hz very-fast rows to $OutputDirectory"
