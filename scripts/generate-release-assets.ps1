[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $InstallerPath,
    [Parameter(Mandatory)] [string] $OutputDir,
    [Parameter(Mandatory)] [string] $ReleaseTag
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must be major.minor.patch.' }
if ($ReleaseTag -ne "v$version") { throw "Release tag $ReleaseTag does not match VERSION $version." }

$installer = Get-Item -LiteralPath $InstallerPath -ErrorAction Stop
$expectedName = "HOTAS-BF6-Setup-v$version.exe"
if ($installer.Name -ne $expectedName) { throw "Installer must be named $expectedName." }
$output = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $output | Out-Null

$installerHash = (Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    schema = 1
    channel = 'stable'
    version = $version
    tag = $ReleaseTag
    installer = $installer.Name
    installer_url = "https://github.com/Kgray44/HOTAS_BF6_simple/releases/download/$ReleaseTag/$($installer.Name)"
    sha256 = $installerHash
    minimum_launcher_version = '1.0.0'
    published_utc = [DateTime]::UtcNow.ToString('o')
    release_notes_url = "https://github.com/Kgray44/HOTAS_BF6_simple/releases/tag/$ReleaseTag"
}
$manifestPath = Join-Path $output 'update-manifest.json'
$encoding = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 3), $encoding)
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sums = @(
    "$installerHash *$($installer.Name)",
    "$manifestHash *update-manifest.json"
) -join "`n"
[System.IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), "$sums`n", $encoding)
Write-Host "Generated update manifest and SHA256SUMS for $ReleaseTag"
