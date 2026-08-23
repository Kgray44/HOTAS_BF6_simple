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

$stage = [System.IO.Path]::GetFullPath($StageDir)
if ($stage -eq [System.IO.Path]::GetPathRoot($stage)) { throw 'Refusing to use a filesystem root as StageDir.' }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $mapper -Destination (Join-Path $stage 'HOTAS BF6.exe')
Copy-Item -LiteralPath $launcher -Destination (Join-Path $stage 'HOTAS BF6 Launcher.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'HOTAS_VERSION') -Destination (Join-Path $stage 'VERSION')

& $deploy --release --compiler-runtime --qmldir (Join-Path $repoRoot 'qml') (Join-Path $stage 'HOTAS BF6.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }

$required = @(
    'HOTAS BF6.exe',
    'HOTAS BF6 Launcher.exe',
    'VERSION',
    'Qt6Core.dll',
    'Qt6Quick.dll',
    'platforms\qwindows.dll',
    'qml'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $relative))) {
        throw "Qt deployment validation failed: $relative is missing."
    }
}

Write-Host "Staged HOTAS BF6 $version at $stage"
