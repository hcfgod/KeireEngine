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
    [switch] $Activate,
    [string] $Dotnet = 'dotnet'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serviceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$arguments = @(
    'publish',
    '--source', [IO.Path]::GetFullPath($Source),
    '--root', [IO.Path]::GetFullPath($DistributionRoot),
    '--snapshot', $SnapshotId,
    '--public-key', [IO.Path]::GetFullPath($PublicKey)
)
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
