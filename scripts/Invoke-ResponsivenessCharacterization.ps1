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

    # Phase 7 drives a host-total band, not a nominal generator percentage.
    # The interval is intentionally slow enough to avoid control oscillation
    # and avoids high-frequency WMI polling.
    [ValidateRange(250, 2000)]
    [int]$CpuControlIntervalMs = 1000,

    [ValidateRange(1, 99)]
    [int]$CpuBandLowerPercent = 90,

    [ValidateRange(2, 100)]
    [int]$CpuBandUpperPercent = 95,

    # A supplied target overrides the scenario default; the observed
    # total-host load record remains authoritative.
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
    [int]$WarmupSeconds = 8,

    [ValidateRange(0, 180)]
    [int]$NativeQualificationTortureSeconds = 10,

    [string]$EvidenceDirectory = (Join-Path $env:TEMP 'HOTAS-BF6-responsiveness-evidence')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($CpuBandLowerPercent -ge $CpuBandUpperPercent) {
    throw 'CpuBandLowerPercent must be lower than CpuBandUpperPercent.'
}

# GetSystemTimes is the same lightweight total-CPU source used by the Phase 4
# controller. It avoids the old five-second Win32_Processor/WMI sample that
# caused Phase 6's requested load to be only loosely related to the host load.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class HotasPhase7SystemTimes
{
    [StructLayout(LayoutKind.Sequential)]
    private struct FILETIME
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetSystemTimes(out FILETIME idleTime,
                                              out FILETIME kernelTime,
                                              out FILETIME userTime);

    private static ulong ToUInt64(FILETIME value)
    {
        return ((ulong)value.dwHighDateTime << 32) | value.dwLowDateTime;
    }

    public static ulong[] Read()
    {
        FILETIME idle;
        FILETIME kernel;
        FILETIME user;
        if (!GetSystemTimes(out idle, out kernel, out user))
            throw new InvalidOperationException("GetSystemTimes failed.");
        return new [] { ToUInt64(idle), ToUInt64(kernel), ToUInt64(user) };
    }
}
'@

$script:phase7SystemTimesBaseline = $null
function Get-TotalSystemCpuPercent {
    $sample = [HotasPhase7SystemTimes]::Read()
    if ($null -eq $script:phase7SystemTimesBaseline) {
        $script:phase7SystemTimesBaseline = $sample
        return $null
    }
    $previous = $script:phase7SystemTimesBaseline
    $script:phase7SystemTimesBaseline = $sample
    $kernelDelta = [UInt64]$sample[1] - [UInt64]$previous[1]
    $userDelta = [UInt64]$sample[2] - [UInt64]$previous[2]
    $idleDelta = [UInt64]$sample[0] - [UInt64]$previous[0]
    $totalDelta = $kernelDelta + $userDelta
    if ($totalDelta -eq 0) { return $null }
    $percent = (1.0 - ([double]$idleDelta / [double]$totalDelta)) * 100.0
    return [Math]::Round([Math]::Min(100.0, [Math]::Max(0.0, $percent)), 1)
}

function Get-LoadSample {
    param([switch]$IncludeDisk)
    $disk = $null
    if ($IncludeDisk) {
        # Disk observation is intentionally low-rate. The closed-loop CPU
        # control path never uses CIM/WMI.
        $disk = Get-CimInstance Win32_PerfFormattedData_PerfDisk_PhysicalDisk -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -eq '_Total' } | Select-Object -First 1
    }
    [ordered]@{
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        cpuPercent = Get-TotalSystemCpuPercent
        diskBytesPerSecond = if ($disk) { [int64]$disk.DiskBytesPersec } else { $null }
        diskTransfersPerSecond = if ($disk) { [int64]$disk.DiskTransfersPersec } else { $null }
    }
}

function New-CpuDutyChannel {
    $name = "HOTAS-BF6-Phase7-$PID-$([Guid]::NewGuid().ToString('N'))"
    $map = [System.IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($name, 4)
    $accessor = $map.CreateViewAccessor()
    $accessor.Write(0, [int]0)
    return [pscustomobject]@{ Name = $name; Map = $map; Accessor = $accessor }
}

function Set-CpuDuty {
    param([Parameter(Mandatory)]$Channel, [int]$DutyPercent)
    $bounded = [Math]::Min(100, [Math]::Max(0, $DutyPercent))
    $Channel.Accessor.Write(0, [int]$bounded)
    return $bounded
}

function Start-CpuWorkers {
    param([int]$WorkerCount, [string]$DutyChannelName, [DateTime]$DeadlineUtc)
    # One task-owned helper process hosts a bounded worker pool. The parent
    # changes its duty via a named in-memory channel; it never spawns a worker
    # process per CPU and it does not write a duty record per control turn.
    $source = @'
using System;
using System.IO.MemoryMappedFiles;
using System.Threading;

public static class HotasPhase7CpuLoad
{
    public static void Run(int workerCount, string dutyChannelName, long deadlineUtcTicks)
    {
        var deadline = new DateTime(deadlineUtcTicks, DateTimeKind.Utc);
        using (var map = MemoryMappedFile.OpenExisting(dutyChannelName, MemoryMappedFileRights.Read))
        {
            var workers = new Thread[workerCount];
            for (var worker = 0; worker < workerCount; ++worker)
            {
                var staggerMs = worker * 100 / Math.Max(1, workerCount);
                workers[worker] = new Thread(() =>
                {
                    using (var accessor = map.CreateViewAccessor(0, 4, MemoryMappedFileAccess.Read))
                    {
                        double accumulator = 0.0;
                        // Do not let every worker enter its duty window at
                        // once. Staggered 100 ms windows produce a smoother
                        // total host load and prevent sampling phase beats.
                        if (staggerMs > 0) Thread.Sleep(staggerMs);
                        while (DateTime.UtcNow < deadline)
                        {
                            var duty = accessor.ReadInt32(0);
                            if (duty < 0) duty = 0;
                            if (duty > 100) duty = 100;
                            var cycleStart = Environment.TickCount;
                            var busyUntil = cycleStart + duty;
                            var cycleEnd = cycleStart + 100;
                            while (Environment.TickCount < busyUntil && DateTime.UtcNow < deadline)
                            {
                                for (var index = 1; index <= 5000; ++index)
                                    accumulator += Math.Sqrt(index) * 0.000001;
                            }
                            while (Environment.TickCount < cycleEnd && DateTime.UtcNow < deadline)
                                Thread.Sleep(1);
                        }
                        GC.KeepAlive(accumulator);
                    }
                });
                workers[worker].IsBackground = true;
                workers[worker].Start();
            }
            foreach (var worker in workers)
                worker.Join();
        }
    }
}
'@
    $command = @"
Add-Type -TypeDefinition @'
$source
'@
[HotasPhase7CpuLoad]::Run($WorkerCount, '$DutyChannelName', $($DeadlineUtc.Ticks))
"@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    return Start-Process -FilePath powershell.exe `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encoded) `
        -PassThru -WindowStyle Hidden
}

function Invoke-CpuControlTurn {
    param([ValidateSet('warmup', 'workload')][string]$Phase)
    $state = $script:phase7CpuControl
    if (-not $state.enabled) { return $null }
    $cpuPercent = Get-TotalSystemCpuPercent
    if ($null -eq $cpuPercent) { return $null }

    $dutyBefore = [int]$state.dutyPercent
    $controllerCpuPercent = if ($null -eq $state.smoothedCpuPercent) {
        [double]$cpuPercent
    } else {
        [Math]::Round(($state.smoothedCpuPercent * 0.65) + ([double]$cpuPercent * 0.35), 1)
    }
    $state.smoothedCpuPercent = $controllerCpuPercent
    $adjustment = 0
    # Deliberately small dead-band controller: it is enough to hold a gaming
    # pressure band without pretending to be a PID tuner. The outer guard is
    # wider than the target band so short Windows scheduling noise does not
    # immediately reverse the prior correction.
    if ($controllerCpuPercent -lt ($state.lowerPercent - 3.0)) { $adjustment = 2 }
    elseif ($controllerCpuPercent -lt $state.lowerPercent) { $adjustment = 1 }
    elseif ($controllerCpuPercent -gt ($state.upperPercent + 3.0)) { $adjustment = -2 }
    elseif ($controllerCpuPercent -gt $state.upperPercent) { $adjustment = -1 }

    $dutyAfter = Set-CpuDuty -Channel $state.channel -DutyPercent ($dutyBefore + $adjustment)
    $state.dutyPercent = $dutyAfter
    if ($Phase -eq 'workload') { ++$state.workloadTurns }
    $steadyState = $Phase -eq 'workload' -and $state.workloadTurns -gt 3
    $record = [ordered]@{
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        elapsedMs = [Math]::Round($state.elapsed.Elapsed.TotalMilliseconds, 1)
        phase = $Phase
        steadyState = $steadyState
        cpuPercent = $cpuPercent
        controllerCpuPercent = $controllerCpuPercent
        dutyPercentBefore = $dutyBefore
        adjustmentPercent = $adjustment
        dutyPercentAfter = $dutyAfter
    }
    [void]$state.samples.Add([pscustomobject]$record)
    return $record
}

function Get-CpuLoadQualification {
    param([Parameter(Mandatory)]$State)
    $workload = @($State.samples | Where-Object { $_.phase -eq 'workload' -and $null -ne $_.cpuPercent })
    $steady = @($workload | Where-Object { $_.steadyState })
    if ($steady.Count -eq 0) { $steady = $workload }
    if ($steady.Count -eq 0) {
        return [ordered]@{ sampleCount = 0; stable = $false }
    }
    $rawValues = @($steady | ForEach-Object { [double]$_.cpuPercent })
    # Control samples remain one second apart, but Windows can quantize one
    # sample around synchronized scheduler turns. Qualify the actual host
    # load over a transparent rolling five-second window while retaining every
    # raw sample below. This represents sustained game-load pressure rather
    # than cherry-picking a favorable instantaneous value.
    $windowSampleCount = [Math]::Max(1, [int][Math]::Ceiling(5000.0 / $State.intervalMs))
    $values = [System.Collections.Generic.List[double]]::new()
    for ($index = $windowSampleCount - 1; $index -lt $rawValues.Count; ++$index) {
        $window = $rawValues[($index - $windowSampleCount + 1)..$index]
        $values.Add([Math]::Round(($window | Measure-Object -Average).Average, 1))
    }
    if ($values.Count -eq 0) {
        return [ordered]@{
            sampleCount = 0
            rawControlSamples = [ordered]@{ sampleCount = $rawValues.Count }
            stabilityWindow = [ordered]@{ durationMs = $windowSampleCount * $State.intervalMs; sampleCount = 0 }
            stable = $false
        }
    }
    $insideTarget = @($values | Where-Object { ($_ -ge $State.lowerPercent) -and ($_ -le $State.upperPercent) })
    # The Phase 7 acceptance envelope allows normal Windows sampling variance
    # around the requested band while keeping the windowed mean in-band.
    $insideEnvelope = @($values | Where-Object {
        ($_ -ge $State.acceptanceLowerPercent) -and ($_ -le $State.acceptanceUpperPercent)
    })
    $longestSamples = 0
    $currentSamples = 0
    foreach ($value in @($values)) {
        if ($value -ge $State.acceptanceLowerPercent -and $value -le $State.acceptanceUpperPercent) {
            ++$currentSamples
            $longestSamples = [Math]::Max($longestSamples, $currentSamples)
        } else {
            $currentSamples = 0
        }
    }
    $average = [Math]::Round(($values | Measure-Object -Average).Average, 1)
    $targetPercent = [Math]::Round(100.0 * $insideTarget.Count / $values.Count, 1)
    $envelopePercent = [Math]::Round(100.0 * $insideEnvelope.Count / $values.Count, 1)
    return [ordered]@{
        targetBand = [ordered]@{ lowerPercent = $State.lowerPercent; upperPercent = $State.upperPercent; midpointPercent = $State.midpointPercent }
        acceptanceEnvelope = [ordered]@{ lowerPercent = $State.acceptanceLowerPercent; upperPercent = $State.acceptanceUpperPercent; requiredPercent = 80.0; averageMustRemainInTargetBand = $true }
        sampleCount = $values.Count
        minimumPercent = [Math]::Round(($values | Measure-Object -Minimum).Minimum, 1)
        averagePercent = $average
        maximumPercent = [Math]::Round(($values | Measure-Object -Maximum).Maximum, 1)
        percentInsideTargetBand = $targetPercent
        percentInsideAcceptanceEnvelope = $envelopePercent
        longestContinuousEnvelopeMs = ($longestSamples + $windowSampleCount - 1) * $State.intervalMs
        rawControlSamples = [ordered]@{
            sampleCount = $rawValues.Count
            minimumPercent = [Math]::Round(($rawValues | Measure-Object -Minimum).Minimum, 1)
            averagePercent = [Math]::Round(($rawValues | Measure-Object -Average).Average, 1)
            maximumPercent = [Math]::Round(($rawValues | Measure-Object -Maximum).Maximum, 1)
        }
        stabilityWindow = [ordered]@{ durationMs = $windowSampleCount * $State.intervalMs; sampleCount = $values.Count }
        stable = $envelopePercent -ge 80.0 -and $average -ge $State.lowerPercent -and $average -le $State.upperPercent
    }
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
$controlLowerPercent = if ($cpuTargetPercent -ge 90 -and $cpuTargetPercent -le 95) {
    [double]$CpuBandLowerPercent
} else {
    [Math]::Max(0.0, [double]$cpuTargetPercent - 2.0)
}
$controlUpperPercent = if ($cpuTargetPercent -ge 90 -and $cpuTargetPercent -le 95) {
    [double]$CpuBandUpperPercent
} else {
    [Math]::Min(100.0, [double]$cpuTargetPercent + 2.0)
}
$controlMidpointPercent = [Math]::Round(($controlLowerPercent + $controlUpperPercent) / 2.0, 1)
$acceptanceLowerPercent = if ($controlLowerPercent -eq 90.0 -and $controlUpperPercent -eq 95.0) {
    88.0
} else {
    [Math]::Max(0.0, $controlLowerPercent - 2.0)
}
$acceptanceUpperPercent = if ($controlLowerPercent -eq 90.0 -and $controlUpperPercent -eq 95.0) {
    97.0
} else {
    [Math]::Min(100.0, $controlUpperPercent + 2.0)
}
$initialWorkerDutyPercent = 0
$usesDisk = $WithDisk -or $Scenario -in @('Disk', 'Combined')
$stressRoot = Join-Path $env:TEMP ("HOTAS-BF6-responsiveness-{0}" -f $PID)
$probeReport = Join-Path $EvidenceDirectory ("{0}-probe.json" -f $Scenario.ToLowerInvariant())
$loadEvidence = Join-Path $EvidenceDirectory ("{0}-load.json" -f $Scenario.ToLowerInvariant())
# Keep stress active for the declared workload interval after warmup. This
# avoids falsely treating a slow native run as loaded after its workers have
# already expired.
$deadlineUtc = [DateTime]::MinValue
$jobs = @()
$cpuStressProcess = $null
$cpuDutyChannel = $null
$script:phase7CpuControl = [pscustomobject]@{
    enabled = $false
    channel = $null
    dutyPercent = 0
    lowerPercent = $controlLowerPercent
    upperPercent = $controlUpperPercent
    midpointPercent = $controlMidpointPercent
    acceptanceLowerPercent = $acceptanceLowerPercent
    acceptanceUpperPercent = $acceptanceUpperPercent
    intervalMs = $CpuControlIntervalMs
    workloadTurns = 0
    smoothedCpuPercent = $null
    elapsed = [System.Diagnostics.Stopwatch]::StartNew()
    samples = [System.Collections.Generic.List[object]]::new()
}
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

    # Prime the lightweight GetSystemTimes delta before taking the baseline.
    $null = Get-TotalSystemCpuPercent
    Start-Sleep -Milliseconds $CpuControlIntervalMs
    $before = Get-LoadSample -IncludeDisk
    $ambientCpuAlreadyAtOrAboveTarget = $cpuTargetPercent -gt 0 -and $null -ne $before.cpuPercent `
        -and $before.cpuPercent -ge $controlUpperPercent
    if ($ambientCpuAlreadyAtOrAboveTarget) {
        # Do not turn a host that is already saturated into a worse stress
        # experiment. The result remains valuable, but the load file makes
        # clear that CPU contention was ambient rather than calibrated here.
        $workerCount = 0
        $cpuThreadCount = 0
    } elseif ($cpuTargetPercent -gt 0 -and $null -ne $before.cpuPercent) {
        # Estimate only an initial duty. The closed loop below owns every
        # subsequent correction against total host CPU, so ambient desktop
        # work is compensated rather than treated as a fixed offset.
        $headroom = [Math]::Max(0.0, $controlMidpointPercent - [double]$before.cpuPercent)
        $initialWorkerDutyPercent = [int][Math]::Round(
            [Math]::Min(100.0, $headroom * $logicalProcessors / [Math]::Max(1, $cpuThreadCount)))
    }
    $deadlineUtc = [DateTime]::UtcNow.AddSeconds($DurationSeconds + $WarmupSeconds)
    if ($cpuThreadCount -gt 0) {
        $cpuDutyChannel = New-CpuDutyChannel
        $script:phase7CpuControl.channel = $cpuDutyChannel
        $script:phase7CpuControl.enabled = $true
        $script:phase7CpuControl.dutyPercent = Set-CpuDuty -Channel $cpuDutyChannel -DutyPercent $initialWorkerDutyPercent
        $cpuStressProcess = Start-CpuWorkers -WorkerCount $cpuThreadCount `
            -DutyChannelName $cpuDutyChannel.Name -DeadlineUtc $deadlineUtc
    }
    if ($usesDisk) { $jobs += Start-DiskWorker -StressDirectory $stressRoot -Megabytes $DiskMiB -DeadlineUtc $deadlineUtc }
    $warmupDeadlineUtc = [DateTime]::UtcNow.AddSeconds($WarmupSeconds)
    while ([DateTime]::UtcNow -lt $warmupDeadlineUtc) {
        Start-Sleep -Milliseconds $CpuControlIntervalMs
        Invoke-CpuControlTurn -Phase warmup | Out-Null
    }
    $underLoad = Get-LoadSample -IncludeDisk

    # A Windows GUI subsystem executable does not reliably populate
    # $LASTEXITCODE when invoked with the call operator. Start-Process gives
    # the harness an explicit, per-process exit code for the native app.
    $startWorkload = @{ FilePath = $WorkloadExecutable; PassThru = $true; Wait = $true }
    # Some supported PowerShell hosts reject an explicitly supplied empty
    # ArgumentList collection. Omit it for the normal no-argument native run.
    if ($WorkloadArguments.Count -gt 0) { $startWorkload.ArgumentList = $WorkloadArguments }
    $startWorkload.Wait = $false
    $workloadProcess = Start-Process @startWorkload
    # Control and qualification use the same 750 ms-class GetSystemTimes
    # sample. That produces a meaningful total-host stability record without
    # adding WMI overhead to the stressed application.
    $duringWorkload = @()
    while (-not $workloadProcess.HasExited) {
        if ([DateTime]::UtcNow -ge $deadlineUtc) {
            # A report after the load generator expires would be an unloaded
            # tail, not a scheduler qualification result.
            $workloadTimedOut = $true
            Stop-Process -Id $workloadProcess.Id -Force -ErrorAction SilentlyContinue
            break
        }
        Start-Sleep -Milliseconds $CpuControlIntervalMs
        $workloadProcess.Refresh()
        if (-not $workloadProcess.HasExited) {
            $controlSample = Invoke-CpuControlTurn -Phase workload
            if ($null -ne $controlSample) { $duringWorkload += $controlSample }
        }
    }
    $workloadProcess.WaitForExit()
    $workloadExitCode = $workloadProcess.ExitCode
    $afterWorkload = Get-LoadSample -IncludeDisk
    $cpuQualification = Get-CpuLoadQualification -State $script:phase7CpuControl

    [ordered]@{
        schemaVersion = 2
        scenario = $Scenario
        durationSeconds = $DurationSeconds
        warmupSeconds = $WarmupSeconds
        boundedDiskMiB = if ($usesDisk) { $DiskMiB } else { 0 }
        requestedCpuPercent = $cpuTargetPercent
        controlledCpuBand = [ordered]@{
            lowerPercent = $controlLowerPercent
            upperPercent = $controlUpperPercent
            midpointPercent = $controlMidpointPercent
            controlIntervalMs = $CpuControlIntervalMs
        }
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
        initialWorkerDutyPercent = $initialWorkerDutyPercent
        logicalProcessors = $logicalProcessors
        workloadExecutable = (Resolve-Path -LiteralPath $WorkloadExecutable).Path
        workloadArguments = $WorkloadArguments
        probeReport = $probeReport
        before = $before
        underLoad = $underLoad
        duringWorkload = $duringWorkload
        cpuControlSamples = @($script:phase7CpuControl.samples)
        cpuLoadQualification = $cpuQualification
        observedCpuPercent = if ($cpuQualification.sampleCount -gt 0) {
            [ordered]@{
                minimum = $cpuQualification.minimumPercent
                average = $cpuQualification.averagePercent
                maximum = $cpuQualification.maximumPercent
            }
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
    if ($cpuDutyChannel) {
        $cpuDutyChannel.Accessor.Dispose()
        $cpuDutyChannel.Map.Dispose()
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
