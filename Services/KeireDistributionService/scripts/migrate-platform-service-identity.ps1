[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [ValidateSet('Prepare', 'Execute', 'Verify')] [string] $Mode,
    [Parameter(Mandatory)] [string] $BridgeDirectory,
    [string] $PlanPath,
    [string] $DestinationRoot,
    [string] $PowerShellExecutable
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'platform-service-identity-common.psm1') -Force
$plainBytes = $null
$taskName = $null
$taskRegistered = $false
$stage = 'identity-and-path-validation'
try
{
    Assert-KeireIdentityAdministrator
    $BridgeDirectory = Resolve-KeireIdentityPath $BridgeDirectory
    if ($Mode -eq 'Prepare')
    {
        $stage = 'prepare-plan'
        $PlanPath = Resolve-KeireIdentityPath $PlanPath
        $DestinationRoot = Resolve-KeireIdentityPath $DestinationRoot
        if ((Test-Path -LiteralPath $DestinationRoot) -or (Test-Path -LiteralPath $BridgeDirectory))
        {
            throw 'Prepare requires new bridge and destination directories.'
        }
        foreach ($pair in @(@($BridgeDirectory, $DestinationRoot), @($DestinationRoot, $BridgeDirectory)))
        {
            if ($pair[1].StartsWith($pair[0].TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
                $pair[0].Equals($pair[1], [StringComparison]::OrdinalIgnoreCase))
            {
                throw 'Bridge and destination directories must be separate.'
            }
        }
        if ((Get-Item -LiteralPath $PlanPath).Length -gt 65536)
        {
            throw 'Migration plan is oversized.'
        }
        $plan = Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
        $entries = @(ConvertTo-KeireIdentityPlan $plan)
        foreach ($entry in $entries)
        {
            Resolve-KeireIdentityPath $entry.sourcePath | Out-Null
            if (-not (Test-Path -LiteralPath $entry.sourcePath -PathType Leaf) -or
                (Get-Item -LiteralPath $entry.sourcePath).Length -gt 1048576)
            {
                throw 'An expected protected source is missing or oversized.'
            }
        }
        $stage = 'prepare-private-bridge'
        New-KeireIdentityPrivateDirectory $BridgeDirectory
        $migrationId = [Guid]::NewGuid().ToString('N')
        $protectedEntries = @()
        $stage = 'prepare-encrypted-settings'
        foreach ($entry in $entries)
        {
            $sourceBytes = [IO.File]::ReadAllBytes($entry.sourcePath)
            $sourceHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($sourceBytes)).ToLowerInvariant()
            $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect($sourceBytes,
                [Text.Encoding]::UTF8.GetBytes($entry.entropy), [Security.Cryptography.DataProtectionScope]::CurrentUser)
            $bridgeFile = [Guid]::NewGuid().ToString('N') + '.bridge'
            $bridgeEntropy = Get-KeireIdentityBridgeEntropy $migrationId $entry.kind $entry.filename
            $bridgeBytes = [Security.Cryptography.ProtectedData]::Protect($plainBytes,
                [Text.Encoding]::UTF8.GetBytes($bridgeEntropy), [Security.Cryptography.DataProtectionScope]::LocalMachine)
            [Array]::Clear($plainBytes, 0, $plainBytes.Length)
            $plainBytes = $null
            $bridgePath = Join-Path $BridgeDirectory $bridgeFile
            Write-KeireIdentityNewFile $bridgePath $bridgeBytes
            $protectedEntries += [pscustomobject]@{
                kind = $entry.kind; sourcePath = $entry.sourcePath; sourceHash = $sourceHash
                targetDirectory = $entry.targetDirectory; filename = $entry.filename; entropy = $entry.entropy
                bridgeFile = $bridgeFile; bridgeHash = Get-KeireIdentityHash $bridgePath
            }
        }
        $stage = 'prepare-protected-receiver'
        $helperHashes = @{}
        foreach ($name in @('complete-platform-service-identity.ps1', 'platform-service-identity-common.psm1'))
        {
            $sourceHelper = Join-Path $PSScriptRoot $name
            $destinationHelper = Join-Path $BridgeDirectory $name
            Write-KeireIdentityNewFile $destinationHelper ([IO.File]::ReadAllBytes($sourceHelper))
            $helperHashes[$name] = Get-KeireIdentityHash $destinationHelper
        }
        $manifest = @{ schemaVersion = 1; migrationId = $migrationId; destinationRoot = $DestinationRoot
            sourceSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
            helperHashes = $helperHashes; entries = $protectedEntries }
        $manifestBytes = [Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Depth 8))
        Write-KeireIdentityNewFile (Join-Path $BridgeDirectory 'manifest.json') $manifestBytes
        Read-KeireIdentityManifest $BridgeDirectory | Out-Null
        @{ status = 'bridge-prepared'; migrationId = $migrationId; settingsFiles = $entries.Count
            sourcePreserved = $true; next = 'Run Execute explicitly as the same elevated user.' } | ConvertTo-Json -Compress
        exit 0
    }

    $stage = 'verify-prepared-bridge'
    $manifest = Read-KeireIdentityManifest $BridgeDirectory
    if ($manifest.sourceSid -cne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value)
    {
        throw 'Use the same elevated provisioning user that prepared this bridge.'
    }
    foreach ($entry in $manifest.entries)
    {
        if ((Get-KeireIdentityHash $entry.sourcePath) -cne $entry.sourceHash)
        {
            throw 'A protected source changed after preparation. No activation is allowed from this bridge.'
        }
    }
    $receiptPath = Join-Path $BridgeDirectory 'receipt.json'
    if ($Mode -eq 'Execute')
    {
        $stage = 'execute-system-task'
        $PowerShellExecutable = Resolve-KeireIdentityPath $PowerShellExecutable
        if (-not (Test-Path -LiteralPath $PowerShellExecutable -PathType Leaf) -or
            (Test-Path -LiteralPath $receiptPath) -or (Test-Path -LiteralPath $manifest.destinationRoot))
        {
            throw 'Execution requires an available PowerShell binary and unused destination. Inspect existing output instead of overwriting.'
        }
        $receiver = Join-Path $BridgeDirectory 'complete-platform-service-identity.ps1'
        $taskName = 'KeireIdentityBridge-' + $manifest.migrationId
        if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)
        {
            throw 'This migration task already exists. Inspect it before retrying.'
        }
        if ($receiver.Contains('"') -or $BridgeDirectory.Contains('"'))
        {
            throw 'Migration paths contain unsupported quoting.'
        }
        $arguments = '-NoLogo -NoProfile -NonInteractive -File "' + $receiver + '" -BridgeDirectory "' + $BridgeDirectory + '"'
        $action = New-ScheduledTaskAction -Execute $PowerShellExecutable -Argument $arguments
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
        Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
        $taskRegistered = $true
        Start-ScheduledTask -TaskName $taskName
        $deadline = [DateTime]::UtcNow.AddMinutes(2)
        do
        {
            Start-Sleep -Milliseconds 250
            $task = Get-ScheduledTask -TaskName $taskName
            $information = Get-ScheduledTaskInfo -TaskName $taskName
            if ($task.State -notin @('Running', 'Queued') -and $information.LastRunTime.Year -gt 2000)
            {
                if ($information.LastTaskResult -ne 0)
                {
                    throw 'SYSTEM migration failed. Inspect the protected bridge; no existing settings were replaced.'
                }
                break
            }
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($task.State -in @('Running', 'Queued') -or -not (Test-Path -LiteralPath $receiptPath))
        {
            throw 'SYSTEM migration did not complete within its time limit.'
        }
    }

    $stage = 'verify-system-receipt'
    Assert-KeireIdentityPrivatePath $receiptPath
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.schemaVersion -ne 1 -or $receipt.migrationId -cne $manifest.migrationId -or
        $receipt.identitySid -cne 'S-1-5-18' -or $receipt.verified -ne $true -or
        @($receipt.files).Count -ne @($manifest.entries).Count)
    {
        throw 'A verified SYSTEM receipt is required.'
    }
    Assert-KeireIdentityPrivatePath $manifest.destinationRoot
    foreach ($entry in $manifest.entries)
    {
        if ((Get-KeireIdentityHash $entry.sourcePath) -cne $entry.sourceHash)
        {
            throw 'A protected source changed during execution. Prepare a new bridge before activation.'
        }
        $relative = $entry.targetDirectory + '/' + $entry.filename
        $files = @($receipt.files | Where-Object { $_.relativePath -ceq $relative })
        $destination = Join-Path (Join-Path $manifest.destinationRoot $entry.targetDirectory) $entry.filename
        Assert-KeireIdentityPrivatePath $destination
        if ($files.Count -ne 1 -or (Get-KeireIdentityHash $destination) -cne $files[0].ciphertextHash)
        {
            throw 'A protected destination no longer matches its verified receipt.'
        }
    }
    @{ status = 'system-settings-verified'; migrationId = $manifest.migrationId
        settingsFiles = @($manifest.entries).Count; sourcePreserved = $true; liveConfigurationChanged = $false
        next = 'Point staging configuration at the verified destination and run acceptance before live activation.' } | ConvertTo-Json -Compress
}
catch
{
    # Never print exception objects or decrypted settings. Encrypted partial output is retained for inspection.
    [Console]::Error.WriteLine("Service-identity migration failed during $stage. Original settings remain untouched; inspect the protected bridge and destination before retrying.")
    exit 1
}
finally
{
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
    if ($taskRegistered)
    {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($null -ne $task -and $task.State -in @('Running', 'Queued'))
        {
            Stop-ScheduledTask -TaskName $taskName
        }
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
