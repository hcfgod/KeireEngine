[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceWebRoot,
    [string] $SettingsPath = '',
    [switch] $AllowRuntimeUpdate,
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ConfiguredPath([string] $Value, [string] $BaseDirectory) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw 'A configured path cannot be empty.'
    }
    if ([IO.Path]::IsPathRooted($Value)) {
        return [IO.Path]::GetFullPath($Value)
    }
    return [IO.Path]::GetFullPath((Join-Path $BaseDirectory $Value))
}

function Get-NormalizedPrefix([string] $Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
}

if ([string]::IsNullOrWhiteSpace($SettingsPath)) {
    $SettingsPath = Join-Path $PSScriptRoot '..\host-settings.json'
}
$SettingsPath = [IO.Path]::GetFullPath($SettingsPath)
$sourceRoot = [IO.Path]::GetFullPath($SourceWebRoot)
if (-not (Test-Path -LiteralPath $SettingsPath -PathType Leaf)) {
    throw "Windows host settings do not exist: '$SettingsPath'."
}
$settingsDirectory = Split-Path -Parent $SettingsPath
$settings = Get-Content -LiteralPath $SettingsPath -Raw | ConvertFrom-Json
if ([int] $settings.schemaVersion -ne 2) {
    throw 'Transactional web deployment requires Windows host settings schema 2.'
}
$destinationRoot = Resolve-ConfiguredPath ([string] $settings.webRoot) $settingsDirectory
$nodeExecutable = Resolve-ConfiguredPath ([string] $settings.nodeExecutable) $settingsDirectory
$supervisor = Join-Path $settingsDirectory 'scripts\start-windows-host.ps1'
$sourceDist = Join-Path $sourceRoot 'dist'
$destinationDist = Join-Path $destinationRoot 'dist'
$sourceLock = Join-Path $sourceRoot 'package-lock.json'
$destinationLock = Join-Path $destinationRoot 'package-lock.json'
$sourcePackage = Join-Path $sourceRoot 'package.json'
foreach ($requiredFile in @((Join-Path $sourceDist 'server\entry.mjs'), (Join-Path $destinationDist 'server\entry.mjs'),
        $sourceLock, $destinationLock, $sourcePackage, $nodeExecutable, $supervisor)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "A required web deployment file does not exist: '$requiredFile'."
    }
}
$runtimeUpdate = (Get-FileHash -LiteralPath $sourceLock -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $destinationLock -Algorithm SHA256).Hash
if ((Get-NormalizedPrefix $sourceRoot).StartsWith((Get-NormalizedPrefix $destinationRoot),
        [StringComparison]::OrdinalIgnoreCase) -or
    (Get-NormalizedPrefix $destinationRoot).StartsWith((Get-NormalizedPrefix $sourceRoot),
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The source and live web roots may not contain one another.'
}
if ($runtimeUpdate -and -not $AllowRuntimeUpdate) {
    throw 'The dependency lock changed; pass -AllowRuntimeUpdate for a transactional locked-runtime deployment.'
}
if ($ValidateOnly) {
    $kind = if ($runtimeUpdate) { 'locked-runtime' } else { 'bundle-only' }
    Write-Host "Windows $kind web deployment inputs are valid for '$destinationRoot'."
    exit 0
}

$stagedDist = Join-Path $destinationRoot ('.dist.staging-' + [Guid]::NewGuid().ToString('N'))
$backupDist = Join-Path $destinationRoot ('dist.rollback-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
$destinationParent = Split-Path -Parent $destinationRoot
$destinationName = Split-Path -Leaf $destinationRoot
$stagedWeb = Join-Path $destinationParent ('.' + $destinationName + '.staging-' + [Guid]::NewGuid().ToString('N'))
$backupWeb = Join-Path $destinationParent ($destinationName + '.rollback-' +
    [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N'))
$destinationEntry = Join-Path $destinationDist 'server\entry.mjs'
$hostTasks = @(Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object {
        foreach ($action in $_.Actions) {
            $argumentsProperty = $action.PSObject.Properties['Arguments']
            if ($null -eq $argumentsProperty) {
                continue
            }
            $arguments = [string] $argumentsProperty.Value
            if ($arguments.IndexOf($SettingsPath, [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
                $arguments.IndexOf($supervisor, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                return $true
            }
        }
        return $false
    })
if ($hostTasks.Count -gt 1) {
    throw "More than one scheduled host task uses '$SettingsPath'; refusing an ambiguous deployment."
}
$hostTask = if ($hostTasks.Count -eq 1) { $hostTasks[0] } else { $null }

function Stop-ExpectedWebProcess {
    foreach ($connection in Get-NetTCPConnection -State Listen -LocalPort 4321 -ErrorAction SilentlyContinue) {
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($connection.OwningProcess)" `
            -ErrorAction SilentlyContinue
        if ($null -eq $process -or
            -not [string]::Equals($process.ExecutablePath, $nodeExecutable, [StringComparison]::OrdinalIgnoreCase) -or
            $process.CommandLine.IndexOf($destinationEntry, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
            throw 'Port 4321 is not owned by the configured live web deployment; refusing to stop it.'
        }
        Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
    }
}

function Wait-ForDeployment([int] $TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Seconds 2
        try {
            & $supervisor -SettingsPath $SettingsPath -ProbeOnly
            if ($LASTEXITCODE -eq 0) {
                return
            }
        }
        catch {
            # The persistent supervisor may still be restarting Node.js.
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The updated web deployment did not become healthy before the deadline.'
}

function Stop-ConfiguredHost {
    if ($null -ne $hostTask) {
        Stop-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
    }
    Stop-ExpectedWebProcess
}

function Start-ConfiguredHost {
    if ($null -ne $hostTask) {
        Start-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
    }
}

if ($runtimeUpdate) {
    $npmExecutable = Join-Path (Split-Path -Parent $nodeExecutable) 'npm.cmd'
    if (-not (Test-Path -LiteralPath $npmExecutable -PathType Leaf)) {
        throw "The configured Node.js installation has no npm.cmd: '$npmExecutable'."
    }
    try {
        [IO.Directory]::CreateDirectory($stagedWeb) | Out-Null
        Copy-Item -LiteralPath $sourcePackage, $sourceLock -Destination $stagedWeb
        Copy-Item -LiteralPath $sourceDist -Destination (Join-Path $stagedWeb 'dist') -Recurse
        & $npmExecutable --prefix $stagedWeb ci --omit=dev --ignore-scripts --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged locked web runtime dependency installation failed.'
        }
        & $nodeExecutable --check (Join-Path $stagedWeb 'dist\server\entry.mjs')
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged locked web runtime failed the Node.js syntax check.'
        }
        Stop-ConfiguredHost
        Move-Item -LiteralPath $destinationRoot -Destination $backupWeb
        Move-Item -LiteralPath $stagedWeb -Destination $destinationRoot
        try {
            Start-ConfiguredHost
            Wait-ForDeployment 75
        }
        catch {
            Stop-ConfiguredHost
            $failedWeb = Join-Path $destinationParent ('.' + $destinationName + '.failed-' +
                [Guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $destinationRoot -Destination $failedWeb
            Move-Item -LiteralPath $backupWeb -Destination $destinationRoot
            Start-ConfiguredHost
            Wait-ForDeployment 75
            throw
        }
        Write-Host "Windows locked web runtime deployment completed. Rollback retained at '$backupWeb'."
    }
    catch {
        if (Test-Path -LiteralPath $stagedWeb) {
            Remove-Item -LiteralPath $stagedWeb -Recurse -Force
        }
        throw
    }
    exit 0
}

try {
    Copy-Item -LiteralPath $sourceDist -Destination $stagedDist -Recurse
    & $nodeExecutable --check (Join-Path $stagedDist 'server\entry.mjs')
    if ($LASTEXITCODE -ne 0) {
        throw 'The staged server entry point failed the Node.js syntax check.'
    }
    Stop-ConfiguredHost
    Move-Item -LiteralPath $destinationDist -Destination $backupDist
    Move-Item -LiteralPath $stagedDist -Destination $destinationDist
    try {
        Start-ConfiguredHost
        Wait-ForDeployment 75
    }
    catch {
        Stop-ConfiguredHost
        $failedDist = Join-Path $destinationRoot ('.dist.failed-' + [Guid]::NewGuid().ToString('N'))
        Move-Item -LiteralPath $destinationDist -Destination $failedDist
        Move-Item -LiteralPath $backupDist -Destination $destinationDist
        Start-ConfiguredHost
        Wait-ForDeployment 75
        throw
    }
    Write-Host "Windows web deployment completed. Rollback retained at '$backupDist'."
}
catch {
    if (Test-Path -LiteralPath $stagedDist) {
        Remove-Item -LiteralPath $stagedDist -Recurse -Force
    }
    throw
}
