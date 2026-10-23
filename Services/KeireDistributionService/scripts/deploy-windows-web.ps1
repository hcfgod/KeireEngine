[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceWebRoot,
    [string] $SettingsPath = '',
    [string] $CandidateSettingsPath = '',
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
$originalSettings = $settings
$settingsTransition = -not [string]::IsNullOrWhiteSpace($CandidateSettingsPath)
$originalSettingsBytes = [IO.File]::ReadAllBytes($SettingsPath)
if ($settingsTransition) {
    $candidate = Get-Content -LiteralPath $CandidateSettingsPath -Raw | ConvertFrom-Json
    # A provider transition may not repoint shared services, ports, storage, or executable ownership.
    $transitionFields = @('schemaVersion', 'platformBackend', 'platformConfigurationPath')
    $names = @($settings.PSObject.Properties.Name) + @($candidate.PSObject.Properties.Name) | Sort-Object -Unique
    foreach ($name in $names) {
        if ($name -in $transitionFields) {
            continue
        }
        $before = $settings.PSObject.Properties[$name]
        $after = $candidate.PSObject.Properties[$name]
        if ($null -eq $before -or $null -eq $after -or
            (ConvertTo-Json $before.Value -Depth 20 -Compress) -cne (ConvertTo-Json $after.Value -Depth 20 -Compress)) {
            throw "Provider transition may not change host setting '$name'."
        }
    }
    if ($candidate.schemaVersion -ne 3 -or $candidate.platformBackend -cne 'mysql' -or
        [string]::IsNullOrWhiteSpace($candidate.platformConfigurationPath)) {
        throw 'Provider transition requires explicit schema-3 MySQL configuration.'
    }
    $settings = $candidate
}
if ([int] $settings.schemaVersion -notin @(2, 3)) {
    throw 'Transactional web deployment requires Windows host settings schema 2 or 3.'
}
if ([int] $settings.schemaVersion -eq 3) {
    $backendProperty = $settings.PSObject.Properties['platformBackend']
    if ($null -eq $backendProperty -or $backendProperty.Value -cnotin @('mysql', 'supabase')) {
        throw 'Schema-3 deployment requires an explicit supported platform backend.'
    }
}
$mysqlDeployment = [int] $settings.schemaVersion -eq 3 -and $settings.platformBackend -eq 'mysql'
$destinationRoot = Resolve-ConfiguredPath ([string] $settings.webRoot) $settingsDirectory
$nodeExecutable = Resolve-ConfiguredPath ([string] $settings.nodeExecutable) $settingsDirectory
if ($settingsTransition) {
    $platformPath = Resolve-ConfiguredPath ([string] $settings.platformConfigurationPath) $settingsDirectory
    $platform = Get-Content -LiteralPath $platformPath -Raw | ConvertFrom-Json
    if ($platform.schemaVersion -ne 1 -or $platform.port -ne 4321 -or
        $platform.siteOrigin -cne ('https://' + $settings.host) -or
        [IO.Path]::GetFullPath($platform.webRoot) -ne $destinationRoot) {
        throw 'Provider transition configuration must match the existing host origin, web root and port.'
    }
}
$supervisor = Join-Path $settingsDirectory 'scripts\start-windows-host.ps1'
$originalSupervisorBytes = [IO.File]::ReadAllBytes($supervisor)
$script:transitionInstalled = $false
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
if ($mysqlDeployment) {
    # The MySQL host imports application modules outside Astro dist; replace them atomically with the bundle.
    $runtimeUpdate = $true
    foreach ($requiredFile in @('scripts/platform-production-start.mjs', 'Source/lib/platform-backend.mjs')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $requiredFile) -PathType Leaf)) {
            throw "The MySQL deployment is missing runtime module '$requiredFile'."
        }
    }
}
function Replace-HostFile([string] $Path, [byte[]] $Contents) {
    $suffix = [Guid]::NewGuid().ToString('N')
    $temporary = $Path + '.staging-' + $suffix
    $backup = $Path + '.rollback-' + $suffix
    [IO.File]::WriteAllBytes($temporary, $Contents)
    # Preserve the destination ACL and an on-disk recovery copy, including across a process interruption.
    [IO.File]::Replace($temporary, $Path, $backup)
}

function Install-ProviderTransition {
    if (-not $settingsTransition) {
        return
    }
    $script:transitionInstalled = $true
    Replace-HostFile $supervisor ([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'start-windows-host.ps1')))
    Replace-HostFile $SettingsPath ([Text.UTF8Encoding]::new($false).GetBytes(($settings | ConvertTo-Json -Depth 20)))
    $script:destinationEntry = $newDestinationEntry
}

function Restore-ProviderTransition {
    if (-not $script:transitionInstalled) {
        return
    }
    Replace-HostFile $SettingsPath $originalSettingsBytes
    Replace-HostFile $supervisor $originalSupervisorBytes
    $script:destinationEntry = $oldDestinationEntry
    $script:transitionInstalled = $false
}
if ((Get-NormalizedPrefix $sourceRoot).StartsWith((Get-NormalizedPrefix $destinationRoot),
        [StringComparison]::OrdinalIgnoreCase) -or
    (Get-NormalizedPrefix $destinationRoot).StartsWith((Get-NormalizedPrefix $sourceRoot),
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The source and live web roots may not contain one another.'
}
if ($runtimeUpdate -and -not $AllowRuntimeUpdate) {
    throw 'This deployment requires -AllowRuntimeUpdate for an atomic locked-runtime replacement.'
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
if ($mysqlDeployment) {
    $destinationEntry = Join-Path $destinationRoot 'scripts\platform-production-start.mjs'
}
$newDestinationEntry = $destinationEntry
if ($settingsTransition -and ([int] $originalSettings.schemaVersion -eq 2 -or $originalSettings.platformBackend -eq 'supabase')) {
    $destinationEntry = Join-Path $destinationDist 'server\entry.mjs'
}
$oldDestinationEntry = $destinationEntry
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
            $writerSettings = $originalSettings
            if ($script:transitionInstalled) {
                $writerSettings = $settings
            }
            $configurationProperty = $writerSettings.PSObject.Properties['platformConfigurationPath']
            if ($null -ne $configurationProperty -and $writerSettings.platformBackend -eq 'mysql') {
                $runtimeConfigurationPath = Resolve-ConfiguredPath ([string] $configurationProperty.Value) $settingsDirectory
                $runtimeConfiguration = Get-Content -LiteralPath $runtimeConfigurationPath -Raw | ConvertFrom-Json
                & (Join-Path $PSScriptRoot 'retire-platform-writer.ps1') `
                    -SpoolDirectory $runtimeConfiguration.spoolDirectory -StoppedProcessId $process.ProcessId
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
        if ($mysqlDeployment) {
            $stagedScripts = Join-Path $stagedWeb 'scripts'
            $stagedLibraries = Join-Path $stagedWeb 'Source/lib'
            [IO.Directory]::CreateDirectory($stagedScripts) | Out-Null
            [IO.Directory]::CreateDirectory($stagedLibraries) | Out-Null
            Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'scripts') -File -Filter 'platform-production-*.mjs' |
                Copy-Item -Destination $stagedScripts
            Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'Source/lib') -File |
                Where-Object { $_.Extension -in @('.mjs', '.ts') -and $_.Name -notmatch '\.test\.' } |
                Copy-Item -Destination $stagedLibraries
        }
        & $npmExecutable --prefix $stagedWeb ci --omit=dev --ignore-scripts --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged locked web runtime dependency installation failed.'
        }
        & $nodeExecutable --check (Join-Path $stagedWeb 'dist\server\entry.mjs')
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged locked web runtime failed the Node.js syntax check.'
        }
        if ($mysqlDeployment) {
            $importCheck = 'import { pathToFileURL } from "node:url"; await import(pathToFileURL(process.argv[1]).href); await import(pathToFileURL(process.argv[2]).href);'
            & $nodeExecutable --input-type=module -e $importCheck `
                (Join-Path $stagedWeb 'scripts/platform-production-backend.mjs') `
                (Join-Path $stagedWeb 'scripts/platform-production-server.mjs')
            if ($LASTEXITCODE -ne 0) {
                throw 'The staged MySQL runtime could not load its production dependencies.'
            }
            # Disable adapter autostart before importing: standalone builds must never bind a port during preflight.
            $adapterCheck = 'process.env.ASTRO_NODE_AUTOSTART = "disabled"; const { pathToFileURL } = await import("node:url"); const entry = await import(pathToFileURL(process.argv[1]).href); if (entry.options?.mode !== "middleware" || typeof entry.handler !== "function") { console.error("MySQL deployment requires KEIRE_NODE_ADAPTER_MODE=middleware. Rebuild before deploying."); process.exit(1); }'
            & $nodeExecutable --input-type=module -e $adapterCheck (Join-Path $stagedWeb 'dist/server/entry.mjs')
            if ($LASTEXITCODE -ne 0) {
                throw 'The staged website is not a valid MySQL middleware build; live host was not stopped.'
            }
            if ($settingsTransition) {
                $configurationCheck = 'import { readFileSync } from "node:fs"; import { pathToFileURL } from "node:url"; const { productionConfiguration } = await import(pathToFileURL(process.argv[1]).href); productionConfiguration(JSON.parse(readFileSync(process.argv[2], "utf8").replace(/^\uFEFF/, "")));'
                & $nodeExecutable --input-type=module -e $configurationCheck `
                    (Join-Path $stagedWeb 'scripts/platform-production-backend.mjs') $platformPath
                if ($LASTEXITCODE -ne 0) {
                    throw 'The staged MySQL runtime rejected the provider configuration.'
                }
            }
        }
        Stop-ConfiguredHost
        Move-Item -LiteralPath $destinationRoot -Destination $backupWeb
        $backupCreated = $true
        Move-Item -LiteralPath $stagedWeb -Destination $destinationRoot
        Install-ProviderTransition
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
                Restore-ProviderTransition
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
