[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Signer,
    [Parameter(Mandatory = $true)] [string]$ProtectedPrivateKey,
    [Parameter(Mandatory = $true)] [string]$ProtectedQueueSecret,
    [Parameter(Mandatory = $true)] [uri]$SupabaseUrl,
    [Parameter(Mandatory = $true)] [string]$PublicationPublicKey,
    [Parameter(Mandatory = $true)] [string]$ValidatorAttestationPublicKey,
    [Parameter(Mandatory = $true)] [string]$WorkerId,
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Security

foreach ($path in @($Signer, $ProtectedPrivateKey, $ProtectedQueueSecret, $PublicationPublicKey,
        $ValidatorAttestationPublicKey)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required publication signer file is missing: $path" }
}
if ($SupabaseUrl.Scheme -ne "https" -or -not [string]::IsNullOrEmpty($SupabaseUrl.UserInfo) -or
    -not [string]::IsNullOrEmpty($SupabaseUrl.Query) -or -not [string]::IsNullOrEmpty($SupabaseUrl.Fragment)) {
    throw "SupabaseUrl must be an HTTPS origin without credentials or query data."
}
if ($WorkerId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$') { throw "WorkerId contains unsupported characters." }

$expectedAccess = @{
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::LocalSystemSid, $null).Value) =
        [int][Security.AccessControl.FileSystemRights]::FullControl
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null).Value) =
        [int][Security.AccessControl.FileSystemRights]::FullControl
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::NetworkServiceSid, $null).Value) =
        [int]([Security.AccessControl.FileSystemRights]::Read -bor
            [Security.AccessControl.FileSystemRights]::Synchronize)
}
foreach ($path in @($ProtectedPrivateKey, $ProtectedQueueSecret)) {
    $acl = Get-Acl -LiteralPath $path
    if (-not $acl.AreAccessRulesProtected -or @($acl.Access).Count -ne $expectedAccess.Count) {
        throw "A publication signer secret does not have its exact protected ACL: $path"
    }
    foreach ($entry in $expectedAccess.GetEnumerator()) {
        $matches = @($acl.Access | Where-Object {
                $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq $entry.Key
            })
        if ($matches.Count -ne 1 -or $matches[0].IsInherited -or
            $matches[0].AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
            [int]$matches[0].FileSystemRights -ne $entry.Value) {
            throw "A publication signer secret is not restricted to its exact NETWORK SERVICE ACL: $path"
        }
    }
}
if ($ValidateOnly) {
    Write-Host "Windows Marketplace publication signer inputs and protected-secret ACLs are valid."
    exit 0
}

function Unprotect([string]$Path, [string]$Entropy) {
    $ciphertext = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
    try {
        return [Security.Cryptography.ProtectedData]::Unprotect(
            $ciphertext,
            [Text.Encoding]::UTF8.GetBytes($Entropy),
            [Security.Cryptography.DataProtectionScope]::LocalMachine)
    }
    finally {
        [Array]::Clear($ciphertext, 0, $ciphertext.Length)
    }
}

$privateBytes = $null
$secretBytes = $null
try {
    $privateBytes = Unprotect $ProtectedPrivateKey "KeireMarketplacePublicationPrivateKey/v1"
    $secretBytes = Unprotect $ProtectedQueueSecret "KeireMarketplacePublicationQueueSecret/v1"
    $privateKey = [Text.Encoding]::UTF8.GetString($privateBytes)
    $queueSecret = [Text.Encoding]::UTF8.GetString($secretBytes)
    if ($privateKey -cnotmatch '^[A-Za-z0-9+/]+={0,2}$' -or $queueSecret.Length -lt 32 -or
        $queueSecret.Length -gt 256 -or $queueSecret -match '\s') {
        throw "A protected publication signer secret has an invalid shape."
    }
    $env:KEIRE_SUPABASE_URL = $SupabaseUrl.GetLeftPart([UriPartial]::Authority)
    $env:KEIRE_MARKETPLACE_PUBLICATION_SIGNER_SECRET = $queueSecret
    $env:KEIRE_MARKETPLACE_PUBLICATION_PRIVATE_KEY = $privateKey
    $env:KEIRE_MARKETPLACE_PUBLICATION_PUBLIC_KEY = (Resolve-Path -LiteralPath $PublicationPublicKey).Path
    $env:KEIRE_VALIDATOR_ATTESTATION_PUBLIC_KEY = (Resolve-Path -LiteralPath $ValidatorAttestationPublicKey).Path
    $env:KEIRE_MARKETPLACE_PUBLICATION_WORKER_ID = $WorkerId
    & $Signer
    exit $LASTEXITCODE
}
finally {
    Remove-Item Env:KEIRE_MARKETPLACE_PUBLICATION_SIGNER_SECRET -ErrorAction SilentlyContinue
    Remove-Item Env:KEIRE_MARKETPLACE_PUBLICATION_PRIVATE_KEY -ErrorAction SilentlyContinue
    if ($null -ne $privateBytes) { [Array]::Clear($privateBytes, 0, $privateBytes.Length) }
    if ($null -ne $secretBytes) { [Array]::Clear($secretBytes, 0, $secretBytes.Length) }
}
