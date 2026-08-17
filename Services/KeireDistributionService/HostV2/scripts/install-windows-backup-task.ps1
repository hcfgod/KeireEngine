[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string] $HostRoot,
    [string] $RemoteRoot = 'keire-drive:KeireEngine/DistributionBackups',
    [string] $RclonePath = '',
    [string] $RcloneConfigPath = '',
    [string] $TaskName = 'Keire Distribution Backup',
    [DateTime] $DailyAt = [DateTime]::Today.AddHours(3).AddMinutes(15),
    [switch] $StartNow,
    [switch] $Uninstall,
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-TaskArgument([string] $Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

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

if ([string]::IsNullOrWhiteSpace($TaskName) -or $TaskName.Length -gt 200) {
    throw 'The backup task name is invalid.'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if ($Uninstall) {
    if ($ValidateOnly) {
        Write-Host "Windows distribution backup task removal inputs are valid for '$TaskName'."
        exit 0
    }
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Removing the distribution backup task requires an elevated PowerShell session.'
    }
    if ((Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) -and
        $PSCmdlet.ShouldProcess($TaskName, 'Remove Windows distribution backup task')) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    }
    exit 0
}

$hostPath = [IO.Path]::GetFullPath($HostRoot)
if (-not (Test-Path -LiteralPath $hostPath -PathType Container)) {
    throw "The protected distribution host does not exist: '$hostPath'."
}
if ([string]::IsNullOrWhiteSpace($RclonePath)) {
    $RclonePath = Join-Path $hostPath 'tools\rclone\rclone.exe'
}
if ([string]::IsNullOrWhiteSpace($RcloneConfigPath)) {
    $RcloneConfigPath = Join-Path $hostPath 'Secrets\rclone.conf'
}
$rcloneExecutable = Resolve-Executable $RclonePath 'rclone'
$rcloneConfig = [IO.Path]::GetFullPath($RcloneConfigPath)
if (-not (Test-Path -LiteralPath $rcloneConfig -PathType Leaf)) {
    throw "The protected rclone configuration does not exist: '$rcloneConfig'."
}
$remote = $RemoteRoot.Trim().TrimEnd('/')
if ($remote -notmatch '^([^:\r\n]+):(.+)$') {
    throw 'RemoteRoot must be a non-root rclone remote path such as remote:folder/backups.'
}
$remoteName = $Matches[1] + ':'
$configuredRemotes = @(& $rcloneExecutable listremotes --config $rcloneConfig)
if ($LASTEXITCODE -ne 0) {
    throw 'The protected rclone configuration could not be read.'
}
if (-not ($configuredRemotes -contains $remoteName)) {
    throw "The rclone configuration does not define '$remoteName'."
}

$distributionRoot = Join-Path $hostPath 'DistributionRoot'
$publisher = Join-Path $hostPath 'tools\publisher\KeireDistributionPublisher.exe'
foreach ($requiredPath in @($distributionRoot, $publisher)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "The protected distribution host is missing '$requiredPath'."
    }
}
$sourceBackupScript = Join-Path $PSScriptRoot 'backup-distribution-rclone.ps1'
$sourceRestoreScript = Join-Path $PSScriptRoot 'restore-distribution-rclone.ps1'
foreach ($sourceScript in @($sourceBackupScript, $sourceRestoreScript)) {
    if (-not (Test-Path -LiteralPath $sourceScript -PathType Leaf)) {
        throw "The backup task installer is missing '$sourceScript'."
    }
}

if ($ValidateOnly) {
    Write-Host "Windows off-machine backup task inputs are valid for '$TaskName'."
    exit 0
}
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Installing the distribution backup task requires an elevated PowerShell session.'
}

$installedScripts = Join-Path $hostPath 'scripts'
[IO.Directory]::CreateDirectory($installedScripts) | Out-Null
foreach ($sourceScript in @($sourceBackupScript, $sourceRestoreScript, $PSCommandPath)) {
    $installedScript = Join-Path $installedScripts (Split-Path -Leaf $sourceScript)
    if (-not [string]::Equals([IO.Path]::GetFullPath($sourceScript), [IO.Path]::GetFullPath($installedScript),
            [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $sourceScript -Destination $installedScript -Force
    }
}

$backupScript = Join-Path $installedScripts 'backup-distribution-rclone.ps1'
$logPath = Join-Path $hostPath 'Logs\distribution-backup.log'
$statusPath = Join-Path $hostPath 'Logs\distribution-backup-status.json'
$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = @(
    '-NoProfile',
    '-NonInteractive',
    '-ExecutionPolicy', 'Bypass',
    '-File', (Quote-TaskArgument $backupScript),
    '-DistributionRoot', (Quote-TaskArgument $distributionRoot),
    '-RclonePath', (Quote-TaskArgument $rcloneExecutable),
    '-RcloneConfigPath', (Quote-TaskArgument $rcloneConfig),
    '-RemoteRoot', (Quote-TaskArgument $remote),
    '-PublisherPath', (Quote-TaskArgument $publisher),
    '-LogPath', (Quote-TaskArgument $logPath),
    '-StatusPath', (Quote-TaskArgument $statusPath)
) -join ' '
$action = New-ScheduledTaskAction -Execute $powerShell -Argument $arguments -WorkingDirectory $installedScripts
$trigger = New-ScheduledTaskTrigger -Daily -At $DailyAt
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'NT AUTHORITY\SYSTEM' -LogonType ServiceAccount `
    -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -WakeToRun -RunOnlyIfNetworkAvailable `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 15) -ExecutionTimeLimit (New-TimeSpan -Hours 6) `
    -MultipleInstances IgnoreNew
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $taskPrincipal -Settings $settings `
    -Description 'Uploads verified immutable Keire distribution snapshots to protected off-machine storage.'

$previousTaskXml = $null
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    $previousTaskXml = Export-ScheduledTask -TaskName $TaskName
}
if ($PSCmdlet.ShouldProcess($TaskName, 'Install Windows distribution backup task as Local System')) {
    try {
        Register-ScheduledTask -TaskName $TaskName -InputObject $task -Force | Out-Null
        if ($StartNow) {
            Start-ScheduledTask -TaskName $TaskName
        }
    }
    catch {
        if ($previousTaskXml) {
            Register-ScheduledTask -TaskName $TaskName -Xml $previousTaskXml -Force | Out-Null
        }
        else {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
        }
        throw
    }
}

Write-Host "Installed '$TaskName' as Local System for $($DailyAt.ToString('HH:mm')) local time."
if ($StartNow) {
    Write-Host 'The initial off-machine backup was started.'
}
