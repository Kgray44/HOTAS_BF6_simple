[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path,
    [string] $Thumbprint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$subject = 'CN=HOTAS BF6 Development'
$codeSigningEku = '1.3.6.1.5.5.7.3.3'

function Test-DevelopmentSigningCertificate {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2] $Certificate)

    if ($Certificate.Subject -ne $subject -or -not $Certificate.HasPrivateKey -or $Certificate.NotAfter -le (Get-Date)) {
        return $false
    }
    $ekuExtension = @($Certificate.Extensions | Where-Object {
        $_.Oid.Value -eq '2.5.29.37'
    }) | Select-Object -First 1
    if ($null -eq $ekuExtension) { return $false }

    $eku = [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension] $ekuExtension
    return @($eku.EnhancedKeyUsages | ForEach-Object { $_.Value }) -contains $codeSigningEku
}

function Find-SignTool {
    $programFilesX86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
        throw 'ProgramFiles(x86) is unavailable; could not locate Windows SDK signtool.exe.'
    }
    $sdkBin = Join-Path $programFilesX86 'Windows Kits\10\bin'
    if (-not (Test-Path -LiteralPath $sdkBin -PathType Container)) {
        throw 'Windows SDK bin directory was not found; install the Windows SDK Signing Tools component.'
    }

    $candidate = Get-ChildItem -LiteralPath $sdkBin -Recurse -Filter 'signtool.exe' -File |
        Where-Object { $_.Directory.Name -eq 'x64' } |
        Sort-Object -Property @{ Expression = {
            try { [Version]$_.Directory.Parent.Name } catch { [Version]'0.0' }
        }; Descending = $true } |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw 'Windows SDK x64 signtool.exe was not found; install the Windows SDK Signing Tools component.'
    }
    return $candidate.FullName
}

$normalizedThumbprint = $Thumbprint -replace '\s', ''
$candidates = @(Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' | Where-Object {
    Test-DevelopmentSigningCertificate $_
})
if (-not [string]::IsNullOrWhiteSpace($normalizedThumbprint)) {
    $candidates = @($candidates | Where-Object { $_.Thumbprint -eq $normalizedThumbprint })
}
$selectedCertificates = @($candidates | Sort-Object NotAfter -Descending | Select-Object -First 1)
$certificate = if ($selectedCertificates.Count -gt 0) { $selectedCertificates[0] } else { $null }
if ($null -eq $certificate) {
    throw "No usable '$subject' certificate was found in Cert:\CurrentUser\My. Run .\scripts\setup-development-signing.ps1 -TrustForCurrentUser first."
}

$files = foreach ($candidate in $Path) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Development signing target is missing: $candidate"
    }
    (Get-Item -LiteralPath $candidate -ErrorAction Stop).FullName
}
$signTool = Find-SignTool
foreach ($file in $files) {
    & $signTool sign /fd SHA256 /sha1 $certificate.Thumbprint /s My /tr http://timestamp.acs.microsoft.com /td SHA256 $file
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for '$file' with exit code $LASTEXITCODE." }
}

& (Join-Path $PSScriptRoot 'verify-authenticode.ps1') -Path $files
