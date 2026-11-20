[CmdletBinding()]
param(
    [string]$Profile0Exe = 'build-v2.3.T-speed-decay-profile0/Release/mapping_hot_path_benchmark.exe',
    [string]$Profile1Exe = 'build-v2.3.T-speed-decay-profile1/Release/mapping_hot_path_benchmark.exe',
    [string]$Profile2Exe = 'build-v2.3.T-speed-decay-profile2/Release/mapping_hot_path_benchmark.exe',
    [string]$Profile3Exe = 'build-v2.3.T-speed-decay-profile3/Release/mapping_hot_path_benchmark.exe',
    [string]$OutputDirectory = 'artifacts/adaptive_response_validation/v2.3.T-speed-decay-final-optimization-20260902-101300',
    [ValidateRange(2, 8)]
    [int]$Rounds = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture

function Number([string]$value) { return [double]::Parse($value, $culture) }
function Median([double[]]$values) {
    $ordered = @($values | Sort-Object)
    if ($ordered.Count -eq 0) { return [double]::NaN }
    if ($ordered.Count % 2) { return $ordered[[int]($ordered.Count / 2)] }
    return ($ordered[$ordered.Count / 2 - 1] + $ordered[$ordered.Count / 2]) / 2.0
}
function Run-Profile([int]$profile, [string]$path, [int]$round) {
    $lines = @(& $path)
    if ($LASTEXITCODE -ne 0) { throw "Profile $profile round $round exited $LASTEXITCODE" }
    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($line in $lines) {
        if ($line -notmatch '^idle (Adaptive Auto|Adaptive All 8 Axes) reports/s=([0-9.]+) typical_us=([0-9.]+) p95_us=([0-9.]+) p99_us=([0-9.]+).*hot_path_allocations=([0-9]+)') { continue }
        $rows.Add([pscustomobject]@{
            profile = $profile; round = $round; scope = $Matches[1]; reports_per_second = Number $Matches[2]
            median_us = Number $Matches[3]; p95_us = Number $Matches[4]; p99_us = Number $Matches[5]
            hot_path_allocations = [int]$Matches[6]
        })
    }
    if ($rows.Count -ne 2) { throw "Profile $profile round $round did not yield both target scopes" }
    return $rows
}

$profiles = @(
    [pscustomobject]@{ level = 0; exe = $Profile0Exe },
    [pscustomobject]@{ level = 1; exe = $Profile1Exe },
    [pscustomobject]@{ level = 2; exe = $Profile2Exe },
    [pscustomobject]@{ level = 3; exe = $Profile3Exe }
)
foreach ($profile in $profiles) {
    if (-not (Test-Path -LiteralPath $profile.exe)) { throw "Profile $($profile.level) benchmark is missing: $($profile.exe)" }
}
$qtBin = 'C:\Users\kkids\Documents\HOTAS_BF6\.toolchain\Qt\6.8.3\msvc2022_64\bin'
if (-not (Test-Path -LiteralPath $qtBin)) { throw "Qt runtime is missing: $qtBin" }
$env:Path = "$qtBin;$env:Path"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
for ($round = 1; $round -le $Rounds; ++$round) {
    $ordered = if ($round % 2) { $profiles } else { @($profiles | Sort-Object level -Descending) }
    foreach ($profile in $ordered) {
        foreach ($row in Run-Profile $profile.level $profile.exe $round) { $results.Add($row) }
    }
}
$rawCsv = Join-Path $OutputDirectory 'stage_profile_raw.csv'
$results | Export-Csv -NoTypeInformation -LiteralPath $rawCsv

$summary = [System.Collections.Generic.List[object]]::new()
foreach ($scope in @('Adaptive Auto', 'Adaptive All 8 Axes')) {
    $baseline = @($results | Where-Object { $_.profile -eq 0 -and $_.scope -eq $scope })
    $baselineRps = Median ([double[]]@($baseline.reports_per_second))
    foreach ($profile in 0..3) {
        $rows = @($results | Where-Object { $_.profile -eq $profile -and $_.scope -eq $scope })
        $p99 = Median ([double[]]@($rows.p99_us))
        $summary.Add([pscustomobject]@{
            profile = $profile; scope = $scope; runs = $rows.Count
            median_reports_per_second = Median ([double[]]@($rows.reports_per_second))
            throughput_delta_vs_profile0_percent = 100.0 * (Median ([double[]]@($rows.reports_per_second)) - $baselineRps) / $baselineRps
            median_us = Median ([double[]]@($rows.median_us)); p95_us = Median ([double[]]@($rows.p95_us)); p99_us = $p99
            p99_250hz_interval_percent = $p99 / 4000.0 * 100.0
            hot_path_allocations_all_zero = (@($rows | Where-Object { $_.hot_path_allocations -ne 0 }).Count -eq 0)
        })
    }
}
$summary | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $OutputDirectory 'stage_profile_summary.csv')
$summary | Format-Table -AutoSize
Write-Output "Stage profile evidence written to $OutputDirectory"
