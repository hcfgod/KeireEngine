[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$TaskName = "Keire Marketplace Publication Signer",
    [string]$Launcher,
    [string]$Signer,
    [string]$ProtectedPrivateKey,
    [string]$ProtectedQueueSecret,
    [uri]$SupabaseUrl,
    [string]$PublicationPublicKey,
    [string]$ValidatorAttestationPublicKey,
    [string]$WorkerId,
    [switch]$Uninstall,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($TaskName) -or $TaskName.Length -gt 200 -or $TaskName.Contains('\')) {
    throw "The publication signer task name is invalid."
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Installing, validating, or removing the publication signer task requires an elevated PowerShell session."
}
if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        if ($PSCmdlet.ShouldProcess($TaskName, "Stop and remove Marketplace publication signer task")) {
            Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        }
    }
    exit 0
}

function Quote-TaskArgument([string]$Value) {
    if ($Value.Contains('"')) { throw "Scheduled-task arguments may not contain quotation marks." }
    return '"' + $Value + '"'
}
foreach ($path in @($Launcher, $Signer, $ProtectedPrivateKey, $ProtectedQueueSecret, $PublicationPublicKey,
        $ValidatorAttestationPublicKey)) {
    if ([string]::IsNullOrWhiteSpace($path) -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "A required publication signer file is missing: '$path'."
    }
}

& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Launcher `
    -Signer $Signer -ProtectedPrivateKey $ProtectedPrivateKey -ProtectedQueueSecret $ProtectedQueueSecret `
    -SupabaseUrl $SupabaseUrl.AbsoluteUri -PublicationPublicKey $PublicationPublicKey `
    -ValidatorAttestationPublicKey $ValidatorAttestationPublicKey -WorkerId $WorkerId -ValidateOnly
if ($LASTEXITCODE -ne 0) { throw "The Marketplace publication signer configuration failed validation." }
if ($ValidateOnly) {
    Write-Host "Windows Marketplace publication signer startup-task inputs are valid."
    exit 0
}

$powerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$arguments = @(
    "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
    (Quote-TaskArgument ([IO.Path]::GetFullPath($Launcher))),
    "-Signer", (Quote-TaskArgument ([IO.Path]::GetFullPath($Signer))),
    "-ProtectedPrivateKey", (Quote-TaskArgument ([IO.Path]::GetFullPath($ProtectedPrivateKey))),
    "-ProtectedQueueSecret", (Quote-TaskArgument ([IO.Path]::GetFullPath($ProtectedQueueSecret))),
    "-SupabaseUrl", (Quote-TaskArgument $SupabaseUrl.GetLeftPart([UriPartial]::Authority)),
    "-PublicationPublicKey", (Quote-TaskArgument ([IO.Path]::GetFullPath($PublicationPublicKey))),
    "-ValidatorAttestationPublicKey", (Quote-TaskArgument ([IO.Path]::GetFullPath($ValidatorAttestationPublicKey))),
    "-WorkerId", (Quote-TaskArgument $WorkerId)
) -join " "
$action = New-ScheduledTaskAction -Execute $powerShell -Argument $arguments `
    -WorkingDirectory ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Launcher)))
$trigger = New-ScheduledTaskTrigger -AtStartup
$trigger.Delay = "PT40S"
$taskPrincipal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\NETWORK SERVICE" -LogonType ServiceAccount `
    -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew
$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $taskPrincipal -Settings $settings `
    -Description "Signs approved Marketplace metadata without receiving publisher package bytes."
if ($PSCmdlet.ShouldProcess($TaskName, "Install Windows Marketplace publication signer task")) {
    Register-ScheduledTask -TaskName $TaskName -InputObject $task -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
}
