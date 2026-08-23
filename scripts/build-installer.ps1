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
$dependencies = Get-Content -LiteralPath (Join-Path $repoRoot 'packaging\dependencies.json') -Raw | ConvertFrom-Json
if ($dependencies.schemaVersion -ne 1) { throw 'Unsupported dependency manifest schema.' }
$vjoy = @($dependencies.components | Where-Object { $_.name -eq 'vJoy' })
$hidhide = @($dependencies.components | Where-Object { $_.name -eq 'HidHide' })
if ($vjoy.Count -ne 1 -or $hidhide.Count -ne 1) { throw 'Dependency manifest must define vJoy and HidHide exactly once.' }
foreach ($dependency in @($vjoy[0], $hidhide[0])) {
    $isValid = $dependency.sha256 -match '^[a-fA-F0-9]{64}$' -and -not [string]::IsNullOrWhiteSpace($dependency.officialSourceUrl) -and -not [string]::IsNullOrWhiteSpace($dependency.authenticodeSubjectContains) -and $null -eq $dependency.silentArguments
    if (-not $isValid) {
        throw "Dependency manifest entry is invalid or has unreviewed silent arguments: $($dependency.name)"
    }
}
$output = [System.IO.Path]::GetFullPath($OutputDir)
if ($output -eq [System.IO.Path]::GetPathRoot($output)) { throw 'Refusing to use a filesystem root as OutputDir.' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach ($required in @('HOTAS BF6.exe', 'HOTAS BF6 Launcher.exe', 'VERSION')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required) -PathType Leaf)) {
        throw "Stage directory is incomplete: $required is missing."
    }
}

$script = Join-Path $repoRoot 'packaging\HOTAS-BF6.iss'
& $compiler "/DMyAppVersion=$version" "/DSourceDir=$source" "/DOutputDir=$output" `
    "/DVJoyVersion=$($vjoy[0].version)" "/DVJoyFileName=$($vjoy[0].filename)" `
    "/DVJoyUrl=$($vjoy[0].officialSourceUrl)" "/DVJoySha256=$($vjoy[0].sha256)" `
    "/DVJoySigner=$($vjoy[0].authenticodeSubjectContains)" `
    "/DHidHideVersion=$($hidhide[0].version)" "/DHidHideFileName=$($hidhide[0].filename)" `
    "/DHidHideUrl=$($hidhide[0].officialSourceUrl)" "/DHidHideSha256=$($hidhide[0].sha256)" `
    "/DHidHideSigner=$($hidhide[0].authenticodeSubjectContains)" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }

$installer = Join-Path $output "HOTAS-BF6-Setup-v$version.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer was not created: $installer" }
Write-Host "Built $installer"
