param(
    [string]$BaselineExe = 'build-v2.3.T-local/Release/mapping_hot_path_benchmark.exe',
    [string]$OptimizedExe = 'build-v2.3.T-speed-decay-profile3/Release/mapping_hot_path_benchmark.exe',
    [string]$OutputCsv = 'artifacts/adaptive_response_validation/v2.3.T-speed-decay-profile-20260901/alternating_benchmark_pairs.csv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Number([string]$value) { return [double]::Parse($value, [Globalization.CultureInfo]::InvariantCulture) }
function Median([double[]]$values) {
    $ordered = @($values | Sort-Object)
    return if ($ordered.Count % 2) { $ordered[[int]($ordered.Count / 2)] }
           else { ($ordered[$ordered.Count / 2 - 1] + $ordered[$ordered.Count / 2]) / 2.0 }
}
function Run-Case([string]$label, [string]$path, [int]$pair) {
    $lines = @(& $path)
    if ($LASTEXITCODE -ne 0) { throw "$label pair $pair exited $LASTEXITCODE" }
    $results = [System.Collections.Generic.List[object]]::new()
    foreach ($line in $lines) {
        if ($line -notmatch '^idle (Adaptive Auto|Adaptive All 8 Axes) reports/s=([0-9.]+) typical_us=([0-9.]+) p95_us=([0-9.]+) p99_us=([0-9.]+).*hot_path_allocations=([0-9]+)') { continue }
        $results.Add([pscustomobject]@{
            pair = $pair; variant = $label; scope = $Matches[1]; reports_per_second = Number $Matches[2]
            median_us = Number $Matches[3]; p95_us = Number $Matches[4]; p99_us = Number $Matches[5]
            hot_path_allocations = [int]$Matches[6]
        })
    }
    if ($results.Count -ne 2) { throw "$label pair $pair did not yield both target scopes" }
    return $results
}

$qtBin = 'C:\Users\kkids\Documents\HOTAS_BF6\.toolchain\Qt\6.8.3\msvc2022_64\bin'
$env:Path = "$qtBin;$env:Path"
$results = [System.Collections.Generic.List[object]]::new()
for ($pair = 1; $pair -le 3; ++$pair) {
    foreach ($row in Run-Case 'baseline' $BaselineExe $pair) { $results.Add($row) }
    foreach ($row in Run-Case 'optimized' $OptimizedExe $pair) { $results.Add($row) }
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputCsv) | Out-Null
$results | Export-Csv -NoTypeInformation -LiteralPath $OutputCsv

$summary = foreach ($scope in 'Adaptive Auto', 'Adaptive All 8 Axes') {
    $base = @($results | Where-Object { $_.scope -eq $scope -and $_.variant -eq 'baseline' })
    $optimized = @($results | Where-Object { $_.scope -eq $scope -and $_.variant -eq 'optimized' })
    $baseRps = Median @($base.reports_per_second | ForEach-Object { [double]$_ })
    $optimizedRps = Median @($optimized.reports_per_second | ForEach-Object { [double]$_ })
    [pscustomobject]@{
        scope = $scope
        baseline_median_rps = $baseRps
        optimized_median_rps = $optimizedRps
        throughput_delta_percent = 100.0 * ($optimizedRps - $baseRps) / $baseRps
        baseline_median_us = Median @($base.median_us | ForEach-Object { [double]$_ })
        optimized_median_us = Median @($optimized.median_us | ForEach-Object { [double]$_ })
        baseline_p95_us = Median @($base.p95_us | ForEach-Object { [double]$_ })
        optimized_p95_us = Median @($optimized.p95_us | ForEach-Object { [double]$_ })
        baseline_p99_us = Median @($base.p99_us | ForEach-Object { [double]$_ })
        optimized_p99_us = Median @($optimized.p99_us | ForEach-Object { [double]$_ })
        allocations_all_zero = (@($base + $optimized | Where-Object hot_path_allocations -ne 0).Count -eq 0)
    }
}
$summary | Format-Table -AutoSize
