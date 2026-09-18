[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StageDir,
    [ValidateRange(1, 20)] [int] $Samples = 5,
    [ValidateSet('idle', 'moderate', 'heavy')] [string[]] $Contention = @('idle', 'moderate', 'heavy'),
    [ValidateRange(1, 120)] [int] $IdleObservationSeconds = 30,
    [ValidateRange(10, 180)] [int] $ProcessTimeoutSeconds = 90,
    [int] $InteractiveProcessId = 0,
    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'

$stage = (Resolve-Path -LiteralPath $StageDir).Path
$doctor = Join-Path $stage 'HidHide Doctor.exe'
if (-not (Test-Path -LiteralPath $doctor -PathType Leaf)) {
    throw "Stage does not contain HidHide Doctor.exe: $stage"
}

$resultFile = if ($OutputPath) {
    [System.IO.Path]::GetFullPath($OutputPath)
} else {
    Join-Path (Join-Path ([System.IO.Path]::GetTempPath()) ("hidhide-doctor-phase5-measurement-" + [guid]::NewGuid().ToString('N'))) 'measurements.json'
}
$resultsRoot = [System.IO.Path]::GetDirectoryName($resultFile)
if ([string]::IsNullOrWhiteSpace($resultsRoot)) {
    throw "Output path does not include a directory: $resultFile"
}
if (-not (Test-Path -LiteralPath $resultsRoot)) {
    [void](New-Item -ItemType Directory -Path $resultsRoot -Force)
}

function Get-ProcessSnapshot {
    param([System.Diagnostics.Process] $Process)
    try {
        $Process.Refresh()
        return [pscustomobject]@{
            workingSetBytes = $Process.WorkingSet64
            privateMemoryBytes = $Process.PrivateMemorySize64
            cpuSeconds = $Process.TotalProcessorTime.TotalSeconds
        }
    } catch {
        return $null
    }
}

function Invoke-DoctorProcess {
    param([string[]] $Arguments, [string] $Kind, [string] $ContentionLevel, [int] $Sample)

    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $doctor -ArgumentList $Arguments -PassThru
    $peakWorkingSet = 0L
    $peakPrivateMemory = 0L
    $lastSnapshot = $null
    while (-not $process.HasExited) {
        if ($watch.Elapsed.TotalSeconds -ge $ProcessTimeoutSeconds) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
            $watch.Stop()
            return [pscustomobject]@{
                kind = $Kind
                contention = $ContentionLevel
                sample = $Sample
                arguments = $Arguments
                exitCode = $null
                timedOut = $true
                elapsedMilliseconds = [Math]::Round($watch.Elapsed.TotalMilliseconds, 3)
                peakWorkingSetBytes = $peakWorkingSet
                peakPrivateMemoryBytes = $peakPrivateMemory
                finalObservedCpuSeconds = if ($lastSnapshot) { $lastSnapshot.cpuSeconds } else { $null }
            }
        }
        $snapshot = Get-ProcessSnapshot -Process $process
        if ($snapshot) {
            $peakWorkingSet = [Math]::Max($peakWorkingSet, $snapshot.workingSetBytes)
            $peakPrivateMemory = [Math]::Max($peakPrivateMemory, $snapshot.privateMemoryBytes)
            $lastSnapshot = $snapshot
        }
        Start-Sleep -Milliseconds 50
    }
    $process.WaitForExit()
    $watch.Stop()
    [pscustomobject]@{
        kind = $Kind
        contention = $ContentionLevel
        sample = $Sample
        arguments = $Arguments
        exitCode = $process.ExitCode
        timedOut = $false
        elapsedMilliseconds = [Math]::Round($watch.Elapsed.TotalMilliseconds, 3)
        peakWorkingSetBytes = $peakWorkingSet
        peakPrivateMemoryBytes = $peakPrivateMemory
        finalObservedCpuSeconds = if ($lastSnapshot) { $lastSnapshot.cpuSeconds } else { $null }
    }
}

Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Threading;

public sealed class HidHideDoctorPhase5CpuLoad : IDisposable
{
    private readonly Thread[] workers;
    private readonly int activeMilliseconds;
    private volatile bool stop;

    public HidHideDoctorPhase5CpuLoad(int workerCount, int activeMilliseconds)
    {
        this.activeMilliseconds = activeMilliseconds;
        workers = new Thread[workerCount];
        for (int index = 0; index < workers.Length; ++index)
        {
            workers[index] = new Thread(Work) { IsBackground = true, Name = "HidHideDoctorPhase5CpuLoad" };
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

function Start-ContentionLoad {
    param([string] $Level)
    if ($Level -eq 'idle') { return $null }

    $logicalProcessors = [Math]::Max(1, [int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)
    $workers = if ($Level -eq 'moderate') { [Math]::Max(1, [Math]::Floor($logicalProcessors / 2)) } else { [Math]::Max(1, $logicalProcessors - 1) }
    $activeMilliseconds = if ($Level -eq 'moderate') { 50 } else { 95 }
    $load = [HidHideDoctorPhase5CpuLoad]::new($workers, $activeMilliseconds)
    Start-Sleep -Seconds 2
    return $load
}

function Stop-ContentionLoad {
    param([object] $Load)
    if ($null -ne $Load) { $Load.Dispose() }
}

function Measure-InteractiveIdle {
    param([int] $ProcessId, [int] $Seconds, [int] $LogicalProcessors)
    if ($ProcessId -le 0) { return $null }
    $process = Get-Process -Id $ProcessId -ErrorAction Stop
    $before = Get-ProcessSnapshot -Process $process
    Start-Sleep -Seconds $Seconds
    $after = Get-ProcessSnapshot -Process $process
    if ($null -eq $before -or $null -eq $after) { return $null }
    [pscustomobject]@{
        processId = $ProcessId
        observationSeconds = $Seconds
        cpuPercentOfAllLogicalProcessors = [Math]::Round((($after.cpuSeconds - $before.cpuSeconds) / ($Seconds * $LogicalProcessors)) * 100.0, 3)
        workingSetBytes = $after.workingSetBytes
        privateMemoryBytes = $after.privateMemoryBytes
    }
}

$logicalProcessorCount = [Math]::Max(1, [int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors)
$startup = @()
for ($sample = 1; $sample -le $Samples; ++$sample) {
    $label = if ($sample -eq 1) { 'first-process-after-harness-start' } else { 'subsequent-process' }
    $startup += Invoke-DoctorProcess -Arguments @('--startup-smoke') -Kind $label -ContentionLevel 'idle' -Sample $sample
}

$headlessReports = @()
for ($sample = 1; $sample -le $Samples; ++$sample) {
    $reportPath = Join-Path $resultsRoot ("headless-report-$sample.json")
    $headlessReports += Invoke-DoctorProcess -Arguments @('--headless', '--report', $reportPath) -Kind 'headless-report-serialization' -ContentionLevel 'idle' -Sample $sample
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or (Get-Item -LiteralPath $reportPath).Length -le 0) {
        throw "Headless report sample $sample did not create a nonempty report."
    }
}

$scans = @()
foreach ($level in $Contention) {
    $load = $null
    try {
        $load = Start-ContentionLoad -Level $level
        for ($sample = 1; $sample -le $Samples; ++$sample) {
            $scans += Invoke-DoctorProcess -Arguments @('--scan-smoke') -Kind 'read-only-scan-smoke' -ContentionLevel $level -Sample $sample
        }
    } finally {
        Stop-ContentionLoad -Load $load
    }
}

$measurement = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    stageDir = $stage
    doctorExe = $doctor
    logicalProcessorCount = $logicalProcessorCount
    samplesPerSeries = $Samples
    interactiveIdle = Measure-InteractiveIdle -ProcessId $InteractiveProcessId -Seconds $IdleObservationSeconds -LogicalProcessors $logicalProcessorCount
    startup = $startup
    headlessReportSerialization = $headlessReports
    scanSmoke = $scans
    limitations = @(
        'Startup labels distinguish the first and subsequent harness processes; they do not flush the Windows file cache.',
        'Scan-smoke runs the real QML/read-only scan and records process elapsed time, not first visible progress or owner-perceived interaction latency.',
        'Headless report serialization is not interactive report composition or a native export-dialog measurement.',
        'CPU load levels are bounded synthetic process load; they do not replace owner interaction, physical input, or a hardware diversity matrix.'
    )
}

$measurement | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultFile -Encoding utf8
foreach ($processResult in @($startup) + @($headlessReports) + @($scans)) {
    if ($processResult.timedOut -or $processResult.exitCode -ne 0) {
        throw "Doctor measurement failed ($($processResult.kind), $($processResult.contention), sample $($processResult.sample)); results were saved to $resultFile."
    }
}
Write-Output $resultFile
