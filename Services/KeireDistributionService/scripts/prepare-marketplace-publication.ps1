[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F-]{36}$')]
    [string]$ProductId,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F-]{36}$')]
    [string]$VersionId,

    [Parameter(Mandatory = $true)]
    [string]$Artifact,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string]$ManifestSha256,

    [Parameter(Mandatory = $true)]
    [string]$PrivateKey,

    [Parameter(Mandatory = $true)]
    [string]$PublicKey,

    [Parameter(Mandatory = $true)]
    [string]$Publisher,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [long]$Sequence = 1,

    [int]$ValidityDays = 365
)

$ErrorActionPreference = 'Stop'

if ($Sequence -lt 1 -or $ValidityDays -lt 1 -or $ValidityDays -gt 3650) {
    throw 'Sequence and ValidityDays are outside the supported publication policy.'
}
foreach ($path in @($Artifact, $PrivateKey, $PublicKey, $Publisher)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required marketplace publication input is missing: $path"
    }
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to replace an existing marketplace publication directory: $OutputDirectory"
}

$artifactPath = (Resolve-Path -LiteralPath $Artifact).Path
$artifactInfo = Get-Item -LiteralPath $artifactPath
if ($artifactInfo.Extension -cne '.keireassetpackage' -or $artifactInfo.Length -lt 1) {
    throw 'Artifact must be a non-empty .keireassetpackage file.'
}
$artifactSha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
$trustedKey = Get-Content -LiteralPath $PublicKey -Raw | ConvertFrom-Json
if ($trustedKey.schemaVersion -ne 1 -or $trustedKey.algorithm -cne 'Ed25519' -or
    $trustedKey.keyId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') {
    throw 'The marketplace public key document is invalid.'
}

$output = [IO.Path]::GetFullPath($OutputDirectory)
$documentDirectory = Join-Path $output 'signed-source\catalogs\stable\marketplace'
New-Item -ItemType Directory -Path $documentDirectory -Force | Out-Null
$documentPath = Join-Path $documentDirectory 'x86_64.json'
$expiry = (Get-Date).ToUniversalTime().AddDays($ValidityDays).ToString('o')
$releaseStoragePath = '{0}/{1}/{2}.keireassetpackage' -f $ProductId.ToLowerInvariant(),
    $VersionId.ToLowerInvariant(), $artifactSha256
$document = [ordered]@{
    schemaVersion = 1
    keyId = $trustedKey.keyId
    sequence = $Sequence
    expiresAt = $expiry
    productId = $ProductId.ToLowerInvariant()
    versionId = $VersionId.ToLowerInvariant()
    artifactSha256 = $artifactSha256
    artifactSizeBytes = $artifactInfo.Length
    manifestSha256 = $ManifestSha256
    releaseStoragePath = $releaseStoragePath
}
$documentText = $document | ConvertTo-Json -Compress -Depth 8
[IO.File]::WriteAllText($documentPath, $documentText, [Text.UTF8Encoding]::new($false))

$sourceRoot = Join-Path $output 'signed-source'
$signaturesPath = Join-Path $sourceRoot 'signatures.json'
& $Publisher sign --source $sourceRoot --output $signaturesPath --private-key $PrivateKey `
    --minimum-sequence $Sequence --minimum-validity-hours 1
if ($LASTEXITCODE -ne 0) { throw 'Offline marketplace signing failed.' }
& $Publisher verify --source $sourceRoot --signatures $signaturesPath --public-key $PublicKey `
    --minimum-sequence $Sequence --minimum-validity-hours 1
if ($LASTEXITCODE -ne 0) { throw 'Offline marketplace signature verification failed.' }

$signatures = Get-Content -LiteralPath $signaturesPath -Raw | ConvertFrom-Json
$entry = @($signatures.documents | Where-Object { $_.path -ceq 'catalogs/stable/marketplace/x86_64.json' })
if ($entry.Count -ne 1 -or $entry[0].keyId -cne $trustedKey.keyId -or $entry[0].sequence -ne $Sequence) {
    throw 'The verified signature manifest does not bind the expected publication document.'
}
$envelope = [ordered]@{
    schemaVersion = 1
    document = $documentText
    signature = [ordered]@{
        algorithm = 'ed25519'
        keyId = $entry[0].keyId
        value = $entry[0].signature
        sequence = $Sequence
        expiresAt = $expiry
    }
}
$envelopePath = Join-Path $output 'marketplace-publication.signed.json'
[IO.File]::WriteAllText(
    $envelopePath,
    ($envelope | ConvertTo-Json -Compress -Depth 8),
    [Text.UTF8Encoding]::new($false))

Write-Host "Prepared and verified offline marketplace publication '$envelopePath'."
Write-Output $envelopePath

