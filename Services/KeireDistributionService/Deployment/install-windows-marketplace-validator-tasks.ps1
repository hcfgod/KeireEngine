[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$WorkerTaskName = "Keire Marketplace Validator Worker",

    [string]$BrokerTaskName = "Keire Marketplace Validator Broker",

    [string]$WorkerLauncher,

    [string]$BrokerLauncher,

    [string]$Validator,

    [string]$Broker,

    [string]$ExchangeRoot,

    [string]$WorkRoot,

    [string]$AssetTool,

    [string]$MalwareScanner,

    [string]$Dotnet,

    [string]$ManagedApi,

    [string]$FirewallAttestation,

    [string]$SecretFile,

    [uri]$SupabaseUrl,

    [string]$ExpectedValidatorFingerprint,

    [string]$WorkerId,

    [switch]$Uninstall,

    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Quote-TaskArgument([string]$Value) {
    if ($Value.Contains('"')) {
        throw "Scheduled-task arguments may not contain quotation marks."
    }

    return '"' + $Value + '"'
}

function Assert-TaskName([string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Name) -or $Name.Length -gt 200 -or $Name.Contains('\')) {
        throw "The marketplace validator task name is invalid."
    }
}

function Assert-File([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: '$Path'."
    }
}

function Assert-Directory([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label is missing: '$Path'."
    }
}

Assert-TaskName $WorkerTaskName
Assert-TaskName $BrokerTaskName
if ([string]::Equals($WorkerTaskName, $BrokerTaskName, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The worker and broker task names must be different."
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Installing, validating, or removing marketplace validator tasks requires an elevated PowerShell session."
}

if ($Uninstall) {
    foreach ($taskName in @($BrokerTaskName, $WorkerTaskName)) {
        if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
            if ($PSCmdlet.ShouldProcess($taskName, "Stop and remove Windows marketplace validator task")) {
                Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
                Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
            }
        }
    }

    exit 0
}

$requiredFiles = [ordered]@{
    "Worker launcher" = $WorkerLauncher
    "Broker launcher" = $BrokerLauncher
    "Validator worker" = $Validator
    "Validator broker" = $Broker
    "Asset Tool" = $AssetTool
    "Malware scanner" = $MalwareScanner
    ".NET host" = $Dotnet
    "Managed API" = $ManagedApi
    "Firewall attestation" = $FirewallAttestation
    "Protected broker secret" = $SecretFile
}
foreach ($file in $requiredFiles.GetEnumerator()) {
    Assert-File $file.Value $file.Key
}
Assert-Directory $ExchangeRoot "Validator exchange root"
Assert-Directory $WorkRoot "Validator work root"

if ($null -eq $SupabaseUrl -or $SupabaseUrl.Scheme -ne "https" -or
    -not [string]::IsNullOrEmpty($SupabaseUrl.UserInfo) -or
    -not [string]::IsNullOrEmpty($SupabaseUrl.Query) -or
    -not [string]::IsNullOrEmpty($SupabaseUrl.Fragment)) {
    throw "SupabaseUrl must be an HTTPS origin without credentials or query data."
}
if ($ExpectedValidatorFingerprint -cnotmatch '^[0-9a-f]{64}$') {
    throw "ExpectedValidatorFingerprint must be a lowercase SHA-256 digest."
}
if ($WorkerId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$') {
    throw "WorkerId contains unsupported characters."
}

& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $WorkerLauncher `
    -Validator $Validator -ExchangeRoot $ExchangeRoot -WorkRoot $WorkRoot -AssetTool $AssetTool `
    -MalwareScanner $MalwareScanner -Dotnet $Dotnet -ManagedApi $ManagedApi `
    -FirewallAttestation $FirewallAttestation -ValidateOnly
if ($LASTEXITCODE -ne 0) {
    throw "The offline marketplace validator configuration failed validation."
}
& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $BrokerLauncher `
    -Broker $Broker -Validator $Validator -ExchangeRoot $ExchangeRoot -SecretFile $SecretFile `
    -SupabaseUrl $SupabaseUrl.AbsoluteUri -ExpectedValidatorFingerprint $ExpectedValidatorFingerprint `
    -WorkerId $WorkerId -ValidateOnly
if ($LASTEXITCODE -ne 0) {
    throw "The marketplace validator broker configuration failed validation."
}

if ($ValidateOnly) {
    Write-Host "Windows marketplace validator startup-task inputs are valid."
    exit 0
}

$powerShell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$workerArguments = @(
    "-NoProfile",
    "-NonInteractive",
    "-ExecutionPolicy", "Bypass",
    "-File", (Quote-TaskArgument ([IO.Path]::GetFullPath($WorkerLauncher))),
    "-Validator", (Quote-TaskArgument ([IO.Path]::GetFullPath($Validator))),
    "-ExchangeRoot", (Quote-TaskArgument ([IO.Path]::GetFullPath($ExchangeRoot))),
    "-WorkRoot", (Quote-TaskArgument ([IO.Path]::GetFullPath($WorkRoot))),
    "-AssetTool", (Quote-TaskArgument ([IO.Path]::GetFullPath($AssetTool))),
    "-MalwareScanner", (Quote-TaskArgument ([IO.Path]::GetFullPath($MalwareScanner))),
    "-Dotnet", (Quote-TaskArgument ([IO.Path]::GetFullPath($Dotnet))),
    "-ManagedApi", (Quote-TaskArgument ([IO.Path]::GetFullPath($ManagedApi))),
    "-FirewallAttestation", (Quote-TaskArgument ([IO.Path]::GetFullPath($FirewallAttestation)))
) -join " "
$brokerArguments = @(
    "-NoProfile",
    "-NonInteractive",
    "-ExecutionPolicy", "Bypass",
    "-File", (Quote-TaskArgument ([IO.Path]::GetFullPath($BrokerLauncher))),
    "-Broker", (Quote-TaskArgument ([IO.Path]::GetFullPath($Broker))),
    "-Validator", (Quote-TaskArgument ([IO.Path]::GetFullPath($Validator))),
    "-ExchangeRoot", (Quote-TaskArgument ([IO.Path]::GetFullPath($ExchangeRoot))),
    "-SecretFile", (Quote-TaskArgument ([IO.Path]::GetFullPath($SecretFile))),
    "-SupabaseUrl", (Quote-TaskArgument $SupabaseUrl.GetLeftPart([UriPartial]::Authority)),
    "-ExpectedValidatorFingerprint", (Quote-TaskArgument $ExpectedValidatorFingerprint),
    "-WorkerId", (Quote-TaskArgument $WorkerId)
) -join " "

$workerAction = New-ScheduledTaskAction -Execute $powerShell -Argument $workerArguments `
    -WorkingDirectory ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($WorkerLauncher)))
$brokerAction = New-ScheduledTaskAction -Execute $powerShell -Argument $brokerArguments `
    -WorkingDirectory ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($BrokerLauncher)))
$workerTrigger = New-ScheduledTaskTrigger -AtStartup
$workerTrigger.Delay = "PT20S"
$brokerTrigger = New-ScheduledTaskTrigger -AtStartup
$brokerTrigger.Delay = "PT30S"
$workerPrincipal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\LOCAL SERVICE" -LogonType ServiceAccount `
    -RunLevel Limited
$brokerPrincipal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\NETWORK SERVICE" -LogonType ServiceAccount `
    -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
$workerTask = New-ScheduledTask -Action $workerAction -Trigger $workerTrigger -Principal $workerPrincipal `
    -Settings $settings -Description "Runs the network-isolated Keire marketplace package validator."
$brokerTask = New-ScheduledTask -Action $brokerAction -Trigger $brokerTrigger -Principal $brokerPrincipal `
    -Settings $settings -Description "Leases marketplace packages for the isolated Keire validator."

if ($PSCmdlet.ShouldProcess($WorkerTaskName, "Install Windows marketplace validator worker task as LOCAL SERVICE")) {
    Register-ScheduledTask -TaskName $WorkerTaskName -InputObject $workerTask -Force | Out-Null
}
if ($PSCmdlet.ShouldProcess($BrokerTaskName, "Install Windows marketplace validator broker task as NETWORK SERVICE")) {
    Register-ScheduledTask -TaskName $BrokerTaskName -InputObject $brokerTask -Force | Out-Null
}
if ($PSCmdlet.ShouldProcess("$WorkerTaskName and $BrokerTaskName", "Start Windows marketplace validator tasks")) {
    Start-ScheduledTask -TaskName $WorkerTaskName
    Start-ScheduledTask -TaskName $BrokerTaskName
}
