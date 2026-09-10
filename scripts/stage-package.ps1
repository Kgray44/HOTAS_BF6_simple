[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $StageDir,
    [Parameter(Mandatory)] [string] $WindeployQt
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'HOTAS_VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'HOTAS_VERSION must be major.minor.patch.' }

$build = (Resolve-Path -LiteralPath $BuildDir).Path
$deploy = (Resolve-Path -LiteralPath $WindeployQt).Path
$mapper = Join-Path $build 'HOTAS BF6.exe'
$launcher = Join-Path $build 'HOTAS BF6 Launcher.exe'
if (-not (Test-Path -LiteralPath $mapper -PathType Leaf)) { throw "Missing mapper: $mapper" }
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Missing launcher: $launcher" }

function Find-MsvcRuntimeDirectory {
    # windeployqt --compiler-runtime only works when its caller inherited a
    # Visual Studio developer environment.  Release staging must be equally
    # complete from a clean PowerShell/CI shell, so locate the installed x64
    # redistributable explicitly when windeployqt leaves it behind.
    $roots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
        $roots += $env:VCToolsRedistDir
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installation = & $vswhere -latest -products * -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installation)) {
            $roots += Join-Path $installation.Trim() 'VC\Redist\MSVC'
        }
    }

    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $versions = if ((Split-Path -Leaf $root) -eq 'MSVC') {
            Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name -Descending
        } else {
            @((Get-Item -LiteralPath $root))
        }
        foreach ($version in $versions) {
            $x64 = Join-Path $version.FullName 'x64'
            if (-not (Test-Path -LiteralPath $x64 -PathType Container)) { continue }
            $runtime = Get-ChildItem -LiteralPath $x64 -Directory -Filter 'Microsoft.VC*.CRT' |
                Select-Object -First 1
            if ($runtime) { return $runtime.FullName }
        }
    }
    return $null
}

$stage = [System.IO.Path]::GetFullPath($StageDir)
if ($stage -eq [System.IO.Path]::GetPathRoot($stage)) { throw 'Refusing to use a filesystem root as StageDir.' }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $mapper -Destination (Join-Path $stage 'HOTAS BF6.exe')
Copy-Item -LiteralPath $launcher -Destination (Join-Path $stage 'HOTAS BF6 Launcher.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'HOTAS_VERSION') -Destination (Join-Path $stage 'VERSION')

& $deploy --release --compiler-runtime --qmldir (Join-Path $repoRoot 'qml') (Join-Path $stage 'HOTAS BF6.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }

# CI exercises the installed mapper with Qt's offscreen platform. Deploy that
# platform explicitly so the acceptance result cannot be satisfied by the Qt
# installation on the build runner.
$qtPrefix = Split-Path -Parent (Split-Path -Parent $deploy)
$offscreenPlatform = Join-Path $qtPrefix 'plugins\platforms\qoffscreen.dll'
if (-not (Test-Path -LiteralPath $offscreenPlatform -PathType Leaf)) {
    throw "The configured Qt runtime does not provide qoffscreen.dll: $offscreenPlatform"
}
Copy-Item -LiteralPath $offscreenPlatform -Destination (Join-Path $stage 'platforms\qoffscreen.dll') -Force

$runtimeFiles = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
$missingRuntime = @($runtimeFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $stage $_) -PathType Leaf) })
if ($missingRuntime.Count -gt 0) {
    $runtimeDirectory = Find-MsvcRuntimeDirectory
    if ([string]::IsNullOrWhiteSpace($runtimeDirectory)) {
        throw "windeployqt did not stage the MSVC runtime ($($missingRuntime -join ', ')), and no Visual Studio x64 redistributable directory could be found."
    }
    foreach ($file in $missingRuntime) {
        $source = Join-Path $runtimeDirectory $file
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "The selected MSVC redistributable is missing $($file): $runtimeDirectory"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $stage $file)
    }
}

$required = @(
    'HOTAS BF6.exe',
    'HOTAS BF6 Launcher.exe',
    'VERSION',
    'Qt6Core.dll',
    'Qt6Widgets.dll',
    'Qt6Quick.dll',
    'msvcp140.dll',
    'vcruntime140.dll',
    'vcruntime140_1.dll',
    'platforms\qwindows.dll',
    'platforms\qoffscreen.dll',
    'qml'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $relative))) {
        throw "Qt deployment validation failed: $relative is missing."
    }
}

Write-Host "Staged HOTAS BF6 $version at $stage"
