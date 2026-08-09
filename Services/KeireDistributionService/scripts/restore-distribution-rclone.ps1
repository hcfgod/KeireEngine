[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $DestinationRoot,
    [Parameter(Mandatory = $true)]
    [string] $RclonePath,
    [Parameter(Mandatory = $true)]
    [string] $RcloneConfigPath,
    [Parameter(Mandatory = $true)]
    [string] $RemoteRoot,
    [string] $BackupId = '',
    [string] $PublisherPath = '',
    [string] $Dotnet = 'dotnet'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Executable([string] $Candidate, [string] $Description) {
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        return [IO.Path]::GetFullPath($Candidate)
    }
    $command = Get-Command $Candidate -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) {
        throw "$Description does not exist: '$Candidate'."
    }
    return $command.Source
}

function Invoke-PublisherValidate([string] $Root) {
    if (-not [string]::IsNullOrWhiteSpace($PublisherPath)) {
        & $PublisherPath validate --root $Root
    }
    else {
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
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Distribution validation failed for '$Root'."
    }
}

function Invoke-Rclone([string[]] $CommandArguments, [string] $Description) {
    & $rcloneExecutable @CommandArguments --config $rcloneConfig
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with rclone exit code $LASTEXITCODE."
    }
}

$rcloneExecutable = Resolve-Executable $RclonePath 'rclone'
$rcloneConfig = [IO.Path]::GetFullPath($RcloneConfigPath)
if (-not (Test-Path -LiteralPath $rcloneConfig -PathType Leaf)) {
    throw "The rclone configuration does not exist: '$rcloneConfig'."
}
if (-not [string]::IsNullOrWhiteSpace($PublisherPath)) {
    $PublisherPath = Resolve-Executable $PublisherPath 'The distribution publisher'
}
$remote = $RemoteRoot.Trim().TrimEnd('/')
if ($remote -notmatch '^[^:\r\n]+:.+$') {
    throw 'RemoteRoot must be a non-root rclone remote path such as remote:folder/backups.'
}
$destination = [IO.Path]::GetFullPath($DestinationRoot)
if (Test-Path -LiteralPath $destination) {
    if ((Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1)) {
        throw "The restore destination must be absent or empty: '$destination'."
    }
}
else {
    [IO.Directory]::CreateDirectory($destination) | Out-Null
}

$recordDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    ('keire-distribution-remote-restore-{0}' -f [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($recordDirectory) | Out-Null
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
try {
    if ([string]::IsNullOrWhiteSpace($BackupId)) {
        $latestPath = Join-Path $recordDirectory 'latest'
        Invoke-Rclone @('copyto', ("{0}/latest" -f $remote), $latestPath, '--checksum') `
            'Latest backup pointer download'
        $BackupId = (Get-Content -LiteralPath $latestPath -Raw).Trim()
    }
    if ($BackupId -notmatch '^backup-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{16}-[a-f0-9]{8}$') {
        throw 'The remote backup ID is invalid.'
    }

    $recordRemote = '{0}/records/{1}' -f $remote, $BackupId
    Invoke-Rclone @('copy', $recordRemote, $recordDirectory, '--checksum') `
        "Backup record download '$BackupId'"
    $recordPath = Join-Path $recordDirectory 'backup.json'
    $currentPath = Join-Path $recordDirectory 'current'
    if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $currentPath -PathType Leaf)) {
        throw "Remote backup record '$BackupId' is incomplete."
    }
    $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    $current = (Get-Content -LiteralPath $currentPath -Raw).Trim()
    if ($current -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$' -or
        [int] $record.schemaVersion -ne 1 -or
        -not [string]::Equals([string] $record.backupId, $BackupId, [StringComparison]::Ordinal) -or
        -not [string]::Equals([string] $record.snapshotId, $current, [StringComparison]::Ordinal)) {
        throw "Remote backup record '$BackupId' is invalid."
    }

    $localSnapshot = Join-Path (Join-Path $destination 'snapshots') $current
    [IO.Directory]::CreateDirectory($localSnapshot) | Out-Null
    $remoteSnapshot = '{0}/snapshots/{1}' -f $remote, $current
    Invoke-Rclone @('copy', $remoteSnapshot, $localSnapshot, '--checksum') `
        "Snapshot restore '$current'"
    Invoke-Rclone @('check', $remoteSnapshot, $localSnapshot, '--checksum', '--one-way') `
        "Downloaded snapshot verification '$current'"
    Copy-Item -LiteralPath $currentPath -Destination (Join-Path $destination 'current')
    Invoke-PublisherValidate $destination
}
catch {
    throw "Remote distribution restore failed in '$destination'. The incomplete destination was retained for diagnosis. $($_.Exception.Message)"
}
finally {
    $stopwatch.Stop()
    Remove-Item -LiteralPath $recordDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Validated off-machine distribution restore '$BackupId': $destination"
Write-Host ('Recovery time: {0:c}' -f $stopwatch.Elapsed)
