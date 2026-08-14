[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Output,

    [Parameter(Mandatory = $true)]
    [ValidateSet("ValidatorAttestationPrivateKey", "PublicationPrivateKey", "PublicationQueueSecret")]
    [string]$Purpose,

    [string]$InputPrivateKeyPem = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Security

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell session."
}

$contract = switch ($Purpose) {
    "ValidatorAttestationPrivateKey" {
        @{ Entropy = "KeireMarketplaceValidatorAttestation/v1"; Identity = "NT AUTHORITY\LOCAL SERVICE"; IsKey = $true }
    }
    "PublicationPrivateKey" {
        @{ Entropy = "KeireMarketplacePublicationPrivateKey/v1"; Identity = "NT AUTHORITY\NETWORK SERVICE"; IsKey = $true }
    }
    "PublicationQueueSecret" {
        @{ Entropy = "KeireMarketplacePublicationQueueSecret/v1"; Identity = "NT AUTHORITY\NETWORK SERVICE"; IsKey = $false }
    }
}
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) {
    throw "The protected secret already exists. Rotate it explicitly instead of overwriting it: $outputPath"
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $outputPath)) | Out-Null

$secureValue = if ([string]::IsNullOrWhiteSpace($InputPrivateKeyPem)) {
    Read-Host "Enter $Purpose" -AsSecureString
} else {
    if (-not $contract.IsKey -or -not (Test-Path -LiteralPath $InputPrivateKeyPem -PathType Leaf)) {
        throw "InputPrivateKeyPem is supported only for an existing private-key PEM file."
    }
    $privateFile = Get-Item -LiteralPath $InputPrivateKeyPem -Force
    if (($privateFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $privateFile.Length -le 0 -or $privateFile.Length -gt 32768) {
        throw "The input private-key PEM is unsafe or outside its size limit."
    }
    $pem = Get-Content -LiteralPath $privateFile.FullName -Raw
    if ($pem -notmatch '(?s)^-----BEGIN PRIVATE KEY-----\s+([A-Za-z0-9+/=\r\n]+)\s+-----END PRIVATE KEY-----\s*$') {
        throw "The input file is not a bounded PKCS#8 private-key PEM."
    }
    $canonical = $Matches[1] -replace '\s', ''
    ConvertTo-SecureString $canonical -AsPlainText -Force
}
$pointer = [IntPtr]::Zero
$plainBytes = $null
$protectedBytes = $null
try {
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureValue)
    $value = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    if ($contract.IsKey) {
        if ($value.Length -lt 40 -or $value.Length -gt 32768 -or $value -cnotmatch '^[A-Za-z0-9+/]+={0,2}$') {
            throw "$Purpose must be a canonical-base64 PKCS#8 Ed25519 private key."
        }
        try {
            $decoded = [Convert]::FromBase64String($value)
            if (-not [string]::Equals([Convert]::ToBase64String($decoded), $value, [StringComparison]::Ordinal)) {
                throw "noncanonical"
            }
        }
        catch {
            throw "$Purpose must be a canonical-base64 PKCS#8 Ed25519 private key."
        }
        finally {
            if ($null -ne $decoded) { [Array]::Clear($decoded, 0, $decoded.Length) }
        }
    }
    elseif ($value.Length -lt 32 -or $value.Length -gt 256 -or $value -match '\s') {
        throw "$Purpose must contain 32 to 256 non-whitespace characters."
    }

    $plainBytes = [Text.Encoding]::UTF8.GetBytes($value)
    $protectedBytes = [Security.Cryptography.ProtectedData]::Protect(
        $plainBytes,
        [Text.Encoding]::UTF8.GetBytes($contract.Entropy),
        [Security.Cryptography.DataProtectionScope]::LocalMachine)
    $temporary = Join-Path (Split-Path -Parent $outputPath) `
        (".{0}.{1}.tmp" -f ([IO.Path]::GetFileName($outputPath)), [Guid]::NewGuid().ToString("N"))
    try {
        [IO.File]::WriteAllBytes($temporary, $protectedBytes)
        Move-Item -LiteralPath $temporary -Destination $outputPath
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }

    $acl = [Security.AccessControl.FileSecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($rule in @(
            @([Security.Principal.SecurityIdentifier]::new(
                    [Security.Principal.WellKnownSidType]::LocalSystemSid, $null),
                [Security.AccessControl.FileSystemRights]::FullControl),
            @([Security.Principal.SecurityIdentifier]::new(
                    [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null),
                [Security.AccessControl.FileSystemRights]::FullControl),
            @([Security.Principal.NTAccount]::new($contract.Identity).Translate(
                    [Security.Principal.SecurityIdentifier]),
                [Security.AccessControl.FileSystemRights]::Read))) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
                $rule[0], $rule[1], [Security.AccessControl.AccessControlType]::Allow))
    }
    Set-Acl -LiteralPath $outputPath -AclObject $acl
}
catch {
    if (Test-Path -LiteralPath $outputPath) { Remove-Item -LiteralPath $outputPath -Force }
    throw
}
finally {
    if ($null -ne $plainBytes) { [Array]::Clear($plainBytes, 0, $plainBytes.Length) }
    if ($null -ne $protectedBytes) { [Array]::Clear($protectedBytes, 0, $protectedBytes.Length) }
    if ($pointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
}

Write-Host "Stored $Purpose as least-privilege machine-DPAPI ciphertext."
