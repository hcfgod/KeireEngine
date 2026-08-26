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
if ($hostTasks.Count -ne 1) {
    throw "Exactly one scheduled host task must use '$SettingsPath'; found $($hostTasks.Count)."
}
$hostTask = $hostTasks[0]
$script:hostRestartRequired = $false

function Stop-ExpectedWebProcess {
    foreach ($connection in Get-NetTCPConnection -State Listen -LocalPort 4321 -ErrorAction SilentlyContinue) {
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($connection.OwningProcess)" `
            -ErrorAction SilentlyContinue
        if ($null -eq $process -or
            -not [string]::Equals($process.ExecutablePath, $nodeExecutable, [StringComparison]::OrdinalIgnoreCase) -or
            $process.CommandLine.IndexOf($destinationEntry, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
            throw 'Port 4321 is not owned by the configured live web deployment; refusing to stop it.'
        }
        $ownedProcess = Get-Process -Id $process.ProcessId -ErrorAction Stop
        try {
            Stop-Process -InputObject $ownedProcess -Force -ErrorAction Stop
            if (-not $ownedProcess.WaitForExit(15000)) {
                throw "The configured web process $($process.ProcessId) did not exit before the deployment deadline."
            }
        }
        finally {
            $ownedProcess.Dispose()
        }
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
    $script:hostRestartRequired = $true
    $currentTask = Get-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
    if ($currentTask.State -eq 'Running') {
        Stop-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $state = (Get-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath `
                    -ErrorAction Stop).State
            if ($state -ne 'Running') {
                break
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($state -eq 'Running') {
            throw 'The configured host task did not stop before the deployment deadline.'
        }
    }
    Stop-ExpectedWebProcess
}

function Start-ConfiguredHost {
    $currentTask = Get-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
    if ($currentTask.State -ne 'Running') {
        Start-ScheduledTask -TaskName $hostTask.TaskName -TaskPath $hostTask.TaskPath -ErrorAction Stop
    }
}

function Start-AndVerifyConfiguredHost {
    Start-ConfiguredHost
    Wait-ForDeployment 75
    $script:hostRestartRequired = $false
}

if ($runtimeUpdate) {
    $npmExecutable = Join-Path (Split-Path -Parent $nodeExecutable) 'npm.cmd'
    if (-not (Test-Path -LiteralPath $npmExecutable -PathType Leaf)) {
        throw "The configured Node.js installation has no npm.cmd: '$npmExecutable'."
    }
    $backupCreated = $false
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
        $backupCreated = $true
        Move-Item -LiteralPath $stagedWeb -Destination $destinationRoot
        Start-AndVerifyConfiguredHost
        Write-Host "Windows locked web runtime deployment completed. Rollback retained at '$backupWeb'."
    }
    catch {
        $deploymentError = $_
        if ($backupCreated -and (Test-Path -LiteralPath $backupWeb -PathType Container)) {
            try {
                Stop-ConfiguredHost
                if (Test-Path -LiteralPath $destinationRoot) {
                    $failedWeb = Join-Path $destinationParent ('.' + $destinationName + '.failed-' +
                        [Guid]::NewGuid().ToString('N'))
                    Move-Item -LiteralPath $destinationRoot -Destination $failedWeb
                }
                Move-Item -LiteralPath $backupWeb -Destination $destinationRoot
                $backupCreated = $false
            }
            catch {
                Write-Warning "The previous web deployment could not be restored after deployment failure: $($_.Exception.Message)"
            }
        }
        if ($script:hostRestartRequired -and (Test-Path -LiteralPath $destinationEntry -PathType Leaf)) {
            try {
                Start-AndVerifyConfiguredHost
            }
            catch {
                Write-Warning `
                    "The previous web deployment could not be restarted after deployment failure: $($_.Exception.Message)"
            }
        }
        if (Test-Path -LiteralPath $stagedWeb) {
            try {
                Remove-Item -LiteralPath $stagedWeb -Recurse -Force
            }
            catch {
                Write-Warning "The failed staged web deployment could not be removed: $($_.Exception.Message)"
            }
        }
        throw $deploymentError
    }
    exit 0
}

$backupCreated = $false
try {
    Copy-Item -LiteralPath $sourceDist -Destination $stagedDist -Recurse
    & $nodeExecutable --check (Join-Path $stagedDist 'server\entry.mjs')
    if ($LASTEXITCODE -ne 0) {
        throw 'The staged server entry point failed the Node.js syntax check.'
    }
    Stop-ConfiguredHost
    Move-Item -LiteralPath $destinationDist -Destination $backupDist
    $backupCreated = $true
    Move-Item -LiteralPath $stagedDist -Destination $destinationDist
    Start-AndVerifyConfiguredHost
    Write-Host "Windows web deployment completed. Rollback retained at '$backupDist'."
}
catch {
    $deploymentError = $_
    if ($backupCreated -and (Test-Path -LiteralPath $backupDist -PathType Container)) {
        try {
            Stop-ConfiguredHost
            if (Test-Path -LiteralPath $destinationDist) {
                $failedDist = Join-Path $destinationRoot ('.dist.failed-' + [Guid]::NewGuid().ToString('N'))
                Move-Item -LiteralPath $destinationDist -Destination $failedDist
            }
            Move-Item -LiteralPath $backupDist -Destination $destinationDist
            $backupCreated = $false
        }
        catch {
            Write-Warning "The previous web deployment could not be restored after deployment failure: $($_.Exception.Message)"
        }
    }
    if ($script:hostRestartRequired -and (Test-Path -LiteralPath $destinationEntry -PathType Leaf)) {
        try {
            Start-AndVerifyConfiguredHost
        }
        catch {
            Write-Warning `
                "The previous web deployment could not be restarted after deployment failure: $($_.Exception.Message)"
        }
    }
    if (Test-Path -LiteralPath $stagedDist) {
        try {
            Remove-Item -LiteralPath $stagedDist -Recurse -Force
        }
        catch {
            Write-Warning "The failed staged web deployment could not be removed: $($_.Exception.Message)"
        }
    }
    throw $deploymentError
}
