[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# Keep the qualified Windows runtime explicit in both workflows, then verify
# those pins before any build begins.  This is deliberately a small guard,
# not another workflow abstraction: install-qt-action still receives ordinary,
# reviewable literal pins in ci.yml and release.yml.
$qualifiedQtVersion = '6.8.3'
$qualifiedQtArch = 'win64_msvc2022_64'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Assert-QualifiedQtWorkflow {
    param(
        [Parameter(Mandatory)] [string] $Path
    )

    $content = Get-Content -LiteralPath $Path -Raw
    $matches = [regex]::Matches(
        $content,
        "(?ms)uses:\s*jurplel/install-qt-action@v4\s*\r?\n\s*with:\s*\r?\n\s*version:\s*'(?<version>[^']+)'\s*\r?\n\s*arch:\s*'(?<arch>[^']+)'")
    if ($matches.Count -ne 1) {
        throw "Expected exactly one install-qt-action pin in $Path; found $($matches.Count)."
    }

    $match = $matches[0]
    if ($match.Groups['version'].Value -ne $qualifiedQtVersion -or
        $match.Groups['arch'].Value -ne $qualifiedQtArch) {
        throw "$Path must use Qt $qualifiedQtVersion / $qualifiedQtArch; found Qt $($match.Groups['version'].Value) / $($match.Groups['arch'].Value)."
    }
}

$ci = Join-Path $repoRoot '.github\workflows\ci.yml'
$release = Join-Path $repoRoot '.github\workflows\release.yml'
Assert-QualifiedQtWorkflow -Path $ci
Assert-QualifiedQtWorkflow -Path $release

$productCatalog = Get-Content -LiteralPath (Join-Path $repoRoot 'docs\catalog\product.json') -Raw | ConvertFrom-Json
$runtime = @($productCatalog.testedStack | Where-Object { $_.category -eq 'UI/runtime' })
if ($runtime.Count -ne 1 -or $runtime[0].item -ne "Qt $qualifiedQtVersion") {
    throw "The active product catalog must identify Qt $qualifiedQtVersion as the qualified UI/runtime baseline."
}

$documentationGenerator = Get-Content -LiteralPath (Join-Path $repoRoot 'scripts\sync-documentation.ps1') -Raw
if (-not $documentationGenerator.Contains(".toolchain\Qt\$qualifiedQtVersion\msvc2022_64")) {
    throw 'The generated local-build guidance does not match the qualified Qt/MSVC 2022 kit.'
}

Write-Host "Verified active CI and release toolchain alignment: Qt $qualifiedQtVersion / $qualifiedQtArch."
