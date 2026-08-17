[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $DistributionRoot,
    [Parameter(Mandatory = $true)]
    [string] $DestinationRoot,
    [string] $BackupId = '',
    [string] $Dotnet = 'dotnet'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-PublisherValidate([string] $Root) {
    $serviceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $publisher = @(
        (Join-Path $serviceRoot 'tools\publisher\KeireDistributionPublisher.exe'),
        (Join-Path $serviceRoot 'tools\publisher\KeireDistributionPublisher')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if ($publisher) {
        & $publisher validate --root $Root
    }
    else {
        $project = Join-Path $serviceRoot 'Source\KeireDistributionPublisher\KeireDistributionPublisher.csproj'
        & $Dotnet run --project $project --configuration Release -- validate --root $Root
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Distribution validation failed for '$Root'."
    }
}

$source = [IO.Path]::GetFullPath($DistributionRoot)
$destination = [IO.Path]::GetFullPath($DestinationRoot)
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "The distribution root does not exist: '$source'."
}
[IO.Directory]::CreateDirectory($destination) | Out-Null
$sourcePrefix = $source.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$destinationPrefix = $destination.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($sourcePrefix.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationPrefix.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The backup destination and distribution root may not contain one another.'
}

Invoke-PublisherValidate $source
$current = (Get-Content -LiteralPath (Join-Path $source 'current') -Raw).Trim()
if ($current -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
    throw 'The distribution current pointer is invalid.'
}
if ([string]::IsNullOrWhiteSpace($BackupId)) {
    $BackupId = 'keire-distribution-{0}-{1}' -f ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')), $current
}
if ($BackupId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
    throw 'The backup ID is invalid.'
}

$final = Join-Path $destination $BackupId
if (Test-Path -LiteralPath $final) {
    throw "The immutable backup already exists: '$final'."
}
$temporary = Join-Path $destination ('.{0}.tmp-{1}' -f $BackupId, [Guid]::NewGuid().ToString('N'))
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
try {
    [IO.Directory]::CreateDirectory($temporary) | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'snapshots') -Destination $temporary -Recurse
    Copy-Item -LiteralPath (Join-Path $source 'current') -Destination $temporary
    Invoke-PublisherValidate $temporary
    Move-Item -LiteralPath $temporary -Destination $final
}
catch {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
    throw
}
finally {
    $stopwatch.Stop()
}

Write-Host "Validated immutable distribution backup: $final"
Write-Host ('Backup elapsed time: {0:c}' -f $stopwatch.Elapsed)
