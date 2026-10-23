function Assert-MarketplacePublicKeyDocument
{
    param([Parameter(Mandatory)] [object] $Document)

    if ($Document.schemaVersion -ne 1 -or $Document.algorithm -cne 'Ed25519' -or
        $Document.keyId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' -or
        $Document.publicKey -cnotmatch '^[A-Za-z0-9+/]{43}=$' -or
        $Document.fingerprint -cnotmatch '^sha256:[0-9a-f]{64}$')
    {
        throw 'A public key failed its identity contract.'
    }
    $bytes = [Convert]::FromBase64String($Document.publicKey)
    if ($bytes.Length -ne 32 -or [Convert]::ToBase64String($bytes) -cne $Document.publicKey)
    {
        throw 'The Ed25519 public key must use canonical 32-byte base64.'
    }
    $hash = [Security.Cryptography.SHA256]::Create()
    try
    {
        $fingerprint = 'sha256:' + [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    }
    finally
    {
        $hash.Dispose()
    }
    if ($fingerprint -cne $Document.fingerprint)
    {
        throw 'The public key fingerprint does not match its bytes.'
    }
}

# Pure SQL generation: accepts public documents only, never creates keys or connects to a database.
function New-MarketplacePublicKeyRegistrationSql
{
    param(
        [Parameter(Mandatory)] [object] $Validator,
        [Parameter(Mandatory)] [object] $Publication,
        [ValidateSet('supabase', 'mysql')] [string] $Backend = 'supabase'
    )

    Assert-MarketplacePublicKeyDocument $Validator
    Assert-MarketplacePublicKeyDocument $Publication
    if ($Validator.keyId -ceq $Publication.keyId -or $Validator.publicKey -ceq $Publication.publicKey)
    {
        throw 'Validator and publication identities must be independent.'
    }
    if ($Backend -eq 'mysql')
    {
        return @"
-- MySQL local platform only. Register public keys; private keys remain on the worker hosts.
USE keire_platform;
START TRANSACTION;
INSERT INTO marketplace_validator_attestation_keys
    (key_id, algorithm, public_key_base64, fingerprint, active, valid_from, created_at)
VALUES
    ('$($Validator.keyId)', 'ed25519', '$($Validator.publicKey)', '$($Validator.fingerprint)', TRUE, UTC_TIMESTAMP(6), UTC_TIMESTAMP(6));
INSERT INTO marketplace_signature_keys
    (key_id, algorithm, public_key_base64, fingerprint, active, valid_from, created_at)
VALUES
    ('$($Publication.keyId)', 'ed25519', '$($Publication.publicKey)', '$($Publication.fingerprint)', TRUE, UTC_TIMESTAMP(6), UTC_TIMESTAMP(6));
COMMIT;
"@
    }
    return @"
-- Supabase PostgreSQL only. Register public keys; private keys remain on the worker hosts.
begin;
insert into public.marketplace_validator_attestation_keys
    (key_id, algorithm, public_key_base64, fingerprint, active)
values
    ('$($Validator.keyId)', 'ed25519', '$($Validator.publicKey)', '$($Validator.fingerprint)', true);
insert into public.marketplace_signature_keys
    (key_id, algorithm, public_key_base64, fingerprint, active)
values
    ('$($Publication.keyId)', 'ed25519', '$($Publication.publicKey)', '$($Publication.fingerprint)', true);
commit;
"@
}
