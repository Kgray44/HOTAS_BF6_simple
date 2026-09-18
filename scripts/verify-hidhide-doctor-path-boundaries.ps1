[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StageDir,
    [Parameter(Mandatory)] [string] $ArtifactRoot,
    [ValidateRange(10, 180)] [int] $TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'

$stage = (Resolve-Path -LiteralPath $StageDir).Path
$doctor = Join-Path $stage 'HidHide Doctor.exe'
if (-not (Test-Path -LiteralPath $doctor -PathType Leaf)) {
    throw "Stage does not contain HidHide Doctor.exe: $stage"
}

$artifactParent = [System.IO.Path]::GetFullPath($ArtifactRoot)
if ($artifactParent -eq [System.IO.Path]::GetPathRoot($artifactParent)) {
    throw 'Refusing to use a filesystem root for path-boundary artifacts.'
}
if (-not (Test-Path -LiteralPath $artifactParent)) {
    New-Item -ItemType Directory -Path $artifactParent | Out-Null
}
$root = Join-Path $artifactParent ("run-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

function Invoke-DoctorReport {
    param([string] $OutputPath)

    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $doctor
    $info.UseShellExecute = $false
    [void]$info.ArgumentList.Add('--headless')
    [void]$info.ArgumentList.Add('--report')
    [void]$info.ArgumentList.Add($OutputPath)
    $process = [System.Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Doctor headless report timed out for destination: $OutputPath"
    }
    return [pscustomobject]@{ ExitCode = $process.ExitCode; OutputPath = $OutputPath }
}

$specialDirectory = Join-Path $root "unicode-Ω ampersand-& percent-% semicolon-; quote-'"
New-Item -ItemType Directory -Path $specialDirectory | Out-Null
$specialReport = Join-Path $specialDirectory "report-Ω & % ; '.json"
$special = Invoke-DoctorReport -OutputPath $specialReport
if ($special.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $specialReport -PathType Leaf)) {
    throw "Doctor did not create the direct-argument special-path report (exit $($special.ExitCode))."
}
$specialJson = Get-Content -LiteralPath $specialReport -Raw | ConvertFrom-Json
if ($null -eq $specialJson) {
    throw 'The direct-argument special-path report was not valid JSON.'
}

$blockedParent = Join-Path $root 'not-a-directory'
[System.IO.File]::WriteAllText($blockedParent, 'This test artifact intentionally occupies the parent path.')
$blockedReport = Join-Path $blockedParent 'report.json'
$blocked = Invoke-DoctorReport -OutputPath $blockedReport
if (Test-Path -LiteralPath $blockedReport -PathType Leaf) {
    throw 'Doctor wrote a report through a non-directory parent path.'
}
if ($blocked.ExitCode -eq 0) {
    throw 'Doctor reported success for a blocked report destination.'
}

[pscustomobject]@{
    stage = $stage
    specialPathExitCode = $special.ExitCode
    specialPathReportBytes = (Get-Item -LiteralPath $specialReport).Length
    blockedPathExitCode = $blocked.ExitCode
    blockedPathCreatedReport = $false
    shellUsed = $false
    note = 'Arguments were supplied through ProcessStartInfo.ArgumentList; no command shell interpreted the destination.'
} | ConvertTo-Json -Depth 4
