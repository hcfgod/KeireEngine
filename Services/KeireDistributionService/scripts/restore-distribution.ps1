[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BackupRoot,
    [string] $DestinationRoot = '',
    [switch] $ValidateOnly,
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

$backup = [IO.Path]::GetFullPath($BackupRoot)
if (-not (Test-Path -LiteralPath $backup -PathType Container)) {
    throw "The distribution backup does not exist: '$backup'."
}
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
Invoke-PublisherValidate $backup
if ($ValidateOnly) {
    $stopwatch.Stop()
    Write-Host "Distribution restore drill validation passed: $backup"
    Write-Host ('Validation elapsed time: {0:c}' -f $stopwatch.Elapsed)
    exit 0
}
if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    throw 'DestinationRoot is required unless ValidateOnly is used.'
}

$destination = [IO.Path]::GetFullPath($DestinationRoot)
$backupPrefix = $backup.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$destinationPrefix = $destination.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($backupPrefix.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationPrefix.StartsWith($backupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The restore destination and backup root may not contain one another.'
}
if (Test-Path -LiteralPath $destination) {
    if ((Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1)) {
        throw "The restore destination must be absent or empty: '$destination'."
    }
}
else {
    [IO.Directory]::CreateDirectory($destination) | Out-Null
}

try {
    Copy-Item -LiteralPath (Join-Path $backup 'snapshots') -Destination $destination -Recurse
    Copy-Item -LiteralPath (Join-Path $backup 'current') -Destination $destination
    Invoke-PublisherValidate $destination
}
catch {
    throw "Distribution restore failed in '$destination'. The incomplete destination was retained for diagnosis. $($_.Exception.Message)"
}
finally {
    $stopwatch.Stop()
}

Write-Host "Validated distribution restore: $destination"
Write-Host ('Recovery time: {0:c}' -f $stopwatch.Elapsed)
