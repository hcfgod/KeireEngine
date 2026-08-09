[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $DistributionRoot,
    [Parameter(Mandatory = $true)]
    [string] $RclonePath,
    [Parameter(Mandatory = $true)]
    [string] $RcloneConfigPath,
    [Parameter(Mandatory = $true)]
    [string] $RemoteRoot,
    [string] $PublisherPath = '',
    [string] $LogPath = '',
    [string] $StatusPath = '',
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
    & $rcloneExecutable @CommandArguments @rcloneCommonArguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with rclone exit code $LASTEXITCODE."
    }
}

function Write-OperationalLog([string] $Level, [string] $Message) {
    $line = '{0} [{1}] {2}' -f ([DateTime]::UtcNow.ToString('o')), $Level, $Message
    Write-Host $line
    if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
        Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8
    }
}

function Write-BackupStatus([string] $Outcome, [string] $BackupId, [string] $SnapshotId, [string] $Message) {
    if ([string]::IsNullOrWhiteSpace($StatusPath)) {
        return
    }
    $document = [ordered]@{
        schemaVersion = 1
        lastAttemptAtUtc = [DateTime]::UtcNow.ToString('o')
        outcome = $Outcome
        backupId = $BackupId
        snapshotId = $SnapshotId
        message = $Message
    }
    $temporaryStatus = '{0}.tmp-{1}' -f $StatusPath, [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllText(
            $temporaryStatus,
            ($document | ConvertTo-Json) + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporaryStatus -Destination $StatusPath -Force
    }
    finally {
        Remove-Item -LiteralPath $temporaryStatus -Force -ErrorAction SilentlyContinue
    }
}

function Read-CurrentPointer([string] $Root) {
    $current = (Get-Content -LiteralPath (Join-Path $Root 'current') -Raw).Trim()
    if ($current -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
        throw 'The distribution current pointer is invalid.'
    }
    return $current
}

$source = [IO.Path]::GetFullPath($DistributionRoot)
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "The distribution root does not exist: '$source'."
}
$snapshotsRoot = Join-Path $source 'snapshots'
if (-not (Test-Path -LiteralPath $snapshotsRoot -PathType Container)) {
    throw "The distribution snapshots directory does not exist: '$snapshotsRoot'."
}
$rcloneExecutable = Resolve-Executable $RclonePath 'rclone'
$rcloneConfig = [IO.Path]::GetFullPath($RcloneConfigPath)
if (-not (Test-Path -LiteralPath $rcloneConfig -PathType Leaf)) {
    throw "The rclone configuration does not exist: '$rcloneConfig'."
}
$remote = $RemoteRoot.Trim().TrimEnd('/')
if ($remote -notmatch '^[^:\r\n]+:.+$') {
    throw 'RemoteRoot must be a non-root rclone remote path such as remote:folder/backups.'
}
if (-not [string]::IsNullOrWhiteSpace($PublisherPath)) {
    $PublisherPath = Resolve-Executable $PublisherPath 'The distribution publisher'
}
foreach ($path in @($LogPath, $StatusPath)) {
    if (-not [string]::IsNullOrWhiteSpace($path)) {
        $parent = Split-Path -Parent ([IO.Path]::GetFullPath($path))
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
}
if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = [IO.Path]::GetFullPath($LogPath)
}
if (-not [string]::IsNullOrWhiteSpace($StatusPath)) {
    $StatusPath = [IO.Path]::GetFullPath($StatusPath)
}
$rcloneCommonArguments = @('--config', $rcloneConfig, '--log-level', 'INFO')
if (-not [string]::IsNullOrWhiteSpace($LogPath)) {
    $rcloneCommonArguments += @('--log-file', $LogPath)
}

$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$recordDirectory = ''
$backupId = ''
$current = ''
try {
    $stable = $false
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        Invoke-PublisherValidate $source
        $currentBeforeUpload = Read-CurrentPointer $source
        $snapshotDirectories = @(Get-ChildItem -LiteralPath $snapshotsRoot -Directory -Force |
                Where-Object { $_.Name -match '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$' } |
                Sort-Object Name)
        if (-not ($snapshotDirectories.Name -contains $currentBeforeUpload)) {
            throw "The active snapshot directory is missing: '$currentBeforeUpload'."
        }

        foreach ($snapshot in $snapshotDirectories) {
            $snapshotRemote = '{0}/snapshots/{1}' -f $remote, $snapshot.Name
            Invoke-Rclone @('copy', $snapshot.FullName, $snapshotRemote, '--checksum', '--immutable') `
                "Immutable snapshot upload '$($snapshot.Name)'"
        }

        $currentAfterUpload = Read-CurrentPointer $source
        if ($currentBeforeUpload -eq $currentAfterUpload) {
            $current = $currentAfterUpload
            $stable = $true
            break
        }
        Write-OperationalLog 'WARN' 'The active snapshot changed during backup; retrying from a fresh validation.'
    }
    if (-not $stable) {
        throw 'The active snapshot changed during all three backup attempts.'
    }

    $currentSnapshot = Join-Path $snapshotsRoot $current
    Invoke-Rclone @('check', $currentSnapshot, ("{0}/snapshots/{1}" -f $remote, $current), '--checksum',
        '--one-way') "Remote snapshot verification '$current'"

    $createdAt = [DateTime]::UtcNow
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $fingerprint = ([BitConverter]::ToString(
                $hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($current)))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $hasher.Dispose()
    }
    $backupId = 'backup-{0}-{1}-{2}' -f $createdAt.ToString('yyyyMMddTHHmmssZ'), $fingerprint.Substring(0, 16),
        [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $recordDirectory = Join-Path ([IO.Path]::GetTempPath()) `
        ('keire-distribution-record-{0}' -f [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($recordDirectory) | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $recordDirectory 'current'),
        $current + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    $record = [ordered]@{
        schemaVersion = 1
        backupId = $backupId
        createdAtUtc = $createdAt.ToString('o')
        snapshotId = $current
    }
    [IO.File]::WriteAllText(
        (Join-Path $recordDirectory 'backup.json'),
        ($record | ConvertTo-Json) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    $recordRemote = '{0}/records/{1}' -f $remote, $backupId
    Invoke-Rclone @('copy', $recordDirectory, $recordRemote, '--checksum', '--immutable') `
        "Immutable backup record upload '$backupId'"
    Invoke-Rclone @('check', $recordDirectory, $recordRemote, '--checksum', '--one-way') `
        "Remote backup record verification '$backupId'"
    $latestPath = Join-Path $recordDirectory 'latest'
    [IO.File]::WriteAllText($latestPath, $backupId + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    Invoke-Rclone @('copyto', $latestPath, ("{0}/latest" -f $remote), '--checksum') `
        'Latest verified backup pointer update'

    $stopwatch.Stop()
    Write-BackupStatus 'succeeded' $backupId $current `
        ('Verified remote backup in {0:c}.' -f $stopwatch.Elapsed)
    Write-OperationalLog 'INFO' "Verified off-machine distribution backup '$backupId' for snapshot '$current'."
    Write-OperationalLog 'INFO' ('Backup elapsed time: {0:c}' -f $stopwatch.Elapsed)
}
catch {
    $failure = $_
    $stopwatch.Stop()
    try {
        Write-BackupStatus 'failed' $backupId $current $failure.Exception.Message
        Write-OperationalLog 'ERROR' $failure.Exception.Message
    }
    catch {
        Write-Warning "Writing the backup failure status also failed: $($_.Exception.Message)"
    }
    throw $failure
}
finally {
    if (-not [string]::IsNullOrWhiteSpace($recordDirectory)) {
        Remove-Item -LiteralPath $recordDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}
