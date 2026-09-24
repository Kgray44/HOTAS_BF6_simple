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
$doctor = Join-Path $build 'HidHide Doctor.exe'
$doctorHelper = Join-Path $build 'HidHideDoctorRepair.exe'
if (-not (Test-Path -LiteralPath $mapper -PathType Leaf)) { throw "Missing mapper: $mapper" }
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Missing launcher: $launcher" }
if (-not (Test-Path -LiteralPath $doctor -PathType Leaf)) { throw "Missing HidHide Doctor: $doctor" }
if (-not (Test-Path -LiteralPath $doctorHelper -PathType Leaf)) { throw "Missing HidHide Doctor helper: $doctorHelper" }

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
Copy-Item -LiteralPath $doctor -Destination (Join-Path $stage 'HidHide Doctor.exe')
Copy-Item -LiteralPath $doctorHelper -Destination (Join-Path $stage 'HidHideDoctorRepair.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'HOTAS_VERSION') -Destination (Join-Path $stage 'VERSION')

& $deploy --release --compiler-runtime --qmldir (Join-Path $repoRoot 'qml') (Join-Path $stage 'HOTAS BF6.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }
& $deploy --release --compiler-runtime --qmldir (Join-Path $repoRoot 'qml') (Join-Path $stage 'HidHide Doctor.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for HidHide Doctor with exit code $LASTEXITCODE." }

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
    'HidHide Doctor.exe',
    'HidHideDoctorRepair.exe',
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

$componentVersions = @{}
foreach ($component in @('HOTAS BF6.exe', 'HidHide Doctor.exe', 'HidHideDoctorRepair.exe')) {
    $fileVersion = (Get-Item -LiteralPath (Join-Path $stage $component)).VersionInfo.FileVersion
    if ([string]::IsNullOrWhiteSpace($fileVersion) -or -not $fileVersion.StartsWith($version)) {
        throw "Component version mismatch: $component reports '$fileVersion', expected $version."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $stage $component)
    $componentVersions[$component] = [ordered]@{
        fileVersion = $fileVersion
        authenticodeStatus = [string]$signature.Status
        signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
    }
}

# The manifest is generated only after the deployment checks pass. It is a
# release-candidate audit record, not a runtime input, and includes every
# staged file so a missing helper or Qt dependency is observable before setup.
$manifestEntries = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Name -notin @('HidHideDoctor-ComponentManifest.json', 'SHA256SUMS.txt') } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length).TrimStart('\', '/') -replace '\\', '/'
        [ordered]@{ path = $relative; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
$manifest = [ordered]@{
    schemaVersion = 1
    productVersion = $version
    components = $componentVersions
    helperProtocolVersion = 2
    integrationProtocolVersion = 1
    generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    files = $manifestEntries
}
$manifestPath = Join-Path $stage 'HidHideDoctor-ComponentManifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ascii -Value "$manifestHash  HidHideDoctor-ComponentManifest.json"

Write-Host "Staged HOTAS BF6 $version at $stage"
