[CmdletBinding()]
param(
    [ValidateSet('idle', 'moderate', 'heavy')]
    [string] $Level = 'moderate',
    [ValidateRange(15, 180)]
    [int] $DurationSeconds = 90,
    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'

if ($null -eq ('HidHideDoctorInteractiveContention' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Threading;

public sealed class HidHideDoctorInteractiveContention : IDisposable
{
    private readonly Thread[] workers;
    private readonly int activeMilliseconds;
    private volatile bool stop;

    public HidHideDoctorInteractiveContention(int workerCount, int activeMilliseconds)
    {
        this.activeMilliseconds = activeMilliseconds;
        workers = new Thread[workerCount];
        for (int index = 0; index < workers.Length; ++index)
        {
            workers[index] = new Thread(Work) {
                IsBackground = true,
                Name = "HidHideDoctorInteractiveContention"
            };
            workers[index].Start();
        }
    }

    private void Work()
    {
        double value = 1.0;
        while (!stop)
        {
            Stopwatch period = Stopwatch.StartNew();
            while (!stop && period.ElapsedMilliseconds < activeMilliseconds)
                value = Math.Sqrt(value + 1.0);

            int remainingMilliseconds = 100 - (int)period.ElapsedMilliseconds;
            if (!stop && remainingMilliseconds > 0)
                Thread.Sleep(remainingMilliseconds);
        }
        GC.KeepAlive(value);
    }

    public void Dispose()
    {
        stop = true;
        foreach (Thread worker in workers)
            worker.Join(1000);
    }
}
'@
}

$logicalProcessorCount = [Math]::Max(1, [int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)
$workerCount = switch ($Level) {
    'idle' { 0 }
    'moderate' { [Math]::Max(1, [Math]::Floor($logicalProcessorCount / 2)) }
    'heavy' { [Math]::Max(1, $logicalProcessorCount - 1) }
}
$activeMilliseconds = switch ($Level) {
    'idle' { 0 }
    'moderate' { 50 }
    'heavy' { 95 }
}

$resultFile = if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    Join-Path ([System.IO.Path]::GetTempPath()) ("hidhide-doctor-interactive-contention-" + [guid]::NewGuid().ToString('N') + '.json')
} else {
    [System.IO.Path]::GetFullPath($OutputPath)
}
$resultRoot = [System.IO.Path]::GetDirectoryName($resultFile)
if ([string]::IsNullOrWhiteSpace($resultRoot)) {
    throw 'OutputPath must include a directory.'
}
if (-not (Test-Path -LiteralPath $resultRoot)) {
    [void](New-Item -ItemType Directory -Path $resultRoot -Force)
}

$hostProcess = Get-Process -Id $PID
$cpuBefore = $hostProcess.TotalProcessorTime.TotalSeconds
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$load = $null
try {
    if ($workerCount -gt 0) {
        $load = [HidHideDoctorInteractiveContention]::new($workerCount, $activeMilliseconds)
    }

    Write-Host "HidHide Doctor interactive $Level contention is active for $DurationSeconds seconds."
    Write-Host 'Use only normal read-only Doctor controls during this interval. Press Ctrl+C to stop early.'
    while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        Start-Sleep -Milliseconds 250
    }
} finally {
    if ($null -ne $load) {
        $load.Dispose()
    }
    $watch.Stop()
}

$hostProcess.Refresh()
$cpuAfter = $hostProcess.TotalProcessorTime.TotalSeconds
$averageCpuPercentOfAllLogicalProcessors = [Math]::Round((($cpuAfter - $cpuBefore) / ($watch.Elapsed.TotalSeconds * $logicalProcessorCount)) * 100.0, 3)
$measurement = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    level = $Level
    requestedDurationSeconds = $DurationSeconds
    actualDurationMilliseconds = [Math]::Round($watch.Elapsed.TotalMilliseconds, 3)
    logicalProcessorCount = $logicalProcessorCount
    workerCount = $workerCount
    activeMillisecondsPerHundred = $activeMilliseconds
    loadProcessAverageCpuPercentOfAllLogicalProcessors = $averageCpuPercentOfAllLogicalProcessors
    limitations = @(
        'This records the bounded synthetic CPU-load configuration and the load process CPU time.',
        'It does not measure first visible progress, click latency, render latency, or owner perception; record those separately in the native worksheet.',
        'It launches no Doctor process and performs no repair, elevation, export, configuration change, or device mutation.'
    )
}
$measurement | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultFile -Encoding utf8
Write-Output $resultFile
