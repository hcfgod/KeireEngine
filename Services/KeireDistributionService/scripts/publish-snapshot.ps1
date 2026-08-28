[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Source,
    [Parameter(Mandatory = $true)]
    [string] $DistributionRoot,
    [Parameter(Mandatory = $true)]
    [string] $SnapshotId,
    [Parameter(Mandatory = $true)]
    [string] $PublicKey,
    [long] $MinimumSequence = 0,
    [double] $MinimumValidityHours = 24.0,
    [switch] $Activate,
    [string] $Dotnet = 'dotnet'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$minimumSequenceProvided = $PSBoundParameters.ContainsKey('MinimumSequence')
if ($minimumSequenceProvided -and $MinimumSequence -lt 1) {
    throw 'Minimum sequence must be at least one.'
}
if ($Activate -and -not $minimumSequenceProvided) {
    throw 'Activation requires an explicit minimum sequence of at least one.'
}
if ([double]::IsNaN($MinimumValidityHours) -or [double]::IsInfinity($MinimumValidityHours) -or
    $MinimumValidityHours -lt 0.0 -or $MinimumValidityHours -gt [TimeSpan]::FromDays(3650).TotalHours) {
    throw 'Minimum validity hours must be finite and between 0 and 87600.'
}

$serviceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$arguments = @(
    'publish',
    '--source', [IO.Path]::GetFullPath($Source),
    '--root', [IO.Path]::GetFullPath($DistributionRoot),
    '--snapshot', $SnapshotId,
    '--public-key', [IO.Path]::GetFullPath($PublicKey),
    '--minimum-validity-hours', $MinimumValidityHours.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
)
if ($minimumSequenceProvided) {
    $arguments += '--minimum-sequence', $MinimumSequence.ToString([Globalization.CultureInfo]::InvariantCulture)
}
if ($Activate) {
    $arguments += '--activate'
}

$publisherCandidates = @(
    (Join-Path $serviceRoot 'tools\publisher\KeireDistributionPublisher.exe'),
    (Join-Path $serviceRoot 'tools\publisher\KeireDistributionPublisher')
)
$publisher = $publisherCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if ($publisher) {
    & $publisher @arguments
}
else {
    $project = Join-Path $serviceRoot 'Source\KeireDistributionPublisher\KeireDistributionPublisher.csproj'
    & $Dotnet run --project $project --configuration Release -- @arguments
}
if ($LASTEXITCODE -ne 0) {
    throw 'Distribution snapshot publishing failed.'
}
