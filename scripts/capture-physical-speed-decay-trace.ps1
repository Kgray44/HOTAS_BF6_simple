[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-z0-9-]+$')]
    [string]$Label,
    [Parameter(Mandatory = $true)]
    [ValidateSet('x', 'y', 'z', 'rx', 'ry', 'rz', 'slider0', 'slider1')]
    [string]$Axis,
    [ValidateRange(1, 30)]
    [int]$Seconds = 6,
    [ValidateRange(1, 10)]
    [int]$PollMilliseconds = 4,
    [int]$RawMinimum = -10000,
    [int]$RawMaximum = 10000,
    [string]$OutputDirectory = (Join-Path (Get-Location) 'artifacts\adaptive_response_validation\v2.3.T-physical-captures\raw'),
    [string]$DeviceId
)

$ErrorActionPreference = 'Stop'
$root = Get-Location
$exe = Join-Path $root 'build-v2.3.T-local\Release\adaptive_physical_trace_capture.exe'
$qtBin = 'C:\Users\kkids\Documents\HOTAS_BF6\.toolchain\Qt\6.8.3\msvc2022_64\bin'
if (-not (Test-Path -LiteralPath $exe)) { throw "Capture executable is missing: $exe" }
if (-not (Test-Path -LiteralPath $qtBin)) { throw "Qt runtime is missing: $qtBin" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$env:Path = "$qtBin;$env:Path"
$output = Join-Path $OutputDirectory "$Label.physical.csv"
if ($RawMinimum -ge $RawMaximum) { throw 'RawMinimum must be less than RawMaximum.' }
$arguments = @('--axis', $Axis, '--label', $Label, '--seconds', $Seconds, '--poll-ms', $PollMilliseconds,
    '--raw-min', $RawMinimum, '--raw-max', $RawMaximum, '--output', $output)
if (-not [string]::IsNullOrWhiteSpace($DeviceId)) { $arguments += @('--device-id', $DeviceId) }
& $exe @arguments
if ($LASTEXITCODE -ne 0) { throw "Physical trace capture failed with exit code $LASTEXITCODE" }
Write-Output "Physical trace saved: $output"
