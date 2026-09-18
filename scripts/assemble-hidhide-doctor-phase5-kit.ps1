[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StageDir,
    [Parameter(Mandatory)] [string] $KitDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$stage = (Resolve-Path -LiteralPath $StageDir).Path
$kit = [System.IO.Path]::GetFullPath($KitDir)
if ($kit -eq [System.IO.Path]::GetPathRoot($kit)) {
    throw 'Refusing to assemble a kit at a filesystem root.'
}
if (Test-Path -LiteralPath $kit) {
    throw "Refusing to overwrite an existing qualification kit: $kit"
}

$stageManifestPath = Join-Path $stage 'HidHideDoctor-ComponentManifest.json'
if (-not (Test-Path -LiteralPath $stageManifestPath -PathType Leaf)) {
    throw "Stage does not contain a component manifest: $stage"
}
$stageManifest = Get-Content -LiteralPath $stageManifestPath -Raw | ConvertFrom-Json
foreach ($entry in $stageManifest.files) {
    $path = Join-Path $stage ($entry.path -replace '/', '\\')
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Stage manifest entry is missing: $($entry.path)"
    }
    $item = Get-Item -LiteralPath $path
    if ($item.Length -ne [int64]$entry.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256.ToLowerInvariant()) {
        throw "Stage manifest entry does not verify: $($entry.path)"
    }
}

$candidate = Join-Path $kit 'candidate'
$documents = Join-Path $kit 'documents'
New-Item -ItemType Directory -Path $candidate, $documents -Force | Out-Null
Get-ChildItem -LiteralPath $stage -Force | Copy-Item -Destination $candidate -Recurse -Force

$guideFiles = @(
    'HidHide_Doctor_User_and_Tester_Guide_v1.md',
    'HidHide_Doctor_Phase5_Owner_Native_Qualification_Worksheet.md',
    'HidHide_Doctor_Phase5_External_Qualification_Kit.md'
)
foreach ($guideFile in $guideFiles) {
    $source = Join-Path $repoRoot (Join-Path 'docs\hidhide-doctor' $guideFile)
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing kit document: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $documents $guideFile) -Force
}

$receipt = [ordered]@{
    schemaVersion = 1
    productVersion = $stageManifest.productVersion
    candidateManifestSha256 = (Get-FileHash -LiteralPath $stageManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    hidhideDoctorExeSha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'HidHide Doctor.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    repairHelperExeSha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'HidHideDoctorRepair.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    repairAuthorized = $false
    note = 'Normal-mode Doctor qualification only. No repair, elevation, installer, restart, or release authorization is included.'
}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $kit 'candidate-receipt.json') -Encoding utf8

$files = @(Get-ChildItem -LiteralPath $kit -Recurse -File |
    Where-Object { $_.Name -notin @('QualificationKit-Manifest.json', 'SHA256SUMS.txt') } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($kit.Length).TrimStart('\', '/') -replace '\\', '/'
        [ordered]@{ path = $relative; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
$kitManifest = [ordered]@{
    schemaVersion = 1
    kind = 'HidHide Doctor Phase 5 external qualification kit'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    files = $files
}
$manifestPath = Join-Path $kit 'QualificationKit-Manifest.json'
$kitManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $kit 'SHA256SUMS.txt') -Encoding ascii -Value "$manifestHash  QualificationKit-Manifest.json"

[pscustomobject]@{
    KitDirectory = $kit
    ProductVersion = $stageManifest.productVersion
    StageManifestSha256 = $receipt.candidateManifestSha256
    KitManifestSha256 = $manifestHash
    FileCount = $files.Count
} | ConvertTo-Json -Compress
