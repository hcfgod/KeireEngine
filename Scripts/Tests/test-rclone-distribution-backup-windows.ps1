$ErrorActionPreference = 'Stop'
$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')

function Assert-Throws([scriptblock] $Action, [string] $Message) {
    try {
        & $Action
    }
    catch {
        return
    }
    throw "$Message did not throw."
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) `
    ('keire-rclone-backup-test-' + [Guid]::NewGuid().ToString('N'))
$distribution = Join-Path $fixture 'distribution'
$remoteStorage = Join-Path $fixture 'remote'
$restore = Join-Path $fixture 'restore'
$hostRoot = Join-Path $fixture 'host'
$fakeRclonePython = Join-Path $fixture 'fake-rclone.py'
$fakeRclone = Join-Path $fixture 'rclone.cmd'
$fakePublisherPython = Join-Path $fixture 'fake-publisher.py'
$fakePublisher = Join-Path $fixture 'publisher.cmd'
$config = Join-Path $fixture 'rclone.conf'
$python = (Get-Command python -ErrorAction Stop).Source
$previousFakeRoot = $env:KEIRE_TEST_RCLONE_ROOT
try {
    [IO.Directory]::CreateDirectory((Join-Path $distribution 'snapshots\snapshot-a')) | Out-Null
    [IO.Directory]::CreateDirectory($remoteStorage) | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $distribution 'current'), "snapshot-a`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText(
        (Join-Path $distribution 'snapshots\snapshot-a\payload.bin'),
        'immutable-payload', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($config, "[test-remote]`ntype = drive`n", [Text.UTF8Encoding]::new($false))

    @'
import filecmp
import os
from pathlib import Path
import shutil
import sys

remote_root = Path(os.environ["KEIRE_TEST_RCLONE_ROOT"])
arguments = sys.argv[1:]
command = arguments.pop(0)
value_flags = {"--config", "--log-file", "--log-level"}
positionals = []
immutable = "--immutable" in arguments
index = 0
while index < len(arguments):
    value = arguments[index]
    if value.startswith("--"):
        index += 2 if value in value_flags else 1
    else:
        positionals.append(value)
        index += 1

def resolve(value: str) -> Path:
    prefix = "test-remote:"
    if value.startswith(prefix):
        relative = value[len(prefix):].replace("/", os.sep).lstrip(os.sep)
        return remote_root / relative
    return Path(value)

def copy_directory(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"missing source directory: {source}")
    for item in source.rglob("*"):
        if not item.is_file():
            continue
        relative = item.relative_to(source)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and immutable and not filecmp.cmp(item, target, shallow=False):
            raise RuntimeError(f"immutable destination changed: {target}")
        if not target.exists() or not filecmp.cmp(item, target, shallow=False):
            shutil.copy2(item, target)

try:
    if command == "listremotes":
        print("test-remote:")
    elif command == "copy":
        copy_directory(resolve(positionals[0]), resolve(positionals[1]))
    elif command == "copyto":
        source = resolve(positionals[0])
        destination = resolve(positionals[1])
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    elif command == "check":
        source = resolve(positionals[0])
        destination = resolve(positionals[1])
        for item in source.rglob("*"):
            if item.is_file():
                target = destination / item.relative_to(source)
                if not target.is_file() or not filecmp.cmp(item, target, shallow=False):
                    raise RuntimeError(f"check failed: {item}")
    else:
        raise RuntimeError(f"unsupported fake rclone command: {command}")
except Exception as error:
    print(error, file=sys.stderr)
    sys.exit(3)
'@ | Set-Content -LiteralPath $fakeRclonePython -Encoding UTF8
    @'
from pathlib import Path
import sys

if sys.argv[1:3] != ["validate", "--root"]:
    sys.exit(2)
root = Path(sys.argv[3])
current = (root / "current").read_text(encoding="utf-8").strip()
if not current or not (root / "snapshots" / current).is_dir():
    sys.exit(2)
'@ | Set-Content -LiteralPath $fakePublisherPython -Encoding UTF8
    [IO.File]::WriteAllText(
        $fakeRclone,
        "@echo off`r`n`"$python`" `"$fakeRclonePython`" %*`r`nexit /b %ERRORLEVEL%`r`n",
        [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText(
        $fakePublisher,
        "@echo off`r`n`"$python`" `"$fakePublisherPython`" %*`r`nexit /b %ERRORLEVEL%`r`n",
        [Text.Encoding]::ASCII)
    $env:KEIRE_TEST_RCLONE_ROOT = $remoteStorage

    $backupScript = Join-Path $Root `
        'Services\KeireDistributionService\scripts\backup-distribution-rclone.ps1'
    $restoreScript = Join-Path $Root `
        'Services\KeireDistributionService\scripts\restore-distribution-rclone.ps1'
    $logPath = Join-Path $fixture 'backup.log'
    $statusPath = Join-Path $fixture 'backup-status.json'
    & $backupScript -DistributionRoot $distribution -RclonePath $fakeRclone `
        -RcloneConfigPath $config -RemoteRoot 'test-remote:backups' -PublisherPath $fakePublisher `
        -LogPath $logPath -StatusPath $statusPath

    $remoteSnapshot = Join-Path $remoteStorage 'backups\snapshots\snapshot-a\payload.bin'
    if (-not (Test-Path -LiteralPath $remoteSnapshot -PathType Leaf)) {
        throw 'The first backup did not upload the immutable snapshot.'
    }
    $firstRecordCount = @(Get-ChildItem -LiteralPath (Join-Path $remoteStorage 'backups\records') `
            -Directory).Count
    if ($firstRecordCount -ne 1) {
        throw 'The first backup did not create exactly one immutable record.'
    }
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    if ($status.outcome -ne 'succeeded' -or $status.snapshotId -ne 'snapshot-a') {
        throw 'The successful backup status document is incorrect.'
    }

    & $backupScript -DistributionRoot $distribution -RclonePath $fakeRclone `
        -RcloneConfigPath $config -RemoteRoot 'test-remote:backups' -PublisherPath $fakePublisher
    $secondRecordCount = @(Get-ChildItem -LiteralPath (Join-Path $remoteStorage 'backups\records') `
            -Directory).Count
    if ($secondRecordCount -ne 2) {
        throw 'A repeated backup did not retain two small records without duplicating the snapshot path.'
    }

    & $restoreScript -DestinationRoot $restore -RclonePath $fakeRclone -RcloneConfigPath $config `
        -RemoteRoot 'test-remote:backups' -PublisherPath $fakePublisher
    $restoredPayload = Get-Content -LiteralPath (Join-Path $restore 'snapshots\snapshot-a\payload.bin') -Raw
    if ($restoredPayload -ne 'immutable-payload') {
        throw 'The remote restore did not reproduce the immutable payload.'
    }

    [IO.File]::WriteAllText(
        (Join-Path $distribution 'snapshots\snapshot-a\payload.bin'),
        'mutated-payload', [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        & $backupScript -DistributionRoot $distribution -RclonePath $fakeRclone `
            -RcloneConfigPath $config -RemoteRoot 'test-remote:backups' -PublisherPath $fakePublisher `
            -StatusPath $statusPath 2>$null
    } 'Immutable remote snapshot overwrite rejection'
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    if ($status.outcome -ne 'failed') {
        throw 'A failed backup did not publish a failed status document.'
    }

    [IO.Directory]::CreateDirectory((Join-Path $hostRoot 'DistributionRoot')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $hostRoot 'tools\publisher')) | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $hostRoot 'tools\publisher\KeireDistributionPublisher.exe'),
        'fixture', [Text.UTF8Encoding]::new($false))
    $installer = Join-Path $Root `
        'Services\KeireDistributionService\scripts\install-windows-backup-task.ps1'
    & $installer -HostRoot $hostRoot -RemoteRoot 'test-remote:backups' -RclonePath $fakeRclone `
        -RcloneConfigPath $config -TaskName 'Keire Distribution Backup Test' -ValidateOnly
}
finally {
    $env:KEIRE_TEST_RCLONE_ROOT = $previousFakeRoot
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'rclone distribution backup checks passed.'
