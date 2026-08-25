[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

foreach ($candidate in $Path) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Authenticode verification target is missing: $candidate"
    }

    $file = Get-Item -LiteralPath $candidate -ErrorAction Stop
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -ne 'Valid') {
        throw "Authenticode verification failed for '$($file.FullName)': $($signature.Status) $($signature.StatusMessage)"
    }
    if ($null -eq $signature.SignerCertificate) {
        throw "Authenticode verification failed for '$($file.FullName)': signer certificate is missing."
    }

    $timestampStatus = if ($null -ne $signature.TimeStamperCertificate) {
        "present ($($signature.TimeStamperCertificate.Subject))"
    } else {
        'not present'
    }
    Write-Host "Authenticode valid: $($file.Name); signer: $($signature.SignerCertificate.Subject); timestamp: $timestampStatus"
}
