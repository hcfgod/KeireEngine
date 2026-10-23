[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Publisher,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [Parameter(Mandatory = $true)] [string]$ValidatorProtectedKey,
    [Parameter(Mandatory = $true)] [string]$PublicationProtectedKey,
    [string]$CurrentTrustBundle = "",
    [ValidateSet("supabase", "mysql")] [string]$Backend = "supabase"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "marketplace-public-key-registration.ps1")

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
$sql = New-MarketplacePublicKeyRegistrationSql -Validator $validator -Publication $publication -Backend $Backend
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
Write-Host "Public-key registration SQL targets the $Backend backend. Review it before applying it to that backend."
Write-Host "Release the candidate Hub trust bundle before activating the publication key."
Write-Host "Archive the ACL-protected PEM files offline; never place either private key in the repository or a platform database."
