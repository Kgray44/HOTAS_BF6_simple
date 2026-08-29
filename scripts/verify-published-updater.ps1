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

$latestManifestUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/latest/download/update-manifest.json'
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

function Wait-PublishedLatestManifest([string] $expectedVersion, [string] $manifestUrl) {
    # The shipped launcher fetches this exact public latest endpoint.  A GitHub
    # Release can be visible before its latest alias advances, so do not let the
    # inherited v1.9.3 launcher consume a prior stable manifest during that gap.
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    $lastObservation = 'the latest manifest was not yet available'
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            # Invoke-WebRequest exposes GitHub release assets as bytes on the
            # hosted runner. Invoke-RestMethod decodes and parses this JSON
            # asset consistently while requesting the same shipped endpoint.
            $manifest = Invoke-RestMethod -Uri $manifestUrl
            $version = [string]$manifest.version
            $tag = [string]$manifest.tag
            $lastObservation = "version '$version', tag '$tag'"
            if ($version -eq $expectedVersion -and $tag -eq "v$expectedVersion") {
                Write-Host "Published latest manifest is ready for v$expectedVersion."
                return
            }
        } catch {
            $lastObservation = $_.Exception.Message
        }
        Start-Sleep -Seconds 2
    }
    throw "Published latest update manifest did not advance to v$expectedVersion within five minutes (last observation: $lastObservation)."
}

$priorPlatform = $env:QT_QPA_PLATFORM

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

    Wait-PublishedLatestManifest -expectedVersion $ExpectedVersion -manifestUrl $latestManifestUrl

    # This uses the unmodified v1.9.3 launcher and the public latest manifest:
    # manifest fetch, download, SHA verification, helper, silent installer,
    # updated launcher, and mapper startup all execute as shipped.
    $launcher = Join-Path $install 'HOTAS BF6 Launcher.exe'
    # GitHub-hosted Windows runners have no interactive desktop. The normal
    # package smoke deliberately uses offscreen Qt for the same reason; make
    # the launcher and its post-update mapper inherit that test-only platform.
    $env:QT_QPA_PLATFORM = 'offscreen'
    [void](Start-Process -FilePath $launcher -PassThru)
    $versionFile = Join-Path $install 'VERSION'
    $mapperPath = (Join-Path $install 'HOTAS BF6.exe')
    $deadline = [DateTime]::UtcNow.AddMinutes(8)
    $mapper = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $versionMatches = (Test-Path -LiteralPath $versionFile) -and
            ((Get-Content -LiteralPath $versionFile -Raw).Trim() -eq $ExpectedVersion)
        if ($versionMatches) {
            $mapper = Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $mapperPath } | Select-Object -First 1
            if ($mapper) { break }
        }
        Start-Sleep -Seconds 2
    }
    if (-not $mapper -or $mapper.HasExited) {
        $updaterLog = Join-Path $env:LOCALAPPDATA 'HOTAS BF6\logs\updater.log'
        if (Test-Path -LiteralPath $updaterLog -PathType Leaf) {
            Write-Host 'Updater log after failed startup:'
            Get-Content -LiteralPath $updaterLog -Raw
        }
        throw 'The published updater did not leave the updated mapper running.'
    }
    Start-Sleep -Seconds 3
    if ($mapper.HasExited) { throw 'The updated mapper exited during its initial stability interval.' }

    & $fixture --assert-v16
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
    $env:QT_QPA_PLATFORM = $priorPlatform
    $running = Get-Process -Name 'HOTAS BF6' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq (Join-Path $work 'installed\HOTAS BF6.exe') }
    foreach ($process in $running) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
    }
    & $fixture --clear 2>$null
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
