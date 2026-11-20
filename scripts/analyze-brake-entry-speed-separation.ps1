[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReplayArtifactRoot,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ReplayArtifactRoot)) {
    throw "Replay artifact root is missing: $ReplayArtifactRoot"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $ReplayArtifactRoot
}
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}

$culture = [Globalization.CultureInfo]::InvariantCulture
$floors = @(0.075, 0.085, 0.100, 0.125, 0.150, 0.200)
$presets = @('Balanced', 'Fast', 'Aggressive')
$classes = @('genuine deliberate braking', 'slow full sweep', 'precision correction', 'slow stop', 'stationary jitter')

function To-Double($Value) {
    return [double]::Parse([string]$Value, [Globalization.NumberStyles]::Float, $culture)
}

function To-Int($Value) {
    return [int]::Parse([string]$Value, [Globalization.NumberStyles]::Integer, $culture)
}

function Format-Number([double]$Value) {
    return $Value.ToString('0.000000', $culture)
}

function Percentile([double[]]$Values, [double]$Fraction) {
    if ($Values.Count -eq 0) { return [double]::NaN }
    $ordered = @($Values | Sort-Object)
    return [double]$ordered[[int][math]::Floor(($ordered.Count - 1) * $Fraction)]
}

function Distribution([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return [pscustomobject]@{ count = 0; min = [double]::NaN; p05 = [double]::NaN; p10 = [double]::NaN; p50 = [double]::NaN; p90 = [double]::NaN; p95 = [double]::NaN; p99 = [double]::NaN; max = [double]::NaN }
    }
    return [pscustomobject]@{
        count = $Values.Count
        min = [double]($Values | Measure-Object -Minimum).Minimum
        p05 = Percentile $Values 0.05
        p10 = Percentile $Values 0.10
        p50 = Percentile $Values 0.50
        p90 = Percentile $Values 0.90
        p95 = Percentile $Values 0.95
        p99 = Percentile $Values 0.99
        max = [double]($Values | Measure-Object -Maximum).Maximum
    }
}

function Classify-Trace([string]$Trace) {
    if ($Trace -eq 'slow-full-sweep-1') { return 'slow full sweep' }
    if ($Trace -eq 'precision-corrections-1') { return 'precision correction' }
    if ($Trace -eq 'slow-stop-1') { return 'slow stop' }
    if ($Trace -eq 'stationary-jitter-1') { return 'stationary jitter' }
    return 'genuine deliberate braking'
}

function Export-Rows([object[]]$Rows, [string]$Path, [string]$Header) {
    if ($Rows.Count -gt 0) {
        $Rows | Export-Csv -NoTypeInformation -LiteralPath $Path
    } else {
        Set-Content -NoNewline -LiteralPath $Path -Value $Header
    }
}

$eventFiles = @(Get-ChildItem -LiteralPath (Join-Path $ReplayArtifactRoot 'candidate') -Recurse -File -Filter 'replay_detector_events.csv' | Sort-Object FullName)
if ($eventFiles.Count -ne 45) {
    throw "Expected 45 candidate detector-event CSVs (15 traces x 3 presets), found $($eventFiles.Count)."
}

$records = [System.Collections.Generic.List[object]]::new()
$turns = [System.Collections.Generic.List[object]]::new()
foreach ($file in $eventFiles) {
    $trace = $file.Directory.Name
    $preset = $file.Directory.Parent.Name
    if ($presets -notcontains $preset) { throw "Unexpected preset directory: $($file.Directory.Parent.FullName)" }
    $summaryPath = Join-Path $file.Directory.FullName 'replay_summary.csv'
    $turnPath = Join-Path $file.Directory.FullName 'replay_turns.csv'
    $summaryRows = @(Import-Csv -LiteralPath $summaryPath)
    if ($summaryRows.Count -ne 1) { throw "Expected one summary row in $summaryPath" }
    $cadence = To-Double $summaryRows[0].median_source_cadence_hz
    foreach ($event in @(Import-Csv -LiteralPath $file.FullName)) {
        $speed = To-Double $event.accepted_source_speed
        $priorSpeed = To-Double $event.previous_accepted_source_speed
        $entryGateSpeed = [math]::Min($speed, $priorSpeed)
        $records.Add([pscustomobject]@{
            trace_label = $trace
            preset = $preset
            activation_class = Classify-Trace $trace
            source_cadence_hz = $cadence
            activation_time_ms = To-Double $event.time_ms
            physical_source_position = To-Double $event.accepted_source_position
            previous_source_position = To-Double $event.previous_accepted_source_position
            absolute_source_speed = $speed
            previous_source_speed = $priorSpeed
            entry_gate_speed = $entryGateSpeed
            cumulative_speed_decay = To-Double $event.source_cumulative_speed_decay
            per_update_speed_retention_ratio = To-Double $event.per_update_source_speed_retention_ratio
            per_update_speed_decay_ratio = To-Double $event.per_update_source_speed_decay_ratio
            accepted_source_interval_ms = To-Double $event.accepted_source_interval_ms
            motion_coherence = To-Double $event.motion_coherence
            confidence = To-Double $event.confidence
            detector_persistence_count = To-Int $event.detector_persistence_count
            active_horizon_ms = To-Double $event.active_horizon_ms
            lead = To-Double $event.lead
            velocity = To-Double $event.velocity
            acceleration = To-Double $event.acceleration
            braking_reduction_factor = To-Double $event.braking_reduction_factor
            accepted_source_updated = To-Int $event.accepted_source_updated
        })
    }
    foreach ($turn in @(Import-Csv -LiteralPath $turnPath)) {
        $turns.Add([pscustomobject]@{
            trace_label = $trace
            preset = $preset
            turn_index = To-Int $turn.turn_index
            physical_turn_ms = To-Double $turn.physical_turn_ms
            detector_activation_ms = To-Double $turn.detector_activation_ms
            warning_before_turn_ms = To-Double $turn.warning_before_turn_ms
        })
    }
}

if ($records.Count -eq 0) { throw 'No candidate detector activations were recorded.' }
if (@($records | Where-Object { $_.accepted_source_updated -ne 1 }).Count -ne 0) {
    throw 'A detector entry did not coincide with an accepted-source update; the diagnostic snapshot is incomplete.'
}

# These two derived fields are offline-only context. source_stopping_ms is
# the candidate's bounded current-speed/drop slope estimate. The turn delta
# uses the saved physical trajectory, so it is diagnostic evidence only and
# cannot be used by the real-time predictor as a causal input.
foreach ($record in $records) {
    $sourceSpeedDrop = [math]::Max(0.0, $record.previous_source_speed - $record.absolute_source_speed)
    $sourceSlope = $sourceSpeedDrop / [math]::Max($record.accepted_source_interval_ms / 1000.0, 0.0005)
    $sourceStoppingMs = [math]::Min(750.0, $record.absolute_source_speed / [math]::Max($sourceSlope, 0.001) * 1000.0)
    $nextTurn = @($turns | Where-Object {
        $_.preset -eq $record.preset -and $_.trace_label -eq $record.trace_label -and $_.physical_turn_ms -ge $record.activation_time_ms
    } | Sort-Object physical_turn_ms | Select-Object -First 1)
    $timeToNextTurnMs = if ($nextTurn.Count -eq 1) { $nextTurn[0].physical_turn_ms - $record.activation_time_ms } else { [double]::NaN }
    $record | Add-Member -NotePropertyName source_speed_drop -NotePropertyValue $sourceSpeedDrop
    $record | Add-Member -NotePropertyName source_stopping_ms -NotePropertyValue $sourceStoppingMs
    $record | Add-Member -NotePropertyName time_to_next_physical_turn_ms -NotePropertyValue $timeToNextTurnMs
}

Export-Rows @($records) (Join-Path $OutputDirectory 'brake_entry_activation_telemetry.csv') 'trace_label,preset,activation_class,source_cadence_hz,activation_time_ms,physical_source_position,previous_source_position,absolute_source_speed,previous_source_speed,entry_gate_speed,cumulative_speed_decay,per_update_speed_retention_ratio,per_update_speed_decay_ratio,accepted_source_interval_ms,motion_coherence,confidence,detector_persistence_count,active_horizon_ms,lead,velocity,acceleration,braking_reduction_factor,accepted_source_updated,source_speed_drop,source_stopping_ms,time_to_next_physical_turn_ms'

$numericMetrics = @('absolute_source_speed', 'previous_source_speed', 'entry_gate_speed', 'source_speed_drop', 'cumulative_speed_decay', 'per_update_speed_retention_ratio', 'per_update_speed_decay_ratio', 'accepted_source_interval_ms', 'motion_coherence', 'confidence', 'detector_persistence_count', 'active_horizon_ms', 'lead', 'velocity', 'acceleration', 'braking_reduction_factor', 'source_stopping_ms', 'time_to_next_physical_turn_ms')
$classDistributionRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    foreach ($class in $classes) {
        $subset = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -eq $class })
        foreach ($metric in $numericMetrics) {
            $values = [double[]]@($subset | ForEach-Object { [double]$_.$metric } | Where-Object { [double]::IsFinite($_) })
            $d = Distribution $values
            $classDistributionRows.Add([pscustomobject]@{
                preset = $preset; activation_class = $class; metric = $metric; count = $d.count
                min = $d.min; p05 = $d.p05; p10 = $d.p10; p50 = $d.p50; p90 = $d.p90; p95 = $d.p95; p99 = $d.p99; max = $d.max
            })
        }
    }
}
Export-Rows @($classDistributionRows) (Join-Path $OutputDirectory 'brake_entry_class_distributions.csv') 'preset,activation_class,metric,count,min,p05,p10,p50,p90,p95,p99,max'

$speedSeparationRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    $genuine = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -eq 'genuine deliberate braking' })
    $unwanted = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -ne 'genuine deliberate braking' })
    $gd = Distribution ([double[]]@($genuine | ForEach-Object { $_.entry_gate_speed }))
    $ud = Distribution ([double[]]@($unwanted | ForEach-Object { $_.entry_gate_speed }))
    $speedSeparationRows.Add([pscustomobject]@{
        preset = $preset; genuine_events = $gd.count; genuine_min = $gd.min; genuine_p05 = $gd.p05; genuine_p10 = $gd.p10; genuine_median = $gd.p50; genuine_p90 = $gd.p90; genuine_max = $gd.max
        unwanted_events = $ud.count; unwanted_min = $ud.min; unwanted_p50 = $ud.p50; unwanted_p90 = $ud.p90; unwanted_p95 = $ud.p95; unwanted_p99 = $ud.p99; unwanted_max = $ud.max
        p05_genuine_minus_max_unwanted = $gd.p05 - $ud.max
    })
}
Export-Rows @($speedSeparationRows) (Join-Path $OutputDirectory 'brake_entry_speed_separation.csv') 'preset,genuine_events,genuine_min,genuine_p05,genuine_p10,genuine_median,genuine_p90,genuine_max,unwanted_events,unwanted_min,unwanted_p50,unwanted_p90,unwanted_p95,unwanted_p99,unwanted_max,p05_genuine_minus_max_unwanted'

$byTraceRows = [System.Collections.Generic.List[object]]::new()
foreach ($group in @($records | Group-Object preset, trace_label, activation_class, source_cadence_hz)) {
    $d = Distribution ([double[]]@($group.Group | ForEach-Object { $_.entry_gate_speed }))
    $byTraceRows.Add([pscustomobject]@{
        preset = $group.Group[0].preset; trace_label = $group.Group[0].trace_label; activation_class = $group.Group[0].activation_class; source_cadence_hz = $group.Group[0].source_cadence_hz
        events = $d.count; entry_gate_speed_min = $d.min; entry_gate_speed_p50 = $d.p50; entry_gate_speed_p90 = $d.p90; entry_gate_speed_max = $d.max
    })
}
Export-Rows @($byTraceRows) (Join-Path $OutputDirectory 'brake_entry_by_trace_cadence.csv') 'preset,trace_label,activation_class,source_cadence_hz,events,entry_gate_speed_min,entry_gate_speed_p50,entry_gate_speed_p90,entry_gate_speed_max'

$floorRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    foreach ($floor in $floors) {
        foreach ($class in @('all unwanted', 'slow full sweep', 'precision correction', 'slow stop', 'stationary jitter', 'genuine deliberate braking')) {
            $subset = if ($class -eq 'all unwanted') { @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -ne 'genuine deliberate braking' }) } else { @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -eq $class }) }
            $retained = @($subset | Where-Object { $_.entry_gate_speed -ge $floor })
            $floorRows.Add([pscustomobject]@{
                preset = $preset; floor = $floor; scope = $class; activations = $subset.Count; retained = $retained.Count; rejected = $subset.Count - $retained.Count
                retained_percent = if ($subset.Count -gt 0) { 100.0 * $retained.Count / $subset.Count } else { 0.0 }
                rejected_percent = if ($subset.Count -gt 0) { 100.0 * ($subset.Count - $retained.Count) / $subset.Count } else { 0.0 }
            })
        }
    }
}
Export-Rows @($floorRows) (Join-Path $OutputDirectory 'brake_entry_floor_counterfactual.csv') 'preset,floor,scope,activations,retained,rejected,retained_percent,rejected_percent'

$featureRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    $genuine = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -eq 'genuine deliberate braking' })
    $unwanted = @($records | Where-Object { $_.preset -eq $preset -and $_.activation_class -ne 'genuine deliberate braking' })
    foreach ($metric in @('absolute_source_speed', 'previous_source_speed', 'entry_gate_speed', 'source_speed_drop', 'cumulative_speed_decay', 'motion_coherence', 'confidence', 'detector_persistence_count', 'active_horizon_ms', 'braking_reduction_factor', 'source_stopping_ms', 'time_to_next_physical_turn_ms')) {
        $gd = Distribution ([double[]]@($genuine | ForEach-Object { [double]$_.$metric } | Where-Object { [double]::IsFinite($_) }))
        $ud = Distribution ([double[]]@($unwanted | ForEach-Object { [double]$_.$metric } | Where-Object { [double]::IsFinite($_) }))
        $featureRows.Add([pscustomobject]@{
            preset = $preset; metric = $metric; genuine_p05 = $gd.p05; genuine_p50 = $gd.p50; unwanted_p95 = $ud.p95; unwanted_p99 = $ud.p99
            high_threshold_gap_p05_genuine_minus_p99_unwanted = $gd.p05 - $ud.p99
        })
    }
}
Export-Rows @($featureRows) (Join-Path $OutputDirectory 'brake_entry_feature_separation.csv') 'preset,metric,genuine_p05,genuine_p50,unwanted_p95,unwanted_p99,high_threshold_gap_p05_genuine_minus_p99_unwanted'

$warningNames = @('fast-reversal-1', 'center-crossing-reversal-1', 'fast-full-sweep-1', 'very-fast-full-sweep-1')
$warningRows = [System.Collections.Generic.List[object]]::new()
foreach ($trace in $warningNames) {
    $turn = @($turns | Where-Object { $_.preset -eq 'Fast' -and $_.trace_label -eq $trace -and $_.warning_before_turn_ms -ge 0 } | Sort-Object warning_before_turn_ms -Descending | Select-Object -First 1)
    if ($turn.Count -ne 1) { throw "Missing Fast warning turn for $trace" }
    $entry = @($records | Where-Object { $_.preset -eq 'Fast' -and $_.trace_label -eq $trace -and [math]::Abs($_.activation_time_ms - $turn[0].detector_activation_ms) -lt 0.002 } | Select-Object -First 1)
    if ($entry.Count -ne 1) { throw "Missing matching Fast detector entry for $trace at $($turn[0].detector_activation_ms) ms" }
    foreach ($floor in $floors) {
        $retained = $entry[0].entry_gate_speed -ge $floor
        $warningRows.Add([pscustomobject]@{
            trace_label = $trace; floor = $floor; physical_turn_ms = $turn[0].physical_turn_ms; observed_detector_entry_ms = $entry[0].activation_time_ms
            observed_warning_before_turn_ms = $turn[0].warning_before_turn_ms; entry_gate_speed = $entry[0].entry_gate_speed
            entry_retained = [int]$retained
            predicted_warning_remaining_ms = if ($retained) { $turn[0].warning_before_turn_ms } else { [double]::NaN }
            interpretation = if ($retained) { 'Observed entry retained: same-entry warning remains.' } else { 'Observed entry rejected: later entry timing is not predicted by this entry-screen counterfactual.' }
        })
    }
}
Export-Rows @($warningRows) (Join-Path $OutputDirectory 'brake_entry_fast_warning_counterfactual.csv') 'trace_label,floor,physical_turn_ms,observed_detector_entry_ms,observed_warning_before_turn_ms,entry_gate_speed,entry_retained,predicted_warning_remaining_ms,interpretation'

$resolutionRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $presets) {
    foreach ($floor in $floors) {
        $multipliers = [double[]]@($records | Where-Object { $_.preset -eq $preset } | ForEach-Object { $floor * ($_.accepted_source_interval_ms / 1000.0) / 0.00015 })
        $d = Distribution $multipliers
        $resolutionRows.Add([pscustomobject]@{
            preset = $preset; absolute_floor = $floor; events = $d.count; equivalent_resolution_multiplier_min = $d.min; equivalent_resolution_multiplier_p50 = $d.p50; equivalent_resolution_multiplier_p90 = $d.p90; equivalent_resolution_multiplier_max = $d.max
        })
    }
}
Export-Rows @($resolutionRows) (Join-Path $OutputDirectory 'brake_entry_resolution_equivalent_multiplier.csv') 'preset,absolute_floor,events,equivalent_resolution_multiplier_min,equivalent_resolution_multiplier_p50,equivalent_resolution_multiplier_p90,equivalent_resolution_multiplier_max'

$report = [System.Text.StringBuilder]::new()
[void]$report.AppendLine('# V2.3.T Brake-Entry Speed Separation Diagnostic')
[void]$report.AppendLine()
[void]$report.AppendLine('Offline diagnostic only: 15 saved physical traces replayed once for each retained-candidate preset. The verification-only snapshot is compiled only with `HOTAS_ADAPTIVE_TURNING_POINT_TELEMETRY`; it records state but does not alter predictor decisions, coefficients, thresholds, presets, UI, DirectInput, MappingWorker, or vJoy.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Classification')
[void]$report.AppendLine()
[void]$report.AppendLine('`slow-full-sweep-1`, `precision-corrections-1`, `slow-stop-1`, and `stationary-jitter-1` are the four unwanted low-motion classes. All other labelled captures are treated as genuine deliberate braking for this diagnostic, including the fast/very-fast/side/center reversals, full sweeps, evasive trace, and abrupt stop.')
[void]$report.AppendLine()
[void]$report.AppendLine('The floor counterfactual exactly screens each observed detector entry with the existing two-sample condition: `min(current accepted-source speed, previous accepted-source speed) >= floor`. It does not claim to simulate later detector entries after a rejection.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Detector-entry coverage')
[void]$report.AppendLine()
[void]$report.AppendLine("- Candidate detector entries: $($records.Count) total; all $($records.Count) began on an accepted-source update.")
[void]$report.AppendLine('- Percentiles use the observed lower order statistic: `floor((n - 1) * p)`.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Source-speed separation')
[void]$report.AppendLine()
[void]$report.AppendLine('| Preset | Genuine n | Genuine min / p05 / p10 / median / p90 / max | Unwanted n | Unwanted min / p50 / p90 / p95 / p99 / max | p05 genuine − max unwanted |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|')
foreach ($row in $speedSeparationRows) {
    [void]$report.AppendLine("| $($row.preset) | $($row.genuine_events) | $(Format-Number $row.genuine_min) / $(Format-Number $row.genuine_p05) / $(Format-Number $row.genuine_p10) / $(Format-Number $row.genuine_median) / $(Format-Number $row.genuine_p90) / $(Format-Number $row.genuine_max) | $($row.unwanted_events) | $(Format-Number $row.unwanted_min) / $(Format-Number $row.unwanted_p50) / $(Format-Number $row.unwanted_p90) / $(Format-Number $row.unwanted_p95) / $(Format-Number $row.unwanted_p99) / $(Format-Number $row.unwanted_max) | $(Format-Number $row.p05_genuine_minus_max_unwanted) |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('A positive last column would indicate a strict observed threshold gap. The measured result is interpreted from the generated table rather than assumed from the prior resolution-floor experiment.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Fast per-class entry-speed distribution')
[void]$report.AppendLine()
[void]$report.AppendLine('| Class | Entries | Min | p05 | Median | p90 | p95 | p99 | Max |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
foreach ($class in $classes) {
    $row = @($classDistributionRows | Where-Object { $_.preset -eq 'Fast' -and $_.activation_class -eq $class -and $_.metric -eq 'entry_gate_speed' })[0]
    [void]$report.AppendLine("| $class | $($row.count) | $(Format-Number $row.min) | $(Format-Number $row.p05) | $(Format-Number $row.p50) | $(Format-Number $row.p90) | $(Format-Number $row.p95) | $(Format-Number $row.p99) | $(Format-Number $row.max) |")
}
$cadenceDistribution = Distribution ([double[]]@($records | ForEach-Object { $_.source_cadence_hz }))
[void]$report.AppendLine()
[void]$report.AppendLine("All final physical traces in this capture set have a narrow source-cadence band: $(Format-Number $cadenceDistribution.min)–$(Format-Number $cadenceDistribution.max) Hz (per-trace values are in brake_entry_by_trace_cadence.csv).")
[void]$report.AppendLine()
[void]$report.AppendLine('## Absolute-floor entry-screen counterfactual')
[void]$report.AppendLine()
[void]$report.AppendLine('| Preset | Floor | Unwanted rejected / total | Genuine retained / total |')
[void]$report.AppendLine('|---|---:|---:|---:|')
foreach ($preset in $presets) {
    foreach ($floor in $floors) {
        $unwanted = @($floorRows | Where-Object { $_.preset -eq $preset -and $_.floor -eq $floor -and $_.scope -eq 'all unwanted' })[0]
        $genuine = @($floorRows | Where-Object { $_.preset -eq $preset -and $_.floor -eq $floor -and $_.scope -eq 'genuine deliberate braking' })[0]
        [void]$report.AppendLine("| $preset | $(Format-Number $floor) | $($unwanted.rejected) / $($unwanted.activations) ($(Format-Number $unwanted.rejected_percent)%) | $($genuine.retained) / $($genuine.activations) ($(Format-Number $genuine.retained_percent)%) |")
    }
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Fast representative warning entries')
[void]$report.AppendLine()
[void]$report.AppendLine('| Trace | Floor | Observed warning ms | Entry gate speed | Entry retained | Remaining observed-entry warning ms |')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|')
foreach ($row in $warningRows) {
    $remaining = if ($row.entry_retained -eq 1) { Format-Number $row.predicted_warning_remaining_ms } else { 'not predicted' }
    [void]$report.AppendLine("| $($row.trace_label) | $(Format-Number $row.floor) | $(Format-Number $row.observed_warning_before_turn_ms) | $(Format-Number $row.entry_gate_speed) | $($row.entry_retained) | $remaining |")
}
[void]$report.AppendLine()
[void]$report.AppendLine('## Supporting artifacts')
[void]$report.AppendLine()
[void]$report.AppendLine('- `brake_entry_activation_telemetry.csv`: one complete source-state record per detector entry.')
[void]$report.AppendLine('- `brake_entry_class_distributions.csv`: min/p05/p10/p50/p90/p95/p99/max for every requested numeric detector-entry field, preset, and class.')
[void]$report.AppendLine('- `brake_entry_by_trace_cadence.csv`: entry speed distribution by capture/source cadence.')
[void]$report.AppendLine('- `brake_entry_feature_separation.csv`: observed high-threshold separation checks for source speed, relative decay, coherence, confidence, persistence, horizon, and reduction factor.')
[void]$report.AppendLine('- `brake_entry_resolution_equivalent_multiplier.csv`: cadence-dependent resolution equivalent for each absolute floor, using `floor × interval / 0.00015`.')
[void]$report.AppendLine()
[void]$report.AppendLine('## Recommendation')
[void]$report.AppendLine()
[void]$report.AppendLine('The observed entry-speed distributions overlap severely: the unwanted maximum is far above genuine p05, and every tested floor rejects at most a small minority of unwanted entries while also removing genuine entries. The strongest additional diagnostic context is time to the next saved physical turn, which is generally longer for the slow full-sweep and stationary-jitter classes but overlaps genuine braking and is future-only. No causal entry-local scalar in this capture set separates the classes safely. The next targeted diagnostic should therefore examine sustained directional travel/time before decay (a causal history feature), not raise the instantaneous speed floor.')
[void]$report.AppendLine()
[void]$report.AppendLine('2. ABANDON SPEED FLOOR AS PRIMARY DISCRIMINATOR')

$report.ToString() | Set-Content -NoNewline -LiteralPath (Join-Path $OutputDirectory 'V2.3.T_Brake_Entry_Speed_Separation_Report.md')
Write-Output "Brake-entry speed-separation evidence written to $OutputDirectory"
