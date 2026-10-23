[CmdletBinding()]
param ([Parameter(Mandatory)] [string] $FixtureRoot)

# Explicit elevated acceptance runner. Uses only freshly generated synthetic payloads; never reads live settings.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'platform-service-identity-common.psm1') -Force
$plainBytes = $null
$checks = 0
$stage = 'validate-fixture'
$rootCreated = $false
$resultPath = $null
$taskName = $null

function Assert-SystemIdentityFixture([bool] $Condition)
{
    if (-not $Condition)
    {
        throw 'Synthetic identity migration acceptance failed.'
    }
    $script:checks++
}

function Invoke-SystemIdentityCoordinator([string] $Mode, [bool] $ExpectSuccess = $true)
{
    $start = [Diagnostics.ProcessStartInfo]::new($script:powerShellExecutable)
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File',
        (Join-Path $PSScriptRoot 'migrate-platform-service-identity.ps1'), '-Mode', $Mode,
        '-BridgeDirectory', $script:bridgeDirectory))
    {
        $start.ArgumentList.Add($argument)
    }
    if ($Mode -eq 'Prepare')
    {
        foreach ($argument in @('-PlanPath', $script:planPath, '-DestinationRoot', $script:destinationRoot))
        {
            $start.ArgumentList.Add($argument)
        }
    }
    if ($Mode -eq 'Execute')
    {
        $start.ArgumentList.Add('-PowerShellExecutable')
        $start.ArgumentList.Add($script:powerShellExecutable)
    }
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $child = [Diagnostics.Process]::Start($start)
    try
    {
        $output = $child.StandardOutput.ReadToEndAsync()
        $errors = $child.StandardError.ReadToEndAsync()
        if (-not $child.WaitForExit(180000))
        {
            $child.Kill($true)
            throw 'Synthetic coordinator timed out; inspect its own protected fixture before retrying.'
        }
        if ($ExpectSuccess)
        {
            Assert-SystemIdentityFixture ($child.ExitCode -eq 0)
            return ($output.GetAwaiter().GetResult() | ConvertFrom-Json)
        }
        Assert-SystemIdentityFixture ($child.ExitCode -ne 0)
        return $null
    }
    finally
    {
        $child.Dispose()
    }
}

try
{
    Assert-KeireIdentityAdministrator
    if ($PSVersionTable.PSVersion.Major -lt 7)
    {
        throw 'Use PowerShell 7 for this explicit synthetic acceptance run.'
    }
    $FixtureRoot = Resolve-KeireIdentityPath $FixtureRoot
    if ([IO.Path]::GetFileName($FixtureRoot) -cnotmatch '^keire-identity-system-qa-[a-f0-9]{32}$')
    {
        throw 'Choose a fresh fixture directory named keire-identity-system-qa- followed by a GUID without hyphens.'
    }
    New-KeireIdentityPrivateDirectory $FixtureRoot
    $rootCreated = $true
    $resultPath = Join-Path $FixtureRoot 'result.json'
    $powerShellExecutable = Join-Path $PSHOME 'pwsh.exe'
    $bridgeDirectory = Join-Path $FixtureRoot 'Bridge'
    $destinationRoot = Join-Path $FixtureRoot 'SystemSettings'
    $planPath = Join-Path $FixtureRoot 'plan.json'
    $sourceRoot = Join-Path $FixtureRoot 'SyntheticSources'
    New-KeireIdentityPrivateDirectory $sourceRoot
    $catalog = Get-KeireIdentityCatalog
    $sources = @()
    $originals = @()
    $stage = 'create-synthetic-current-user-settings'
    foreach ($kind in @('mysql', 'mail', 'native', 'downloads', 'github', 'workers'))
    {
        $directory = Join-Path $sourceRoot $catalog[$kind].Directory
        New-KeireIdentityPrivateDirectory $directory
        $sources += @{ kind = $kind; sourceDirectory = $directory }
        foreach ($file in $catalog[$kind].Files)
        {
            if ($file.Name -ceq 'account-record-key.dpapi')
            {
                $plainBytes = [Security.Cryptography.RandomNumberGenerator]::GetBytes(32)
            }
            else
            {
                $synthetic = @{ syntheticFixture = $true; kind = $kind; marker = [Guid]::NewGuid().ToString('N')
                    fakeCredential = 'synthetic-acceptance-only-not-a-real-credential' }
                $plainBytes = [Text.Encoding]::UTF8.GetBytes(($synthetic | ConvertTo-Json -Compress))
            }
            $ciphertext = [Security.Cryptography.ProtectedData]::Protect($plainBytes,
                [Text.Encoding]::UTF8.GetBytes($file.Entropy), [Security.Cryptography.DataProtectionScope]::CurrentUser)
            $sourcePath = Join-Path $directory $file.Name
            Write-KeireIdentityNewFile $sourcePath $ciphertext
            $originals += @{ sourcePath = $sourcePath; hash = Get-KeireIdentityHash $sourcePath
                destinationDirectory = $catalog[$kind].Directory; filename = $file.Name; entropy = $file.Entropy }
            [Array]::Clear($plainBytes, 0, $plainBytes.Length)
            $plainBytes = $null
        }
    }
    $plan = @{ schemaVersion = 1; sources = $sources }
    Write-KeireIdentityNewFile $planPath ([Text.Encoding]::UTF8.GetBytes(($plan | ConvertTo-Json -Depth 6)))
    $stage = 'prepare'
    $prepared = Invoke-SystemIdentityCoordinator 'Prepare'
    Assert-SystemIdentityFixture ($prepared.status -ceq 'bridge-prepared' -and $prepared.settingsFiles -eq 7)
    Assert-SystemIdentityFixture (-not (Test-Path -LiteralPath $destinationRoot))
    $taskName = 'KeireIdentityBridge-' + $prepared.migrationId
    $stage = 'execute-system-task'
    $executed = Invoke-SystemIdentityCoordinator 'Execute'
    Assert-SystemIdentityFixture ($executed.status -ceq 'system-settings-verified' -and $executed.settingsFiles -eq 7)
    $stage = 'verify-system-receipt'
    $verified = Invoke-SystemIdentityCoordinator 'Verify'
    Assert-SystemIdentityFixture ($verified.sourcePreserved -eq $true -and $verified.liveConfigurationChanged -eq $false)
    Assert-SystemIdentityFixture ($null -eq (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue))
    $stage = 'verify-source-preservation-and-system-binding'
    foreach ($original in $originals)
    {
        Assert-SystemIdentityFixture ((Get-KeireIdentityHash $original.sourcePath) -ceq $original.hash)
        $destination = Join-Path (Join-Path $destinationRoot $original.destinationDirectory) $original.filename
        $bytes = [IO.File]::ReadAllBytes($destination)
        $userCouldDecrypt = $false
        try
        {
            $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect($bytes,
                [Text.Encoding]::UTF8.GetBytes($original.entropy), [Security.Cryptography.DataProtectionScope]::CurrentUser)
            $userCouldDecrypt = $true
        }
        catch
        {
            # Expected: the original user must not decrypt the SYSTEM CurrentUser payload.
        }
        finally
        {
            if ($null -ne $plainBytes)
            {
                [Array]::Clear($plainBytes, 0, $plainBytes.Length)
                $plainBytes = $null
            }
        }
        Assert-SystemIdentityFixture (-not $userCouldDecrypt)
    }
    $stage = 'verify-no-overwrite'
    Invoke-SystemIdentityCoordinator 'Prepare' $false | Out-Null
    Invoke-SystemIdentityCoordinator 'Execute' $false | Out-Null
    $verifiedAgain = Invoke-SystemIdentityCoordinator 'Verify'
    Assert-SystemIdentityFixture ($verifiedAgain.status -ceq 'system-settings-verified')
    foreach ($original in $originals)
    {
        Assert-SystemIdentityFixture ((Get-KeireIdentityHash $original.sourcePath) -ceq $original.hash)
    }
    $result = @{ status = 'passed'; checks = $checks; settingsFiles = 7; syntheticOnly = $true
        liveConfigurationChanged = $false; originalSourcesPreserved = $true; systemTaskRemoved = $true
        fixtureRoot = $FixtureRoot }
    Write-KeireIdentityNewFile $resultPath ([Text.Encoding]::UTF8.GetBytes(($result | ConvertTo-Json -Depth 4)))
    $result | ConvertTo-Json -Compress
}
catch
{
    $result = @{ status = 'failed'; stage = $stage; checks = $checks; syntheticOnly = $true
        liveConfigurationChanged = $false; fixturePreserved = $rootCreated }
    if ($rootCreated -and $null -ne $resultPath -and -not (Test-Path -LiteralPath $resultPath))
    {
        try
        {
            Write-KeireIdentityNewFile $resultPath ([Text.Encoding]::UTF8.GetBytes(($result | ConvertTo-Json -Depth 4)))
        }
        catch
        {
            # Preserve the failure and all existing fixture data without emitting underlying diagnostics.
        }
    }
    [Console]::Error.WriteLine(($result | ConvertTo-Json -Compress))
    exit 1
}
finally
{
    if ($null -ne $taskName)
    {
        try
        {
            $ownedTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            if ($null -ne $ownedTask)
            {
                if ($ownedTask.State -in @('Running', 'Queued'))
                {
                    Stop-ScheduledTask -TaskName $taskName
                }
                Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
            }
        }
        catch
        {
            [Console]::Error.WriteLine('The synthetic migration task could not be cleaned up. Inspect only the task named in this fixture manifest.')
        }
    }
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
}
