[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $CandidateInstaller,
    [Parameter(Mandatory)] [string] $FixtureTool,
    [Parameter(Mandatory)] [string] $ExpectedVersion,
    [string] $LegacyInstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v1.9.3/HOTAS-BF6-Setup-v1.9.3.exe',
    [string] $FailedV200InstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v2.0.0/HOTAS-BF6-Setup-v2.0.0.exe'
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'Installer upgrade acceptance is intentionally restricted to an isolated GitHub Actions Windows runner.'
}

$candidate = (Resolve-Path -LiteralPath $CandidateInstaller).Path
$fixture = (Resolve-Path -LiteralPath $FixtureTool).Path
$runnerTemp = [System.IO.Path]::GetFullPath($env:RUNNER_TEMP)
$work = Join-Path $runnerTemp "hotas-installer-upgrade-$PID"
if (-not ([System.IO.Path]::GetFullPath($work).StartsWith($runnerTemp, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw 'Refusing to create acceptance artifacts outside RUNNER_TEMP.'
}

function Invoke-Installer([string] $installer, [string] $target) {
    $result = Start-Process -FilePath $installer -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$target`"") -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Installer failed with exit code $($result.ExitCode): $installer" }
}

function Assert-InstalledVersion([string] $target) {
    $versionFile = Join-Path $target 'VERSION'
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { throw "Missing installed VERSION: $versionFile" }
    if ((Get-Content -LiteralPath $versionFile -Raw).Trim() -ne $ExpectedVersion) {
        throw "Installed VERSION does not match $ExpectedVersion."
    }
}

function Invoke-MapperStartupSmoke([string] $target) {
    $mapper = Join-Path $target 'HOTAS BF6.exe'
    $priorPlatform = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $result = Start-Process -FilePath $mapper -ArgumentList '--startup-smoke' -Wait -PassThru
        if ($result.ExitCode -ne 0) { throw "Packaged mapper startup smoke failed with exit code $($result.ExitCode)." }
    } finally {
        $env:QT_QPA_PLATFORM = $priorPlatform
    }
}

function Assert-LegacyMapperStarts([string] $target) {
    $mapper = Join-Path $target 'HOTAS BF6.exe'
    $process = Start-Process -FilePath $mapper -PassThru
    try {
        Start-Sleep -Seconds 3
        if ($process.HasExited) { throw "v1.9.3 mapper exited during startup with code $($process.ExitCode)." }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -ErrorAction Stop
            $process.WaitForExit()
        }
    }
}

try {
    New-Item -ItemType Directory -Path $work | Out-Null
    $legacyInstaller = Join-Path $work 'HOTAS-BF6-Setup-v1.9.3.exe'
    $failedV200Installer = Join-Path $work 'HOTAS-BF6-Setup-v2.0.0.exe'
    Invoke-WebRequest -Uri $LegacyInstallerUrl -OutFile $legacyInstaller
    Invoke-WebRequest -Uri $FailedV200InstallerUrl -OutFile $failedV200Installer

    & $fixture --clear
    if ($LASTEXITCODE -ne 0) { throw 'Could not clear the isolated acceptance configuration.' }

    # A clean v2.0.1 install must create its QML root and remain alive through
    # initialization; an installer success code by itself is not acceptance.
    $cleanInstall = Join-Path $work 'clean-install'
    Invoke-Installer $candidate $cleanInstall
    Assert-InstalledVersion $cleanInstall
    Invoke-MapperStartupSmoke $cleanInstall

    # This is a real v1.9.3 installation with a populated schema-14 profile,
    # controls, curve, POV, Automation, and application settings record.
    $upgradeInstall = Join-Path $work 'v193-upgrade'
    Invoke-Installer $legacyInstaller $upgradeInstall
    & $fixture --seed-v14
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed v1.9.3 schema-14 acceptance data.' }
    Assert-LegacyMapperStarts $upgradeInstall
    Invoke-Installer $candidate $upgradeInstall
    Assert-InstalledVersion $upgradeInstall
    Invoke-MapperStartupSmoke $upgradeInstall
    & $fixture --assert-v18
    if ($LASTEXITCODE -ne 0) { throw 'v1.9.3 upgrade did not preserve and migrate the acceptance fixture.' }

    # A v2.0.0 tray crash can leave the exact same program location with a
    # schema-15 record already persisted. Install that released binary, seed
    # the affected state, then prove v2.0.1 recovers it without AppData reset.
    $recoveryInstall = Join-Path $work 'v200-recovery'
    Invoke-Installer $failedV200Installer $recoveryInstall
    & $fixture --seed-v15
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed the affected schema-15 acceptance data.' }
    Invoke-Installer $candidate $recoveryInstall
    Assert-InstalledVersion $recoveryInstall
    Invoke-MapperStartupSmoke $recoveryInstall
    & $fixture --assert-v18
    if ($LASTEXITCODE -ne 0) { throw 'v2.0.1 did not preserve the affected schema-15 acceptance fixture.' }

    Write-Host "Installer acceptance passed: clean install, v1.9.3 -> v$ExpectedVersion, and v2.0.0 recovery."
} finally {
    & $fixture --clear 2>$null
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
