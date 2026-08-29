[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $FixtureTool,
    [Parameter(Mandatory)] [string] $ExpectedVersion,
    [string] $LegacyInstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v1.9.3/HOTAS-BF6-Setup-v1.9.3.exe'
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'Published updater acceptance is intentionally restricted to an isolated GitHub Actions Windows runner.'
}

$fixture = (Resolve-Path -LiteralPath $FixtureTool).Path
$runnerTemp = [System.IO.Path]::GetFullPath($env:RUNNER_TEMP)
$work = Join-Path $runnerTemp "hotas-published-updater-$PID"
if (-not ([System.IO.Path]::GetFullPath($work).StartsWith($runnerTemp, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw 'Refusing to create updater acceptance artifacts outside RUNNER_TEMP.'
}

function Invoke-Installer([string] $installer, [string] $target) {
    $result = Start-Process -FilePath $installer -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$target`"") -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Legacy installer failed with exit code $($result.ExitCode)." }
}

function Get-InstalledVersion([string] $versionFile) {
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { return '' }
    return (Get-Content -LiteralPath $versionFile -Raw).Trim()
}

function Get-LatestPublishedVersion {
    $manifest = Invoke-RestMethod `
        -Uri 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json' `
        -Headers @{ 'Cache-Control' = 'no-cache' } -TimeoutSec 15
    return [string] $manifest.version
}

function Stop-TargetMapper([string] $mapperPath) {
    $running = Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $mapperPath }
    foreach ($process in $running) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
            [void] $process.WaitForExit(5000)
        }
    }
}

try {
    New-Item -ItemType Directory -Path $work | Out-Null
    $legacyInstaller = Join-Path $work 'HOTAS-BF6-Setup-v1.9.3.exe'
    $install = Join-Path $work 'installed'
    Invoke-WebRequest -Uri $LegacyInstallerUrl -OutFile $legacyInstaller
    & $fixture --clear
    if ($LASTEXITCODE -ne 0) { throw 'Could not clear the isolated updater acceptance configuration.' }
    Invoke-Installer $legacyInstaller $install
    & $fixture --seed-v14
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed v1.9.3 updater acceptance data.' }

    # The legacy launcher consumes this exact public endpoint. Wait for the
    # newly published manifest before launching it, so a CDN edge returning
    # the previous release cannot create a false negative immediately after
    # publication.
    $manifestDeadline = [DateTime]::UtcNow.AddMinutes(2)
    $publishedVersion = ''
    while ([DateTime]::UtcNow -lt $manifestDeadline) {
        try { $publishedVersion = Get-LatestPublishedVersion } catch { $publishedVersion = '' }
        if ($publishedVersion -eq $ExpectedVersion) { break }
        Start-Sleep -Seconds 2
    }
    if ($publishedVersion -ne $ExpectedVersion) {
        throw "The public latest manifest did not converge to $ExpectedVersion; last observed version: $publishedVersion."
    }

    # This uses the unmodified v1.9.3 launcher and the public latest manifest:
    # manifest fetch, download, SHA verification, helper, silent installer,
    # updated launcher, and mapper startup all execute as shipped.
    $launcher = Join-Path $install 'HOTAS BF6 Launcher.exe'
    [void](Start-Process -FilePath $launcher -PassThru)
    $versionFile = Join-Path $install 'VERSION'
    $mapperPath = (Join-Path $install 'HOTAS BF6.exe')
    $deadline = [DateTime]::UtcNow.AddMinutes(8)
    $nextRetry = [DateTime]::UtcNow.AddSeconds(60)
    $retryCount = 0
    $lastVersion = Get-InstalledVersion $versionFile
    $mapper = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $lastVersion = Get-InstalledVersion $versionFile
        $versionMatches = $lastVersion -eq $ExpectedVersion
        if ($versionMatches) {
            $mapper = Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $mapperPath } | Select-Object -First 1
            if ($mapper) { break }
        }

        # If an immediately previous stable installer was served by a stale
        # edge, the original v1.9.3 launcher has already completed safely.
        # Relaunch that task-owned updated launcher only after this explicit
        # version observation; it retries the normal public update chain
        # without changing the shipped legacy launcher or touching any host
        # process outside the isolated installation.
        if ($lastVersion -and $lastVersion -ne '1.9.3' -and -not $versionMatches `
                -and [DateTime]::UtcNow -ge $nextRetry) {
            Stop-TargetMapper $mapperPath
            [void](Start-Process -FilePath $launcher -PassThru)
            $retryCount++
            $nextRetry = [DateTime]::UtcNow.AddSeconds(30)
        }
        Start-Sleep -Seconds 2
    }
    if (-not $mapper -or $mapper.HasExited) {
        $updaterLog = Join-Path $env:LOCALAPPDATA 'HOTAS BF6\logs\updater.log'
        $updaterDiagnostic = if (Test-Path -LiteralPath $updaterLog -PathType Leaf) {
            (Get-Content -LiteralPath $updaterLog -Tail 20) -join ' | '
        } else { 'updater log was not created' }
        throw "The published updater did not leave the updated mapper running. Last installed version: $lastVersion. Retry count: $retryCount. Updater log: $updaterDiagnostic"
    }
    Start-Sleep -Seconds 3
    if ($mapper.HasExited) { throw 'The updated mapper exited during its initial stability interval.' }

    & $fixture --assert-v15
    if ($LASTEXITCODE -ne 0) { throw 'The published updater did not preserve and migrate the v1.9.3 fixture.' }

    $updaterLog = Join-Path $env:LOCALAPPDATA 'HOTAS BF6\logs\updater.log'
    if (-not (Test-Path -LiteralPath $updaterLog -PathType Leaf)) { throw 'Updater log was not created.' }
    $log = Get-Content -LiteralPath $updaterLog -Raw
    foreach ($event in @(
        'new stable release available:',
        'update installer download started',
        'update installer SHA-256 verified',
        'verified update handed to temporary helper',
        'installer completed successfully; starting updated launcher',
        'mapper remained alive after initial startup'
    )) {
        if ($log -notmatch [regex]::Escape($event)) { throw "Updater log is missing required event: $event" }
    }
    Write-Host "Published updater acceptance passed: v1.9.3 -> v$ExpectedVersion."
} finally {
    Stop-TargetMapper (Join-Path $work 'installed\HOTAS BF6.exe')
    & $fixture --clear 2>$null
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
