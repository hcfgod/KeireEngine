[CmdletBinding()]
param([Parameter(Mandatory)] [string] $Thumbprint)

# Private parent-process pipe only. Never invoke directly into terminal output.
$ErrorActionPreference = 'Stop'
$pfx = $null
try
{
    if ($Thumbprint -notmatch '^[A-Fa-f0-9]{40}$')
    {
        throw 'Invalid development certificate identifier.'
    }
    $certificate = Get-Item -LiteralPath "Cert:/CurrentUser/My/$Thumbprint"
    $developmentExtension = $certificate.Extensions | Where-Object { $_.Oid.Value -eq '1.3.6.1.4.1.311.84.1.1' }
    if ($certificate.Subject -ne 'CN=localhost' -or -not $certificate.HasPrivateKey -or
        -not $developmentExtension -or $certificate.NotAfter -le [DateTime]::Now -or
        -not (Test-Path -LiteralPath "Cert:/CurrentUser/Root/$Thumbprint"))
    {
        throw 'A trusted current-user localhost development certificate is required.'
    }
    $password = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
    $pfx = $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $password)
    @{ pfx = [Convert]::ToBase64String($pfx); passphrase = $password } | ConvertTo-Json -Compress | Write-Output
}
catch
{
    [Console]::Error.WriteLine('The trusted localhost development certificate could not be loaded.')
    exit 1
}
finally
{
    if ($null -ne $pfx)
    {
        [Array]::Clear($pfx, 0, $pfx.Length)
    }
    $password = $null
    $certificate = $null
}
