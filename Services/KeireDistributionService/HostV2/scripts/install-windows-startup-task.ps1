[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string] $SettingsPath = '',
    [string] $TaskName = 'Keire Distribution Host',
    [switch] $Uninstall,
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-TaskArgument([string] $Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

$supervisor = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'start-windows-host.ps1'))
if ([string]::IsNullOrWhiteSpace($SettingsPath)) {
    $SettingsPath = Join-Path $PSScriptRoot 'host-settings.json'
}
$SettingsPath = [IO.Path]::GetFullPath($SettingsPath)

if (-not (Test-Path -LiteralPath $supervisor -PathType Leaf)) {
    throw "The Windows host supervisor does not exist: '$supervisor'."
}
if ([string]::IsNullOrWhiteSpace($TaskName) -or $TaskName.Length -gt 200) {
    throw 'The startup task name is invalid.'
}

if (-not $Uninstall) {
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $supervisor `
        -SettingsPath $SettingsPath -ValidateOnly
    if ($LASTEXITCODE -ne 0) {
        throw 'The distribution host settings failed validation.'
    }
}

if ($ValidateOnly) {
    Write-Host "Windows pre-login startup task inputs are valid for '$TaskName'."
    exit 0
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Installing or removing the pre-login startup task requires an elevated PowerShell session.'
}

if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        if ($PSCmdlet.ShouldProcess($TaskName, 'Remove Windows pre-login distribution host task')) {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        }
    }
    exit 0
}

$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = @(
    '-NoProfile',
    '-NonInteractive',
    '-ExecutionPolicy', 'Bypass',
    '-File', (Quote-TaskArgument $supervisor),
    '-SettingsPath', (Quote-TaskArgument $SettingsPath),
    '-KeepAlive'
) -join ' '
$action = New-ScheduledTaskAction -Execute $powerShell -Argument $arguments -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -AtStartup
$trigger.Delay = 'PT30S'
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'NT AUTHORITY\SYSTEM' -LogonType ServiceAccount `
    -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $taskPrincipal -Settings $settings `
    -Description 'Starts and supervises the Kéire distribution service and Caddy before interactive sign-in.'

if ($PSCmdlet.ShouldProcess($TaskName, 'Install Windows pre-login distribution host task as Local System')) {
    Register-ScheduledTask -TaskName $TaskName -InputObject $task -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
}
