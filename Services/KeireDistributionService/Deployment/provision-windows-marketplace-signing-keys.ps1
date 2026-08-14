[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Publisher,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [Parameter(Mandatory = $true)] [string]$ValidatorProtectedKey,
    [Parameter(Mandatory = $true)] [string]$PublicationProtectedKey,
    [string]$CurrentTrustBundle = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Publisher -PathType Leaf)) { throw "The distribution publisher is missing." }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Choose a new output directory for this key ceremony: $output" }
[IO.Directory]::CreateDirectory($output) | Out-Null

$validatorPem = Join-Path $output "marketplace-validator-attestation-private.pem"
$validatorPublic = Join-Path $output "marketplace-validator-attestation-public.json"
$publicationPem = Join-Path $output "marketplace-publication-private.pem"
$publicationPublic = Join-Path $output "marketplace-publication-public.json"
& $Publisher generate-key --private-key $validatorPem --public-key $validatorPublic
if ($LASTEXITCODE -ne 0) { throw "Validator attestation key generation failed." }
& $Publisher generate-key --private-key $publicationPem --public-key $publicationPublic
if ($LASTEXITCODE -ne 0) { throw "Publication key generation failed." }

$protector = Join-Path $PSScriptRoot "protect-windows-marketplace-secret.ps1"
& $protector -Output $ValidatorProtectedKey -Purpose ValidatorAttestationPrivateKey `
    -InputPrivateKeyPem $validatorPem
& $protector -Output $PublicationProtectedKey -Purpose PublicationPrivateKey `
    -InputPrivateKeyPem $publicationPem

$validator = Get-Content -LiteralPath $validatorPublic -Raw | ConvertFrom-Json
$publication = Get-Content -LiteralPath $publicationPublic -Raw | ConvertFrom-Json
foreach ($document in @($validator, $publication)) {
    if ($document.schemaVersion -ne 1 -or $document.algorithm -cne "Ed25519" -or
        $document.keyId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' -or
        $document.publicKey -cnotmatch '^[A-Za-z0-9+/]{43}=$' -or
        $document.fingerprint -cnotmatch '^sha256:[0-9a-f]{64}$') {
        throw "A generated public key failed its identity contract."
    }
}

$sql = @"
begin;
insert into public.marketplace_validator_attestation_keys
    (key_id, algorithm, public_key_base64, fingerprint, active)
values
    ('$($validator.keyId)', 'ed25519', '$($validator.publicKey)', '$($validator.fingerprint)', true);
insert into public.marketplace_signature_keys
    (key_id, algorithm, public_key_base64, fingerprint, active)
values
    ('$($publication.keyId)', 'ed25519', '$($publication.publicKey)', '$($publication.fingerprint)', true);
commit;
"@
$sqlPath = Join-Path $output "register-marketplace-public-keys.sql"
[IO.File]::WriteAllText($sqlPath, $sql, [Text.UTF8Encoding]::new($false))

if (-not [string]::IsNullOrWhiteSpace($CurrentTrustBundle)) {
    $bundle = Get-Content -LiteralPath $CurrentTrustBundle -Raw | ConvertFrom-Json
    if ($bundle.schemaVersion -ne 1 -or @($bundle.keys).Count -lt 1 -or @($bundle.keys).Count -ge 8) {
        throw "The current Marketplace trust bundle is invalid or full."
    }
    $candidate = [ordered]@{ schemaVersion = 1; keys = @($bundle.keys) + @($publication) }
    $candidatePath = Join-Path $output "trusted-marketplace-keys.candidate.json"
    [IO.File]::WriteAllText(
        $candidatePath,
        ($candidate | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
}

Write-Host "Generated two independent Ed25519 identities and protected their online copies with machine DPAPI."
Write-Host "Release the candidate Hub trust bundle before activating the publication key in Supabase."
Write-Host "Archive the ACL-protected PEM files offline; never place either private key in the repository or Supabase."
