[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $CandidateInstaller,
    [Parameter(Mandatory)] [string] $FixtureTool,
    [Parameter(Mandatory)] [string] $ExpectedVersion,
    [string] $LegacyInstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v1.9.3/HOTAS-BF6-Setup-v1.9.3.exe',
    [string] $FailedV200InstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v2.0.0/HOTAS-BF6-Setup-v2.0.0.exe',
    [string] $PriorStableInstallerUrl = 'https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/v2.5.0/HOTAS-BF6-Setup-v2.5.0.exe'
)

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'Installer upgrade acceptance is intentionally restricted to an isolated GitHub Actions Windows runner.'
}

$candidate = (Resolve-Path -LiteralPath $CandidateInstaller).Path
$fixture = (Resolve-Path -LiteralPath $FixtureTool).Path
$runnerTemp = [System.IO.Path]::GetFullPath($env:RUNNER_TEMP)
$work = Join-Path $runnerTemp "hotas-installer-upgrade-$PID"
$defaultInstall = $null
if (-not ([System.IO.Path]::GetFullPath($work).StartsWith($runnerTemp, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw 'Refusing to create acceptance artifacts outside RUNNER_TEMP.'
}

function Invoke-Installer([string] $installer, [string] $target) {
    $result = Start-Process -FilePath $installer -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=`"$target`"") -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Installer failed with exit code $($result.ExitCode): $installer" }
}

function Assert-InstalledPackage([string] $target, [string] $expectedInstalledVersion, [switch] $AllowMissingLauncher) {
    $requiredFiles = @(
        'HOTAS BF6.exe', 'VERSION', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll',
        'Qt6QuickControls2.dll'
    )
    if (-not $AllowMissingLauncher) {
        $requiredFiles = @('HOTAS BF6 Launcher.exe') + $requiredFiles
    }
    foreach ($file in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $file) -PathType Leaf)) {
            throw "Installed package is missing ${file}: $target"
        }
    }
    if ($AllowMissingLauncher) {
        $priorLauncher = Join-Path $target 'HOTAS BF6 Launcher.exe'
        if (Test-Path -LiteralPath $priorLauncher -PathType Leaf) {
            Write-Host 'N-1 package contains the launcher before upgrade.'
        } else {
            Write-Host 'N-1 package is missing its launcher before upgrade; the candidate must restore it.'
        }
    }
    $qml = Join-Path $target 'qml'
    if (-not (Test-Path -LiteralPath $qml -PathType Container) -or
        -not (Get-ChildItem -LiteralPath $qml -Force | Select-Object -First 1)) {
        throw 'Installed package is missing the required QML runtime.'
    }
    $versionFile = Join-Path $target 'VERSION'
    if ((Get-Content -LiteralPath $versionFile -Raw).Trim() -ne $expectedInstalledVersion) {
        throw "Installed VERSION does not match $expectedInstalledVersion."
    }
}

function Assert-ShortcutTarget([string] $shortcut, [string] $expectedTarget) {
    if (-not (Test-Path -LiteralPath $shortcut -PathType Leaf)) { throw "Missing shortcut: $shortcut" }
    $shell = New-Object -ComObject WScript.Shell
    $target = $shell.CreateShortcut($shortcut).TargetPath
    if ($target -ne $expectedTarget -or -not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Shortcut target is not runnable: $shortcut -> $target"
    }
}

function Invoke-MapperStartupSmoke([string] $target) {
    $mapper = Join-Path $target 'HOTAS BF6.exe'
    $environmentNames = @('QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')
    $priorEnvironment = @{}
    foreach ($name in $environmentNames) {
        $priorEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_PLUGIN_PATH = $null
        $env:QML2_IMPORT_PATH = $null
        $env:QML_IMPORT_PATH = $null
        $result = Start-Process -FilePath $mapper -ArgumentList '--startup-smoke' -Wait -PassThru
        if ($result.ExitCode -ne 0) { throw "Packaged mapper startup smoke failed with exit code $($result.ExitCode)." }
    } finally {
        foreach ($name in $environmentNames) {
            [Environment]::SetEnvironmentVariable($name, $priorEnvironment[$name], 'Process')
        }
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
    $priorStableInstaller = Join-Path $work 'HOTAS-BF6-Setup-v2.5.0.exe'
    Invoke-WebRequest -Uri $LegacyInstallerUrl -OutFile $legacyInstaller
    Invoke-WebRequest -Uri $FailedV200InstallerUrl -OutFile $failedV200Installer
    Invoke-WebRequest -Uri $PriorStableInstallerUrl -OutFile $priorStableInstaller

    & $fixture --clear
    if ($LASTEXITCODE -ne 0) { throw 'Could not clear the isolated acceptance configuration.' }

    # A clean candidate install must create every launch-critical package file
    # and remain alive through initialization; an installer success code by
    # itself is not acceptance.
    Write-Host 'Installer acceptance: clean candidate install.'
    $cleanInstall = Join-Path $work 'clean-install'
    Invoke-Installer $candidate $cleanInstall
    Assert-InstalledPackage $cleanInstall $ExpectedVersion
    Invoke-MapperStartupSmoke $cleanInstall
    & $fixture --assert-fresh-v23
    if ($LASTEXITCODE -ne 0) { throw 'Clean installation did not create the Battlefield 6 / Helicopter starter.' }

    # This is a real v1.9.3 installation with a populated schema-14 profile,
    # controls, curve, POV, Automation, and application settings record.
    Write-Host 'Installer acceptance: v1.9.3 migration.'
    $upgradeInstall = Join-Path $work 'v193-upgrade'
    Invoke-Installer $legacyInstaller $upgradeInstall
    & $fixture --seed-v14
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed v1.9.3 schema-14 acceptance data.' }
    Assert-LegacyMapperStarts $upgradeInstall
    Invoke-Installer $candidate $upgradeInstall
    Assert-InstalledPackage $upgradeInstall $ExpectedVersion
    Invoke-MapperStartupSmoke $upgradeInstall
    & $fixture --assert-v23
    if ($LASTEXITCODE -ne 0) { throw 'v1.9.3 upgrade did not preserve and migrate the acceptance fixture.' }

    # A v2.0.0 tray crash can leave the exact same program location with a
    # schema-15 record already persisted. Install that released binary, seed
    # the affected state, then prove v2.0.1 recovers it without AppData reset.
    Write-Host 'Installer acceptance: v2.0.0 recovery.'
    $recoveryInstall = Join-Path $work 'v200-recovery'
    Invoke-Installer $failedV200Installer $recoveryInstall
    & $fixture --seed-v15
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed the affected schema-15 acceptance data.' }
    Invoke-Installer $candidate $recoveryInstall
    Assert-InstalledPackage $recoveryInstall $ExpectedVersion
    Invoke-MapperStartupSmoke $recoveryInstall
    & $fixture --assert-v23
    if ($LASTEXITCODE -ne 0) { throw 'v2.0.1 did not preserve the affected schema-15 acceptance fixture.' }

    # Release-quality N-1 coverage: use the actual public v2.5.0 package and
    # upgrade it to the candidate in place. The public updater could leave a
    # partial v2.5.0 directory without its launcher, so accept that historical
    # input while requiring the candidate upgrade below to restore a complete
    # runnable package and preserve the established user configuration fixture.
    Write-Host 'Installer acceptance: v2.5.0 N-1 upgrade.'
    $priorStableInstall = Join-Path $work 'v250-upgrade'
    Invoke-Installer $priorStableInstaller $priorStableInstall
    Assert-InstalledPackage $priorStableInstall '2.5.0' -AllowMissingLauncher
    & $fixture --seed-v15
    if ($LASTEXITCODE -ne 0) { throw 'Could not seed the v2.5.0 upgrade fixture.' }
    Invoke-Installer $candidate $priorStableInstall
    Assert-InstalledPackage $priorStableInstall $ExpectedVersion
    Invoke-MapperStartupSmoke $priorStableInstall
    & $fixture --assert-v23
    if ($LASTEXITCODE -ne 0) { throw 'v2.5.0 -> candidate did not preserve the acceptance fixture.' }

    # The default local-app-data installation path and both shortcuts are
    # checked only on the ephemeral GitHub runner. No redirected /DIR is used
    # for this acceptance case.
    Write-Host 'Installer acceptance: default path and shortcuts.'
    $defaultInstall = Join-Path $env:LOCALAPPDATA 'Programs\HOTAS BF6'
    if (Test-Path -LiteralPath $defaultInstall) { throw "Default acceptance path already exists: $defaultInstall" }
    $defaultResult = Start-Process -FilePath $candidate -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/TASKS="desktopicon"') -Wait -PassThru
    if ($defaultResult.ExitCode -ne 0) { throw "Default-path installer failed with exit code $($defaultResult.ExitCode)." }
    Assert-InstalledPackage $defaultInstall $ExpectedVersion
    Invoke-MapperStartupSmoke $defaultInstall
    $launcher = Join-Path $defaultInstall 'HOTAS BF6 Launcher.exe'
    Assert-ShortcutTarget (Join-Path ([Environment]::GetFolderPath('Programs')) 'HOTAS BF6\HOTAS BF6.lnk') $launcher
    Assert-ShortcutTarget (Join-Path ([Environment]::GetFolderPath('Desktop')) 'HOTAS BF6.lnk') $launcher

    Write-Host "Installer acceptance passed: clean install, v1.9.3 migration, v2.0.0 recovery, v2.5.0 -> v$ExpectedVersion, and default-path shortcuts."
} finally {
    if ($defaultInstall -and (Test-Path -LiteralPath $defaultInstall -PathType Container)) {
        $uninstaller = Join-Path $defaultInstall 'unins000.exe'
        if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
            [void](Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru)
        }
    }
    & $fixture --clear 2>$null
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
