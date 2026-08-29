param(
    [Parameter(Mandatory)] [string] $FixtureTool,
    [Parameter(Mandatory)] [string] $ExpectedVersion,
    [string] $LegacyInstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v1.9.3/HOTAS-BF6-Setup-v1.9.3.exe'
)

# This is an evidence collector only. It neither changes product behavior nor
# evaluates the acceptance assertion; it records the exact shipped path.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'Published-updater diagnostics are restricted to an isolated GitHub Actions Windows runner.'
}

$fixture = (Resolve-Path -LiteralPath $FixtureTool).Path
$root = Join-Path ([System.IO.Path]::GetFullPath($env:RUNNER_TEMP)) "hotas-updater-diagnosis-$PID"
$evidence = Join-Path $root 'evidence'
$install = Join-Path $root 'installed'
$stopMonitor = Join-Path $root 'stop-monitor'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null

$trackedNames = @('HOTAS BF6', 'HOTAS BF6 Launcher', 'HOTAS BF6 Update Helper',
    'HOTAS-BF6-Setup-v1.9.3', "HOTAS-BF6-Setup-v$ExpectedVersion")
$mapperPath = Join-Path $install 'HOTAS BF6.exe'
$launcherPath = Join-Path $install 'HOTAS BF6 Launcher.exe'
$startedUtc = [DateTime]::UtcNow

function Write-Evidence([string] $name, [object] $value) {
    $path = Join-Path $evidence $name
    $value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8
}

function Read-Version {
    $versionFile = Join-Path $install 'VERSION'
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { return $null }
    return (Get-Content -LiteralPath $versionFile -Raw).Trim()
}

function Get-ProcessDetails {
    param([string[]] $Names)
    $result = @()
    foreach ($name in $Names) {
        $result += @(Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
            $path = $null
            $started = $null
            $windowHandle = $null
            $windowTitle = $null
            try { $path = $_.Path } catch {}
            try { $started = $_.StartTime.ToUniversalTime().ToString('o') } catch {}
            try { $windowHandle = $_.MainWindowHandle } catch {}
            try { $windowTitle = $_.MainWindowTitle } catch {}
            [pscustomobject]@{
                Id = $_.Id; ProcessName = $_.ProcessName; Path = $path; StartUtc = $started
                MainWindowHandle = $windowHandle; MainWindowTitle = $windowTitle
            }
        })
    }
    return @($result)
}

function Snapshot([string] $name) {
    $harnessLookup = @(Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -eq $mapperPath } catch { $false }
    } | ForEach-Object {
        [pscustomobject]@{ Id = $_.Id; ProcessName = $_.ProcessName; Path = $_.Path; HasExited = $_.HasExited
            MainWindowHandle = $_.MainWindowHandle; MainWindowTitle = $_.MainWindowTitle }
    })
    Write-Evidence "$name.json" ([pscustomobject]@{
        RecordedUtc = [DateTime]::UtcNow.ToString('o'); InstalledVersion = Read-Version
        ExpectedVersion = $ExpectedVersion; InstallDirectory = $install; MapperPath = $mapperPath
        LauncherPath = $launcherPath; HarnessLookup = $harnessLookup; AllTrackedProcesses = Get-ProcessDetails $trackedNames
    })
}

$monitor = Start-Job -ScriptBlock {
    param($outputPath, $stopPath, $names, $maxSeconds)
    $known = @{}
    $events = [System.Collections.Generic.List[object]]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds($maxSeconds)
    while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $stopPath)) {
        foreach ($name in $names) {
            foreach ($process in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
                $key = [string]$process.Id
                if (-not $known.ContainsKey($key)) {
                    $path = $null; $startUtc = $null
                    try { $path = $process.Path } catch {}
                    try { $startUtc = $process.StartTime.ToUniversalTime() } catch {}
                    $known[$key] = [pscustomobject]@{ Process = $process; Name = $process.ProcessName; Path = $path; StartUtc = $startUtc }
                    $events.Add([pscustomobject]@{ Event = 'start-observed'; ObservedUtc = [DateTime]::UtcNow.ToString('o')
                        Id = $process.Id; ProcessName = $process.ProcessName; Path = $path; StartUtc = if ($startUtc) { $startUtc.ToString('o') } else { $null } })
                }
            }
        }
        foreach ($key in @($known.Keys)) {
            $entry = $known[$key]
            try {
                $entry.Process.Refresh()
                if ($entry.Process.HasExited) {
                    $exitUtc = $entry.Process.ExitTime.ToUniversalTime()
                    $events.Add([pscustomobject]@{ Event = 'exit-observed'; ObservedUtc = [DateTime]::UtcNow.ToString('o')
                        Id = $entry.Process.Id; ProcessName = $entry.Name; Path = $entry.Path
                        ExitUtc = $exitUtc.ToString('o'); ExitCode = $entry.Process.ExitCode
                        LifetimeMilliseconds = if ($entry.StartUtc) { [math]::Round(($exitUtc - $entry.StartUtc).TotalMilliseconds, 1) } else { $null } })
                    $known.Remove($key)
                }
            } catch {}
        }
        Start-Sleep -Milliseconds 100
    }
    foreach ($entry in $known.Values) {
        $events.Add([pscustomobject]@{ Event = 'still-running-at-monitor-stop'; ObservedUtc = [DateTime]::UtcNow.ToString('o')
            Id = $entry.Process.Id; ProcessName = $entry.Name; Path = $entry.Path })
    }
    $events | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outputPath -Encoding utf8
} -ArgumentList (Join-Path $evidence 'process-lifetimes.json'), $stopMonitor, $trackedNames, 600

$summary = [ordered]@{
    DiagnosticStartedUtc = $startedUtc.ToString('o'); ExpectedVersion = $ExpectedVersion
    InstallDirectory = $install; MapperPath = $mapperPath; LauncherPath = $launcherPath
    LegacyInstallerExitCode = $null; LegacyLauncherPid = $null; VersionAfterUpdater = $null
    MigrationAssertV15ExitCode = $null; ManualCleanLaunch = $null; Errors = @()
}

try {
    $legacyInstaller = Join-Path $root 'HOTAS-BF6-Setup-v1.9.3.exe'
    Invoke-WebRequest -Uri $LegacyInstallerUrl -OutFile $legacyInstaller
    & $fixture --clear
    if ($LASTEXITCODE -ne 0) { throw "Fixture clear failed with exit code $LASTEXITCODE." }

    $legacy = Start-Process -FilePath $legacyInstaller -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$install`"") -Wait -PassThru
    $summary.LegacyInstallerExitCode = $legacy.ExitCode
    if ($legacy.ExitCode -ne 0) { throw "Legacy installer failed with exit code $($legacy.ExitCode)." }
    & $fixture --seed-v14
    if ($LASTEXITCODE -ne 0) { throw "Fixture seed failed with exit code $LASTEXITCODE." }
    Snapshot '01-before-updater'

    $legacyLauncher = Start-Process -FilePath $launcherPath -PassThru
    $summary.LegacyLauncherPid = $legacyLauncher.Id
    $deadline = [DateTime]::UtcNow.AddMinutes(8)
    $mapperObserved = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        $matching = @(Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue | Where-Object {
            try { $_.Path -eq $mapperPath } catch { $false }
        })
        if ($matching.Count -gt 0) { $mapperObserved = $true; break }
        Start-Sleep -Seconds 2
    }
    $summary.MapperObservedByOriginalLookup = $mapperObserved
    $summary.VersionAfterUpdater = Read-Version
    Snapshot '02-after-updater'

    & $fixture --assert-v15
    $summary.MigrationAssertV15ExitCode = $LASTEXITCODE

    $logDirectory = Join-Path $env:LOCALAPPDATA 'HOTAS BF6\logs'
    if (Test-Path -LiteralPath $logDirectory -PathType Container) {
        Copy-Item -LiteralPath $logDirectory -Destination (Join-Path $evidence 'hotas-logs') -Recurse -Force
    }
    $errors = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $startedUtc.ToLocalTime() } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -in @('Application Error', 'Windows Error Reporting') -or $_.Message -match 'HOTAS BF6|Qt' } |
        Select-Object TimeCreated, Id, ProviderName, LevelDisplayName, Message
    Write-Evidence 'application-events.json' $errors

    # Compare a direct, clean launch of the exact installed v2.0.2 executable.
    foreach ($process in @(Get-Process -Name 'HOTAS BF6','HOTAS BF6 Launcher' -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -in @($mapperPath, $launcherPath) } catch { $false }
    })) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
    & $fixture --clear
    if ($LASTEXITCODE -ne 0) { throw "Fixture clear before manual launch failed with exit code $LASTEXITCODE." }
    if (Test-Path -LiteralPath $mapperPath -PathType Leaf) {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $mapperPath; $psi.WorkingDirectory = $install; $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
        $manual = [System.Diagnostics.Process]::new(); $manual.StartInfo = $psi
        $manualStarted = [DateTime]::UtcNow
        [void]$manual.Start()
        $stdout = $manual.StandardOutput.ReadToEndAsync(); $stderr = $manual.StandardError.ReadToEndAsync()
        $finished = $manual.WaitForExit(10000)
        $manualExitCode = $null
        if (-not $finished) { $manual.Kill(); $manual.WaitForExit() } else { $manualExitCode = $manual.ExitCode }
        $summary.ManualCleanLaunch = [pscustomobject]@{
            Pid = $manual.Id; StartedUtc = $manualStarted.ToString('o'); AliveAt10Seconds = -not $finished
            ExitCodeBeforeDiagnosticCleanup = $manualExitCode; LifetimeMilliseconds = [math]::Round(([DateTime]::UtcNow - $manualStarted).TotalMilliseconds, 1)
            Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult()
        }
    }
    Snapshot '03-after-manual-clean-launch'
} catch {
    $summary.Errors += $_.Exception.ToString()
} finally {
    $summary.DiagnosticFinishedUtc = [DateTime]::UtcNow.ToString('o')
    Write-Evidence 'summary.json' $summary
    New-Item -ItemType File -Path $stopMonitor -Force | Out-Null
    Wait-Job -Job $monitor | Out-Null
    Receive-Job -Job $monitor | Out-Null
    Remove-Job -Job $monitor -Force
    & $fixture --clear 2>$null
}

Write-Host "Diagnostic evidence written to $evidence"
