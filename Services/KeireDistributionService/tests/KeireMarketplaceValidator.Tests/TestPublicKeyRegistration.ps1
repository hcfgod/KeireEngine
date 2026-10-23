$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../../Deployment/marketplace-public-key-registration.ps1')

function New-PublicFixture
{
    param([byte] $Offset, [string] $KeyId)
    $bytes = [byte[]](0..31 | ForEach-Object { $_ + $Offset })
    $hash = [Security.Cryptography.SHA256]::Create()
    try
    {
        $fingerprint = 'sha256:' + [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $hash.Dispose()
    }
    return [pscustomobject]@{
        schemaVersion = 1
        algorithm = 'Ed25519'
        keyId = $KeyId
        publicKey = [Convert]::ToBase64String($bytes)
        fingerprint = $fingerprint
    }
}

function Assert-Check
{
    param([bool] $Condition, [string] $Message)
    if (-not $Condition)
    {
        throw $Message
    }
    $script:checks++
}

function Assert-Rejected
{
    param([scriptblock] $Action)
    $rejected = $false
    try
    {
        & $Action | Out-Null
    }
    catch
    {
        $rejected = $true
    }
    Assert-Check $rejected 'Invalid public-key registration was accepted.'
}

$checks = 0
$validator = New-PublicFixture 0 'validator-test'
$publication = New-PublicFixture 32 'publication-test'
$supabase = New-MarketplacePublicKeyRegistrationSql $validator $publication
$mysql = New-MarketplacePublicKeyRegistrationSql $validator $publication -Backend mysql
Assert-Check ($supabase.Contains('public.marketplace_validator_attestation_keys')) 'Default must remain Supabase.'
Assert-Check ($supabase.Contains('public.marketplace_signature_keys')) 'Missing Supabase publication table.'
Assert-Check (-not $supabase.Contains('UTC_TIMESTAMP')) 'MySQL syntax leaked into Supabase SQL.'
Assert-Check ($mysql.Contains('USE keire_platform;')) 'Missing explicit MySQL database.'
Assert-Check ($mysql.Contains('START TRANSACTION;') -and $mysql.Contains('COMMIT;')) 'Missing transaction.'
Assert-Check (-not $mysql.Contains('public.')) 'PostgreSQL schema leaked into MySQL SQL.'
Assert-Check (([regex]::Matches($mysql, 'valid_from, created_at')).Count -eq 2) 'MySQL required timestamps missing.'
Assert-Check (([regex]::Matches($mysql, 'UTC_TIMESTAMP\(6\)')).Count -eq 4) 'MySQL UTC timestamps missing.'
Assert-Check (([regex]::Matches($mysql, 'INSERT INTO')).Count -eq 2) 'Expected exactly two registrations.'
Assert-Check ($mysql -notmatch 'UPSERT|IGNORE|ON DUPLICATE') 'Registration must not overwrite existing trust.'
Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $publication -Backend unknown }
Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $validator }
foreach ($invalid in @(
    @{ property = 'schemaVersion'; value = 2 },
    @{ property = 'algorithm'; value = 'RSA' },
    @{ property = 'keyId'; value = "bad'; DROP TABLE users; --" },
    @{ property = 'publicKey'; value = 'invalid' },
    @{ property = 'fingerprint'; value = ('sha256:' + ('0' * 64)) }
))
{
    $broken = New-PublicFixture 32 'publication-test'
    $broken.($invalid.property) = $invalid.value
    Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $broken -Backend mysql }
}
$nonCanonical = New-PublicFixture 32 'publication-test'
$alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
$lastCharacter = $alphabet.IndexOf($nonCanonical.publicKey[42])
$nonCanonical.publicKey = $nonCanonical.publicKey.Substring(0, 42) + $alphabet[$lastCharacter + 1] + '='
Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $nonCanonical }
$sameBytes = New-PublicFixture 0 'another-id'
Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $sameBytes }
$sameId = New-PublicFixture 32 'validator-test'
Assert-Rejected { New-MarketplacePublicKeyRegistrationSql $validator $sameId }
# Inspect the ceremony without executing it: these tests never generate or protect a private key.
$ceremonyPath = Join-Path $PSScriptRoot '../../Deployment/provision-windows-marketplace-signing-keys.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($ceremonyPath, [ref]$tokens, [ref]$parseErrors)
Assert-Check ($parseErrors.Count -eq 0) 'Ceremony syntax failed.'
$backendParameter = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Backend' }
Assert-Check ($backendParameter.DefaultValue.Value -eq 'supabase') 'Ceremony changed its default backend.'
$ceremony = Get-Content -LiteralPath $ceremonyPath -Raw
Assert-Check ($ceremony.Contains('-Publication $publication -Backend $Backend')) 'Ceremony must pass backend explicitly.'
Write-Output "Public-key registration: $checks checks passed; no keys generated and no database connections."
