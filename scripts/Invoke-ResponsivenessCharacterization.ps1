[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Idle', 'ModerateCpu', 'HeavyCpu', 'SevereCpu', 'NearSaturationCpu', 'Disk', 'Combined')]
    [string]$Scenario,

    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$WorkloadExecutable,

    [string[]]$WorkloadArguments = @(),

    # Near-saturation native qualification can legitimately take longer than
    # the action program itself because queued GUI work must drain.  Keep the
    # stress deadline bounded, but allow it to cover that delayed completion.
    [ValidateRange(8, 720)]
    [int]$DurationSeconds = 30,

    [ValidateRange(32, 512)]
    [int]$DiskMiB = 128,

    [ValidateRange(1, 64)]
    [int]$MaximumCpuWorkers = 8,

    # Phase 3 uses the established bounded workload while calibrating around
    # the scheduler cliff. A supplied value overrides the scenario default;
    # the captured load record remains authoritative.
    [ValidateRange(-1, 100)]
    [int]$CpuTargetPercent = -1,

    # A host may expose logical processors whose sustained PowerShell-worker
    # utilization differs from a one-worker-per-logical-CPU estimate. Phase 3
    # may supply a bounded explicit count and still reports the sampled load.
    [ValidateRange(-1, 64)]
    [int]$CpuWorkerCount = -1,

    [switch]$WithDisk,

    [ValidateSet('current', 'gui', 'render', 'gui-render', 'process-above-normal')]
    [string]$SchedulingPolicy = 'current',

    # Phase 4 qualification may exercise the production-safe automatic
    # controller or one explicit development override. This is process-local
    # test setup, never a user-facing setting.
    [ValidateSet('auto', 'normal', 'pressure', 'severe')]
    [string]$ContentionLevel = 'auto',

    [ValidateRange(0, 30)]
    [int]$WarmupSeconds = 2,

    [ValidateRange(0, 180)]
    [int]$NativeQualificationTortureSeconds = 10,

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
    param([int]$WorkerCount, [int]$TargetPercent, [DateTime]$DeadlineUtc)
    # Starting one out-of-process PowerShell job per CPU worker can starve the
    # harness before it ever launches the native executable.  Keep the same
    # bounded number of runnable CPU threads in one task-owned helper process
    # instead.  This is a load generator, not application code.
    $source = @'
using System;
using System.Threading;

public static class HotasResponsivenessCpuLoad
{
    public static void Run(int workerCount, int targetPercent, long deadlineUtcTicks)
    {
        var deadline = new DateTime(deadlineUtcTicks, DateTimeKind.Utc);
        var workers = new Thread[workerCount];
        var activeCoreUnits = workerCount * targetPercent / 100.0;
        var fullyBusyWorkers = (int)Math.Floor(activeCoreUnits);
        var fractionalWorkerPercent = (int)Math.Round((activeCoreUnits - fullyBusyWorkers) * 100.0);
        for (var worker = 0; worker < workerCount; ++worker)
        {
            var workerPercent = worker < fullyBusyWorkers ? 100 :
                worker == fullyBusyWorkers ? fractionalWorkerPercent : 0;
            workers[worker] = new Thread(() =>
            {
                double accumulator = 0.0;
                while (DateTime.UtcNow < deadline)
                {
                    var cycleStart = Environment.TickCount;
                    var busyUntil = cycleStart + workerPercent;
                    var cycleEnd = cycleStart + 100;
                    while (Environment.TickCount < busyUntil && DateTime.UtcNow < deadline)
                    {
                        for (var index = 1; index <= 250000; ++index)
                            accumulator += Math.Sqrt(index) * 0.000001;
                    }
                    while (Environment.TickCount < cycleEnd && DateTime.UtcNow < deadline)
                        Thread.Sleep(1);
                }
                GC.KeepAlive(accumulator);
            });
            workers[worker].IsBackground = true;
            workers[worker].Start();
        }
        foreach (var worker in workers)
            worker.Join();
    }
}
'@
    $command = @"
Add-Type -TypeDefinition @'
$source
'@
[HotasResponsivenessCpuLoad]::Run($WorkerCount, $TargetPercent, $($DeadlineUtc.Ticks))
"@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    return Start-Process -FilePath powershell.exe `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encoded) `
        -PassThru -WindowStyle Hidden
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
$cpuTargetPercent = if ($CpuTargetPercent -ge 0) { $CpuTargetPercent } else { [int]$scenarioTargets[$Scenario] }
$logicalProcessors = [Environment]::ProcessorCount
$workerCount = if ($CpuWorkerCount -ge 0) {
    $CpuWorkerCount
} elseif ($cpuTargetPercent -gt 0) {
    [Math]::Min($MaximumCpuWorkers,
        [Math]::Max(1, [Math]::Ceiling($logicalProcessors * $cpuTargetPercent / 100.0)))
} else { 0 }
$cpuThreadCount = [Math]::Min($workerCount, $logicalProcessors)
$generatorCpuTargetPercent = 0
$usesDisk = $WithDisk -or $Scenario -in @('Disk', 'Combined')
$stressRoot = Join-Path $env:TEMP ("HOTAS-BF6-responsiveness-{0}" -f $PID)
$probeReport = Join-Path $EvidenceDirectory ("{0}-probe.json" -f $Scenario.ToLowerInvariant())
$loadEvidence = Join-Path $EvidenceDirectory ("{0}-load.json" -f $Scenario.ToLowerInvariant())
# Keep stress active for the declared workload interval after warmup. This
# avoids falsely treating a slow native run as loaded after its workers have
# already expired.
$deadlineUtc = [DateTime]::UtcNow.AddSeconds($DurationSeconds + $WarmupSeconds)
$jobs = @()
$cpuStressProcess = $null
$workloadProcess = $null
$workloadTimedOut = $false

New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$priorEnvironment = @{
    HOTAS_RESPONSIVENESS_PROBE = $env:HOTAS_RESPONSIVENESS_PROBE
    HOTAS_RESPONSIVENESS_PROBE_OUTPUT = $env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT
    HOTAS_RESPONSIVENESS_SCENARIO = $env:HOTAS_RESPONSIVENESS_SCENARIO
    HOTAS_RESPONSIVENESS_LOAD_EVIDENCE = $env:HOTAS_RESPONSIVENESS_LOAD_EVIDENCE
    HOTAS_RESPONSIVENESS_SCHEDULING_POLICY = $env:HOTAS_RESPONSIVENESS_SCHEDULING_POLICY
    HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION = $env:HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION
    HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION_TORTURE_SECONDS = $env:HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION_TORTURE_SECONDS
    HOTAS_CONTENTION_LEVEL = $env:HOTAS_CONTENTION_LEVEL
}

try {
    $env:HOTAS_RESPONSIVENESS_PROBE = '1'
    $env:HOTAS_RESPONSIVENESS_PROBE_OUTPUT = $probeReport
    $env:HOTAS_RESPONSIVENESS_SCENARIO = $Scenario
    $env:HOTAS_RESPONSIVENESS_LOAD_EVIDENCE = $loadEvidence
    $env:HOTAS_RESPONSIVENESS_SCHEDULING_POLICY = $SchedulingPolicy
    $env:HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION = '1'
    $env:HOTAS_RESPONSIVENESS_NATIVE_QUALIFICATION_TORTURE_SECONDS = "$NativeQualificationTortureSeconds"
    $env:HOTAS_CONTENTION_LEVEL = $ContentionLevel

    $before = Get-LoadSample
    $ambientCpuAlreadyAtOrAboveTarget = $cpuTargetPercent -gt 0 -and $null -ne $before.cpuPercent `
        -and $before.cpuPercent -ge $cpuTargetPercent
    if ($ambientCpuAlreadyAtOrAboveTarget) {
        # Do not turn a host that is already saturated into a worse stress
        # experiment. The result remains valuable, but the load file makes
        # clear that CPU contention was ambient rather than calibrated here.
        $workerCount = 0
        $cpuThreadCount = 0
    } elseif ($cpuTargetPercent -gt 0 -and $null -ne $before.cpuPercent) {
        # The workload target describes total machine CPU, not CPU in addition
        # to an active desktop.  Generate only the measured headroom; final
        # five-second samples remain the acceptance record.
        $generatorCpuTargetPercent = [Math]::Max(0, $cpuTargetPercent - [int][Math]::Round($before.cpuPercent))
    }
    if ($cpuThreadCount -gt 0) {
        $cpuStressProcess = Start-CpuWorkers -WorkerCount $cpuThreadCount -TargetPercent $generatorCpuTargetPercent -DeadlineUtc $deadlineUtc
    }
    if ($usesDisk) { $jobs += Start-DiskWorker -StressDirectory $stressRoot -Megabytes $DiskMiB -DeadlineUtc $deadlineUtc }
    if ($WarmupSeconds -gt 0) { Start-Sleep -Seconds $WarmupSeconds }
    $underLoad = Get-LoadSample

    # A Windows GUI subsystem executable does not reliably populate
    # $LASTEXITCODE when invoked with the call operator. Start-Process gives
    # the harness an explicit, per-process exit code for the native app.
    $startWorkload = @{ FilePath = $WorkloadExecutable; PassThru = $true; Wait = $true }
    # Some supported PowerShell hosts reject an explicitly supplied empty
    # ArgumentList collection. Omit it for the normal no-argument native run.
    if ($WorkloadArguments.Count -gt 0) { $startWorkload.ArgumentList = $WorkloadArguments }
    $startWorkload.Wait = $false
    $workloadProcess = Start-Process @startWorkload
    # A single warmup sample is not enough to describe a long native run on a
    # busy host. Five-second process-lifetime samples are intentionally low
    # rate and capture the actual CPU range without adding load to the app.
    $duringWorkload = @()
    while (-not $workloadProcess.HasExited) {
        if ([DateTime]::UtcNow -ge $deadlineUtc) {
            # A report after the load generator expires would be an unloaded
            # tail, not a scheduler qualification result.
            $workloadTimedOut = $true
            Stop-Process -Id $workloadProcess.Id -Force -ErrorAction SilentlyContinue
            break
        }
        Start-Sleep -Seconds 5
        $workloadProcess.Refresh()
        if (-not $workloadProcess.HasExited) { $duringWorkload += Get-LoadSample }
    }
    $workloadProcess.WaitForExit()
    $workloadExitCode = $workloadProcess.ExitCode
    $afterWorkload = Get-LoadSample

    [ordered]@{
        schemaVersion = 1
        scenario = $Scenario
        durationSeconds = $DurationSeconds
        warmupSeconds = $WarmupSeconds
        boundedDiskMiB = if ($usesDisk) { $DiskMiB } else { 0 }
        requestedCpuPercent = $cpuTargetPercent
        requestedGeneratorCpuPercent = $generatorCpuTargetPercent
        requestedCpuWorkerCount = if ($CpuWorkerCount -ge 0) { $CpuWorkerCount } else { $null }
        requestedSchedulingPolicy = $SchedulingPolicy
        requestedContentionLevel = $ContentionLevel
        nativeQualificationTortureSeconds = $NativeQualificationTortureSeconds
        workloadTimedOutBeforeStressDeadline = $workloadTimedOut
        workloadExitCode = $workloadExitCode
        cpuWorkerCount = $workerCount
        cpuWorkerThreadCount = $cpuThreadCount
        maximumCpuWorkers = $MaximumCpuWorkers
        ambientCpuAlreadyAtOrAboveRequestedTarget = $ambientCpuAlreadyAtOrAboveTarget
        logicalProcessors = $logicalProcessors
        workloadExecutable = (Resolve-Path -LiteralPath $WorkloadExecutable).Path
        workloadArguments = $WorkloadArguments
        probeReport = $probeReport
        before = $before
        underLoad = $underLoad
        duringWorkload = $duringWorkload
        observedCpuPercent = if ($duringWorkload.Count) {
            $values = @($duringWorkload | Where-Object { $null -ne $_.cpuPercent } | ForEach-Object { [double]$_.cpuPercent })
            if ($values.Count) {
                [ordered]@{
                    minimum = [Math]::Round(($values | Measure-Object -Minimum).Minimum, 1)
                    average = [Math]::Round(($values | Measure-Object -Average).Average, 1)
                    maximum = [Math]::Round(($values | Measure-Object -Maximum).Maximum, 1)
                }
            } else { $null }
        } else { $null }
        afterWorkload = $afterWorkload
        cleanup = 'stress jobs stopped and the task-owned temporary disk file was removed in finally'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $loadEvidence -Encoding utf8
    Get-Content -Raw -LiteralPath $loadEvidence
    if ($workloadTimedOut) {
        throw "Workload did not complete before the bounded stress deadline."
    }
    if ($workloadExitCode -ne 0) { throw "Workload exited with code $workloadExitCode." }
} finally {
    if ($workloadProcess -and -not $workloadProcess.HasExited) {
        Stop-Process -Id $workloadProcess.Id -Force -ErrorAction SilentlyContinue
        $workloadProcess.WaitForExit()
    }
    if ($cpuStressProcess -and -not $cpuStressProcess.HasExited) {
        Stop-Process -Id $cpuStressProcess.Id -Force -ErrorAction SilentlyContinue
        $cpuStressProcess.WaitForExit()
    }
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
