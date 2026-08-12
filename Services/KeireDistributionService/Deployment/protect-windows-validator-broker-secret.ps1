[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Output,

    [string]$BrokerIdentity = "NT AUTHORITY\NETWORK SERVICE"
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Security

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell session."
}

$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) {
    throw "The broker secret file already exists. Rotate it explicitly instead of overwriting it: $outputPath"
}

$parent = Split-Path -Parent $outputPath
[IO.Directory]::CreateDirectory($parent) | Out-Null
$secureSecret = Read-Host "Enter VALIDATOR_BROKER_SECRET" -AsSecureString
$secretPointer = [IntPtr]::Zero
$secretBytes = $null
$protectedBytes = $null
try {
    $secretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureSecret)
    $secret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer)
    if ($secret.Length -lt 32 -or $secret.Length -gt 256 -or $secret -match '\s') {
        throw "VALIDATOR_BROKER_SECRET must contain 32 to 256 non-whitespace characters."
    }

    $entropy = [Text.Encoding]::UTF8.GetBytes("KeireMarketplaceValidatorBroker/v1")
    $secretBytes = [Text.Encoding]::UTF8.GetBytes($secret)
    $protectedBytes = [System.Security.Cryptography.ProtectedData]::Protect(
        $secretBytes,
        $entropy,
        [System.Security.Cryptography.DataProtectionScope]::LocalMachine)
    $temporary = Join-Path $parent (".{0}.{1}.tmp" -f ([IO.Path]::GetFileName($outputPath)), [Guid]::NewGuid().ToString("N"))
    try {
        [IO.File]::WriteAllBytes($temporary, $protectedBytes)
        Move-Item -LiteralPath $temporary -Destination $outputPath
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }

    $fileSecurity = [Security.AccessControl.FileSecurity]::new()
    $fileSecurity.SetAccessRuleProtection($true, $false)
    $rules = @(
        @([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::LocalSystemSid, $null),
            [Security.AccessControl.FileSystemRights]::FullControl),
        @([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null),
            [Security.AccessControl.FileSystemRights]::FullControl),
        @([Security.Principal.NTAccount]::new($BrokerIdentity).Translate(
                [Security.Principal.SecurityIdentifier]),
            [Security.AccessControl.FileSystemRights]::Read)
    )
    foreach ($rule in $rules) {
        $fileSecurity.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
                $rule[0],
                $rule[1],
                [Security.AccessControl.AccessControlType]::Allow))
    }
    Set-Acl -LiteralPath $outputPath -AclObject $fileSecurity
}
catch {
    if (Test-Path -LiteralPath $outputPath) {
        Remove-Item -LiteralPath $outputPath -Force
    }
    throw
}
finally {
    if ($null -ne $secretBytes) {
        [Array]::Clear($secretBytes, 0, $secretBytes.Length)
    }
    if ($null -ne $protectedBytes) {
        [Array]::Clear($protectedBytes, 0, $protectedBytes.Length)
    }
    if ($secretPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer)
    }
}

Write-Host "Stored the validator broker secret as ACL-protected machine-DPAPI ciphertext."
