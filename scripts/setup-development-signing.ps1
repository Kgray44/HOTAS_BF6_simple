[CmdletBinding()]
param(
    [switch] $TrustForCurrentUser
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

$existingCertificates = @(Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' | Where-Object {
    Test-DevelopmentSigningCertificate $_
} | Sort-Object NotAfter -Descending | Select-Object -First 1)
$certificate = if ($existingCertificates.Count -gt 0) { $existingCertificates[0] } else { $null }

if ($null -eq $certificate) {
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears(3)
    Write-Host 'Created a new CurrentUser development code-signing certificate.'
} else {
    Write-Host 'Reusing the existing CurrentUser development code-signing certificate.'
}

if ($TrustForCurrentUser) {
    $publicCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    )
    try {
        foreach ($storeName in @('TrustedPublisher', 'Root')) {
            $providerPath = "Cert:\CurrentUser\$storeName"
            $alreadyTrusted = @(Get-ChildItem -LiteralPath $providerPath | Where-Object {
                $_.Thumbprint -eq $certificate.Thumbprint
            }).Count -gt 0
            if ($alreadyTrusted) {
                Write-Host "Development certificate is already trusted in $providerPath."
                continue
            }

            $store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
                $storeName,
                [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser
            )
            try {
                $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
                $store.Add($publicCertificate)
                Write-Host "Trusted the development certificate in $providerPath."
            } finally {
                $store.Close()
                $store.Dispose()
            }
        }
    } finally {
        $publicCertificate.Dispose()
    }
}

Write-Host 'DEVELOPMENT ONLY: this self-signed certificate must never be distributed or used for public releases.'
Write-Host "Subject: $($certificate.Subject)"
Write-Host "Thumbprint: $($certificate.Thumbprint)"
Write-Host "Expires: $($certificate.NotAfter.ToString('u'))"
