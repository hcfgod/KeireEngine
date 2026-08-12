[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Broker,

    [Parameter(Mandatory = $true)]
    [string]$Validator,

    [Parameter(Mandatory = $true)]
    [string]$ExchangeRoot,

    [Parameter(Mandatory = $true)]
    [string]$SecretFile,

    [Parameter(Mandatory = $true)]
    [uri]$SupabaseUrl,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedValidatorFingerprint,

    [Parameter(Mandatory = $true)]
    [string]$WorkerId,

    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Security

foreach ($path in @($Broker, $Validator, $SecretFile)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required validator broker file is missing: $path"
    }
}
if (-not (Test-Path -LiteralPath $ExchangeRoot -PathType Container)) {
    throw "The validator exchange root is missing: $ExchangeRoot"
}
if ($SupabaseUrl.Scheme -ne "https" -or -not [string]::IsNullOrEmpty($SupabaseUrl.UserInfo) -or
    -not [string]::IsNullOrEmpty($SupabaseUrl.Query) -or -not [string]::IsNullOrEmpty($SupabaseUrl.Fragment)) {
    throw "SupabaseUrl must be an HTTPS origin without credentials or query data."
}
if ($ExpectedValidatorFingerprint -cnotmatch '^[0-9a-f]{64}$') {
    throw "ExpectedValidatorFingerprint must be a lowercase SHA-256 digest."
}
if ($WorkerId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$') {
    throw "WorkerId contains unsupported characters."
}

$actualFingerprint = (Get-FileHash -LiteralPath $Validator -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not [string]::Equals(
        $actualFingerprint,
        $ExpectedValidatorFingerprint,
        [StringComparison]::Ordinal)) {
    throw "The validator binary fingerprint does not match the reviewed release."
}

$secretPath = (Resolve-Path -LiteralPath $SecretFile).Path
$secretAcl = Get-Acl -LiteralPath $secretPath
if (-not $secretAcl.AreAccessRulesProtected) {
    throw "The validator broker secret inherits filesystem permissions."
}
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
if (@($secretAcl.Access).Count -ne $expectedAccess.Count) {
    throw "The validator broker secret ACL has an unexpected rule count."
}
foreach ($entry in $expectedAccess.GetEnumerator()) {
    $matches = @($secretAcl.Access | Where-Object {
            $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq $entry.Key
        })
    if ($matches.Count -ne 1 -or $matches[0].IsInherited -or
        $matches[0].AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
        [int]$matches[0].FileSystemRights -ne $entry.Value) {
        throw "The validator broker secret ACL does not match its exact least-privilege contract."
    }
}

if ($ValidateOnly) {
    Write-Host "Windows marketplace validator broker inputs, secret ACL, and worker fingerprint are valid."
    exit 0
}

$protectedBytes = [IO.File]::ReadAllBytes($secretPath)
$secretBytes = $null
try {
    $entropy = [Text.Encoding]::UTF8.GetBytes("KeireMarketplaceValidatorBroker/v1")
    $secretBytes = [System.Security.Cryptography.ProtectedData]::Unprotect(
        $protectedBytes,
        $entropy,
        [System.Security.Cryptography.DataProtectionScope]::LocalMachine)
    $secret = [Text.Encoding]::UTF8.GetString($secretBytes)
    if ($secret.Length -lt 32 -or $secret.Length -gt 256 -or $secret -match '\s') {
        throw "The protected validator broker secret has an invalid shape."
    }

    $env:KEIRE_SUPABASE_URL = $SupabaseUrl.GetLeftPart([UriPartial]::Authority)
    $env:KEIRE_VALIDATOR_BROKER_SECRET = $secret
    $env:KEIRE_VALIDATOR_EXPECTED_FINGERPRINT_SHA256 = $ExpectedValidatorFingerprint
    $env:KEIRE_VALIDATOR_EXCHANGE_ROOT = (Resolve-Path -LiteralPath $ExchangeRoot).Path
    $env:KEIRE_VALIDATOR_WORKER_ID = $WorkerId
    & $Broker
    exit $LASTEXITCODE
}
finally {
    Remove-Item Env:KEIRE_VALIDATOR_BROKER_SECRET -ErrorAction SilentlyContinue
    if ($null -ne $secretBytes) {
        [Array]::Clear($secretBytes, 0, $secretBytes.Length)
    }
    [Array]::Clear($protectedBytes, 0, $protectedBytes.Length)
}
