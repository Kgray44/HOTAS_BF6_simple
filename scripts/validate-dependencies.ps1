[CmdletBinding()]
param(
    [string] $ManifestPath = (Join-Path $PSScriptRoot '..\packaging\dependencies.json')
)

$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or @($manifest.components).Count -ne 2) {
    throw 'Dependency manifest must contain schemaVersion 1 and exactly vJoy plus HidHide.'
}

foreach ($name in @('vJoy', 'HidHide')) {
    $component = @($manifest.components | Where-Object { $_.name -eq $name })
    if ($component.Count -ne 1) { throw "Dependency manifest is missing $name." }
    $item = $component[0]
    if ($item.architecture -ne 'x64' -or $item.installerType -ne 'exe') {
        throw "$name must pin an x64 EXE installer."
    }
    $isOfficialGitHubRelease = -not [string]::IsNullOrWhiteSpace($item.officialSourceUrl) -and $item.officialSourceUrl.StartsWith('https://github.com/', [StringComparison]::OrdinalIgnoreCase)
    if (-not $isOfficialGitHubRelease) {
        throw "$name must use an HTTPS GitHub official-release URL."
    }
    if ($item.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw "$name has no valid SHA-256 pin." }
    if ([string]::IsNullOrWhiteSpace($item.authenticodeSubjectContains)) {
        throw "$name must pin its expected Authenticode signer subject."
    }
    if ($null -ne $item.silentArguments) { throw "$name silent arguments must be explicitly reviewed before use." }
}

Write-Host 'Dependency manifest validation passed.'
