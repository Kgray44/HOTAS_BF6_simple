[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StageDir,
    [Parameter(Mandatory)] [string] $OutputDir,
    [Parameter(Mandatory)] [string] $Iscc
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'HOTAS_VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'HOTAS_VERSION must be major.minor.patch.' }

$source = (Resolve-Path -LiteralPath $StageDir).Path
$compiler = (Resolve-Path -LiteralPath $Iscc).Path
$output = [System.IO.Path]::GetFullPath($OutputDir)
if ($output -eq [System.IO.Path]::GetPathRoot($output)) { throw 'Refusing to use a filesystem root as OutputDir.' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach ($required in @('HOTAS BF6.exe', 'HOTAS BF6 Launcher.exe', 'VERSION')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required) -PathType Leaf)) {
        throw "Stage directory is incomplete: $required is missing."
    }
}

$script = Join-Path $repoRoot 'packaging\HOTAS-BF6.iss'
& $compiler "/DMyAppVersion=$version" "/DSourceDir=$source" "/DOutputDir=$output" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }

$installer = Join-Path $output "HOTAS-BF6-Setup-v$version.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer was not created: $installer" }
Write-Host "Built $installer"
