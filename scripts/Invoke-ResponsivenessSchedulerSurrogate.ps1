[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Idle', 'HeavyCpu', 'Combined')]
    [string]$Scenario,

    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$WorkloadExecutable,

    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$MappingBenchmarkExecutable,

    [ValidateRange(30, 120)]
    [int]$DurationSeconds = 60,

    [ValidateRange(32, 512)]
    [int]$DiskMiB = 128,

    [ValidateRange(1, 64)]
    [int]$MaximumCpuWorkers = 16,

    [ValidateSet('current', 'gui', 'render', 'gui-render', 'process-above-normal')]
    [string]$SchedulingPolicy = 'current',

    [Parameter(Mandatory)]
    [string]$EvidenceDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$evidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidenceDirectory -Force | Out-Null
$mappingLog = Join-Path $evidenceDirectory ("{0}-mapping-hot-path.log" -f $Scenario.ToLowerInvariant())
$summaryPath = Join-Path $evidenceDirectory ("{0}-scheduler-surrogate.json" -f $Scenario.ToLowerInvariant())
$deadlineUtc = [DateTime]::UtcNow.AddSeconds($DurationSeconds)

# This is intentionally a CPU/scheduler surrogate only. The benchmark has no
# DirectInput or vJoy calls, so it cannot contend for an owner device or write
# a second stream to vJoy.
$mappingJob = Start-Job -ScriptBlock {
    param([string]$Benchmark, [string]$Log, [DateTime]$Deadline)
    $runs = 0
    while ([DateTime]::UtcNow -lt $Deadline) {
        & $Benchmark 2>&1 | Out-File -LiteralPath $Log -Encoding utf8 -Append
        if ($LASTEXITCODE -ne 0) { throw "mapping_hot_path_benchmark exited with code $LASTEXITCODE" }
        ++$runs
        "scheduler-surrogate-benchmark-complete=$runs" | Out-File -LiteralPath $Log -Encoding utf8 -Append
    }
    $runs
} -ArgumentList $MappingBenchmarkExecutable, $mappingLog, $deadlineUtc

$mappingRuns = 0
try {
    & (Join-Path $PSScriptRoot 'Invoke-ResponsivenessCharacterization.ps1') `
        -Scenario $Scenario `
        -WorkloadExecutable $WorkloadExecutable `
        -DurationSeconds $DurationSeconds `
        -DiskMiB $DiskMiB `
        -MaximumCpuWorkers $MaximumCpuWorkers `
        -SchedulingPolicy $SchedulingPolicy `
        -EvidenceDirectory $evidenceDirectory
} finally {
    if ($mappingJob) {
        if ((Get-Job -Id $mappingJob.Id -ErrorAction SilentlyContinue).State -eq 'Running') {
            Stop-Job -Job $mappingJob -ErrorAction SilentlyContinue
        }
        Receive-Job -Job $mappingJob -ErrorAction SilentlyContinue | Out-Null
        Remove-Job -Job $mappingJob -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $mappingLog) {
        $mappingRuns = @(Select-String -LiteralPath $mappingLog -SimpleMatch 'scheduler-surrogate-benchmark-complete=').Count
    }
    [ordered]@{
        schemaVersion = 1
        scenario = $Scenario
        mode = 'scheduler-contention-surrogate'
        qualification = 'native UI process plus synthetic mapping benchmark; not physical mapping proof'
        requestedSchedulingPolicy = $SchedulingPolicy
        mappingBenchmark = (Resolve-Path -LiteralPath $MappingBenchmarkExecutable).Path
        mappingBenchmarkRuns = $mappingRuns
        mappingBenchmarkLog = $mappingLog
        nativeProbe = Join-Path $evidenceDirectory ("{0}-probe.json" -f $Scenario.ToLowerInvariant())
        loadEvidence = Join-Path $evidenceDirectory ("{0}-load.json" -f $Scenario.ToLowerInvariant())
        cleanup = 'benchmark job stopped and removed'
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
}
