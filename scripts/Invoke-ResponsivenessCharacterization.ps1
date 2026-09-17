[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Idle', 'ModerateCpu', 'HeavyCpu', 'SevereCpu', 'NearSaturationCpu', 'Disk', 'Combined')]
    [string]$Scenario,

    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$WorkloadExecutable,

    [string[]]$WorkloadArguments = @(),

    [ValidateRange(8, 120)]
    [int]$DurationSeconds = 30,

    [ValidateRange(32, 512)]
    [int]$DiskMiB = 128,

    [ValidateRange(1, 64)]
    [int]$MaximumCpuWorkers = 8,

    [string]$EvidenceDirectory = (Join-Path $env:TEMP 'HOTAS-BF6-responsiveness-evidence')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-LoadSample {
    $processors = @(Get-CimInstance Win32_Processor)
    $cpuValues = @($processors | ForEach-Object { [double]$_.LoadPercentage })
    $disk = Get-CimInstance Win32_PerfFormattedData_PerfDisk_PhysicalDisk -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq '_Total' } | Select-Object -First 1
    [ordered]@{
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        cpuPercent = if ($cpuValues.Count) { [Math]::Round(($cpuValues | Measure-Object -Average).Average, 1) } else { $null }
        diskBytesPerSecond = if ($disk) { [int64]$disk.DiskBytesPersec } else { $null }
        diskTransfersPerSecond = if ($disk) { [int64]$disk.DiskTransfersPersec } else { $null }
    }
}

function Start-CpuWorkers {
    param([int]$WorkerCount, [DateTime]$DeadlineUtc)
    $jobs = @()
    for ($worker = 0; $worker -lt $WorkerCount; ++$worker) {
        $jobs += Start-Job -ScriptBlock {
            param([DateTime]$Deadline)
            $accumulator = 0.0
            while ([DateTime]::UtcNow -lt $Deadline) {
                for ($index = 1; $index -le 250000; ++$index) {
                    $accumulator += [Math]::Sqrt($index) * 0.000001
                }
            }
            $accumulator | Out-Null
        } -ArgumentList $DeadlineUtc
    }
    return $jobs
}

function Start-DiskWorker {
    param([string]$StressDirectory, [int]$Megabytes, [DateTime]$DeadlineUtc)
    Start-Job -ScriptBlock {
        param([string]$Directory, [int]$MiB, [DateTime]$Deadline)
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
        $path = Join-Path $Directory 'bounded-disk-stress.bin'
        $buffer = New-Object byte[] (4MB)
        $random = [System.Random]::new(419)
        $random.NextBytes($buffer)
        try {
            $stream = [System.IO.FileStream]::new(
                $path,
                [System.IO.FileMode]::Create,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None,
                $buffer.Length,
                [System.IO.FileOptions]::WriteThrough)
            try {
                $remaining = [int64]$MiB * 1MB
                while ($remaining -gt 0) {
                    $count = [int][Math]::Min([int64]$buffer.Length, $remaining)
                    $stream.Write($buffer, 0, $count)
                    $remaining -= $count
                }
                $stream.Flush($true)
                while ([DateTime]::UtcNow -lt $Deadline) {
                    $stream.Position = 0
                    while ($stream.Position -lt $stream.Length -and [DateTime]::UtcNow -lt $Deadline) {
                        $count = $stream.Read($buffer, 0, $buffer.Length)
                        if ($count -le 0) { break }
                    }
                    $stream.Position = 0
                    $stream.Write($buffer, 0, $buffer.Length)
                    $stream.Flush($true)
                }
            } finally {
                $stream.Dispose()
            }
        } finally {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        }
    } -ArgumentList $StressDirectory, $Megabytes, $DeadlineUtc
}

$scenarioTargets = @{
    Idle = 0
    ModerateCpu = 55
    HeavyCpu = 80
    SevereCpu = 92
    NearSaturationCpu = 97
    Disk = 0
    Combined = 92
}
$cpuTargetPercent = [int]$scenarioTargets[$Scenario]
$logicalProcessors = [Environment]::ProcessorCount
$workerCount = if ($cpuTargetPercent -gt 0) {
    [Math]::Min($MaximumCpuWorkers,
        [Math]::Max(1, [Math]::Ceiling($logicalProcessors * $cpuTargetPercent / 100.0)))
} else { 0 }
$usesDisk = $Scenario -in @('Disk', 'Combined')
$stressRoot = Join-Path $env:TEMP ("HOTAS-BF6-responsiveness-{0}" -f $PID)
$probeReport = Join-Path $EvidenceDirectory ("{0}-probe.json" -f $Scenario.ToLowerInvariant())
$loadEvidence = Join-Path $EvidenceDirectory ("{0}-load.json" -f $Scenario.ToLowerInvariant())
$deadlineUtc = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
$jobs = @()

New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$priorEnvironment = @{
    HOTAS_RESPONSIVENESS_PROBE = $env:HOTAS_RESPONSIVENESS_PROBE
    HOTAS_RESPONSIVENESS_PROBE_OUTPUT = $env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT
    HOTAS_RESPONSIVENESS_SCENARIO = $env:HOTAS_RESPONSIVENESS_SCENARIO
    HOTAS_RESPONSIVENESS_LOAD_EVIDENCE = $env:HOTAS_RESPONSIVENESS_LOAD_EVIDENCE
}

try {
    $env:HOTAS_RESPONSIVENESS_PROBE = '1'
    $env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT = $probeReport
    $env:HOTAS_RESPONSIVENESS_SCENARIO = $Scenario
    $env:HOTAS_RESPONSIVENESS_LOAD_EVIDENCE = $loadEvidence

    $before = Get-LoadSample
    $ambientCpuAlreadyAtOrAboveTarget = $cpuTargetPercent -gt 0 -and $null -ne $before.cpuPercent `
        -and $before.cpuPercent -ge $cpuTargetPercent
    if ($ambientCpuAlreadyAtOrAboveTarget) {
        # Do not turn a host that is already saturated into a worse stress
        # experiment. The result remains valuable, but the load file makes
        # clear that CPU contention was ambient rather than calibrated here.
        $workerCount = 0
    }
    if ($workerCount -gt 0) { $jobs += Start-CpuWorkers -WorkerCount $workerCount -DeadlineUtc $deadlineUtc }
    if ($usesDisk) { $jobs += Start-DiskWorker -StressDirectory $stressRoot -Megabytes $DiskMiB -DeadlineUtc $deadlineUtc }
    Start-Sleep -Seconds 2
    $underLoad = Get-LoadSample

    # A Windows GUI subsystem executable does not reliably populate
    # $LASTEXITCODE when invoked with the call operator. Start-Process gives
    # the harness an explicit, per-process exit code for the native app.
    $workloadProcess = Start-Process -FilePath $WorkloadExecutable -ArgumentList $WorkloadArguments -PassThru -Wait
    $workloadExitCode = $workloadProcess.ExitCode
    $afterWorkload = Get-LoadSample
    if ($workloadExitCode -ne 0) { throw "Workload exited with code $workloadExitCode." }

    [ordered]@{
        schemaVersion = 1
        scenario = $Scenario
        durationSeconds = $DurationSeconds
        boundedDiskMiB = if ($usesDisk) { $DiskMiB } else { 0 }
        requestedCpuPercent = $cpuTargetPercent
        cpuWorkerCount = $workerCount
        maximumCpuWorkers = $MaximumCpuWorkers
        ambientCpuAlreadyAtOrAboveRequestedTarget = $ambientCpuAlreadyAtOrAboveTarget
        logicalProcessors = $logicalProcessors
        workloadExecutable = (Resolve-Path -LiteralPath $WorkloadExecutable).Path
        workloadArguments = $WorkloadArguments
        probeReport = $probeReport
        before = $before
        underLoad = $underLoad
        afterWorkload = $afterWorkload
        cleanup = 'stress jobs stopped and the task-owned temporary disk file was removed in finally'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $loadEvidence -Encoding utf8
    Get-Content -Raw -LiteralPath $loadEvidence
} finally {
    foreach ($job in @($jobs)) {
        if ($job) {
            Stop-Job -Job $job -ErrorAction SilentlyContinue
            Receive-Job -Job $job -ErrorAction SilentlyContinue | Out-Null
            Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
        }
    }
    if ($stressRoot -like "$env:TEMP\\HOTAS-BF6-responsiveness-*") {
        Remove-Item -LiteralPath $stressRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    foreach ($name in $priorEnvironment.Keys) {
        if ($null -eq $priorEnvironment[$name]) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
        else { Set-Item "Env:$name" $priorEnvironment[$name] }
    }
}
