<#
.SYNOPSIS
Runs an isolated real broker/worker/signer acceptance with an externally supplied DB harness.
.DESCRIPTION
The Node harness receives --worker-config <JSON> and --result <JSON>. It must
merge the public worker configuration with its protected QA settings, create
only isolated database fixtures, invoke the actual broker/signer integration,
clean up its own children and fixtures, and write a sanitized result containing
status: "passed" on success. Never pass credentials or capability URLs through
IntegrationArguments; use protected settings paths. The helper creates no
synthetic exchange requests, firewall rules, services, or scheduled tasks.
An existing protected key and matching isolated firewall namespace are required.
SettingsDirectory is read through CurrentUser DPAPI and delivered only through
the private Node stdin pipe. The output directory must be new. Failed evidence is retained.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Validator,
    [Parameter(Mandatory)] [string] $AssetTool,
    [Parameter(Mandatory)] [string] $MalwareScanner,
    [Parameter(Mandatory)] [string] $Dotnet,
    [Parameter(Mandatory)] [string] $ManagedApi,
    [Parameter(Mandatory)] [string] $ProtectedAttestationKey,
    [Parameter(Mandatory)] [string] $AttestationPublicKey,
    [Parameter(Mandatory)] [string] $IntegrationScript,
    [Parameter(Mandatory)] [string] $Node,
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string[]] $IntegrationArguments = @(),
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}$')]
    [string] $FirewallRulePrefix = 'Keire Marketplace Validator',
    [ValidatePattern('^$|^[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}$')]
    [string] $FirewallRuleGroup = '',
    [string] $FirewallAttestation,
    [ValidateRange(60, 1800)] [int] $TimeoutSeconds = 900,
    [ValidateRange(1, 30)] [int] $MaximumDefinitionAgeDays = 7
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows)
{
    throw 'This orchestration helper requires Windows PowerShell 7.'
}
foreach ($path in @($Validator, $AssetTool, $MalwareScanner, $Dotnet, $ManagedApi,
        $ProtectedAttestationKey, $AttestationPublicKey, $IntegrationScript, $Node))
{
    if (-not [IO.Path]::IsPathFullyQualified($path) -or -not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        throw 'Every executable, script and key input must be an existing absolute file path.'
    }
}
if (-not [IO.Path]::IsPathFullyQualified($OutputDirectory) -or (Test-Path -LiteralPath $OutputDirectory))
{
    throw 'Use a new absolute output directory; existing evidence is preserved.'
}
function Assert-IntegrationArguments([string[]] $Arguments)
{
    foreach ($argument in $Arguments)
    {
        if ($argument -match '^--(?:worker-config|result)(?:=|$)')
        {
            throw 'IntegrationArguments must not override reserved worker-config or result options.'
        }
    }
}

Assert-IntegrationArguments $IntegrationArguments
if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory) -or
    -not (Test-Path -LiteralPath (Join-Path $SettingsDirectory 'mysql-settings.dpapi') -PathType Leaf))
{
    throw 'An existing absolute protected MySQL settings directory is required.'
}
$fixtureRoot = [IO.Path]::GetFullPath($OutputDirectory)
$ancestor = Split-Path -Parent $fixtureRoot
while ($ancestor)
{
    if ((Test-Path -LiteralPath $ancestor) -and
        ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint))
    {
        throw 'The acceptance output must not traverse a reparse point.'
    }
    $ancestor = Split-Path -Parent $ancestor
}
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetAccessRuleProtection($true, $false)
foreach ($identity in @([Security.Principal.WindowsIdentity]::GetCurrent().User,
        [Security.Principal.SecurityIdentifier]::new('S-1-5-18')))
{
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
        $identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
}
Set-Acl -LiteralPath $fixtureRoot -AclObject $acl
$exchange = Join-Path $fixtureRoot 'exchange'
$work = Join-Path $fixtureRoot 'work'
foreach ($directory in @($exchange, $work, (Join-Path $exchange 'packages'),
        (Join-Path $exchange 'requests'), (Join-Path $exchange 'reports'), (Join-Path $exchange 'evidence')))
{
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}

function Start-OwnedProcess([string] $Executable, [string[]] $Arguments, [switch] $PrivateInput)
{
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = [bool]$PrivateInput
    foreach ($argument in $Arguments)
    {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    return [pscustomobject]@{
        Process = $process
        Output = $process.StandardOutput.ReadToEndAsync()
        Errors = $process.StandardError.ReadToEndAsync()
    }
}

function Write-PrivateSettings($Owned, [byte[]] $SettingsBytes)
{
    if ($SettingsBytes.Length -lt 2 -or $SettingsBytes.Length -gt 65536)
    {
        throw 'Protected MySQL settings have an invalid size.'
    }
    $write = $Owned.Process.StandardInput.BaseStream.WriteAsync($SettingsBytes, 0, $SettingsBytes.Length)
    if (-not $write.Wait(10000))
    {
        throw 'Private MySQL settings delivery timed out.'
    }
    $Owned.Process.StandardInput.Close()
}

function Stop-OwnedProcess($Owned)
{
    if ($null -eq $Owned)
    {
        return
    }
    if (-not $Owned.Process.HasExited)
    {
        $Owned.Process.Kill($true)
    }
    if (-not $Owned.Process.WaitForExit(10000))
    {
        throw 'An owned acceptance process did not exit within the cleanup deadline.'
    }
    $Owned.Process.Dispose()
}

function Assert-ScannerFreshness([string] $VersionOutput, [int] $MaximumAgeDays, [DateTime] $Now)
{
    $versionParts = $VersionOutput.Trim().Split('/', 3)
    $definitionDate = [DateTime]::MinValue
    if ($versionParts.Count -ne 3 -or
        -not [DateTime]::TryParseExact(($versionParts[2] -replace '\s+', ' '),
            [string[]]@('ddd MMM d HH:mm:ss yyyy', 'ddd MMM dd HH:mm:ss yyyy'),
            [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AllowWhiteSpaces,
            [ref]$definitionDate))
    {
        throw 'ClamAV did not report a verifiable definition date.'
    }
    $ageDays = ($Now - $definitionDate).TotalDays
    if ($ageDays -lt -1 -or $ageDays -gt $MaximumAgeDays)
    {
        throw 'ClamAV definitions are stale or future-dated.'
    }
    return $ageDays
}

$launcher = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Deployment/start-windows-marketplace-validator.ps1'))
$workerArguments = @('-NoProfile', '-NonInteractive', '-File', $launcher,
    '-Validator', $Validator, '-ExchangeRoot', $exchange, '-WorkRoot', $work,
    '-AssetTool', $AssetTool, '-MalwareScanner', $MalwareScanner, '-Dotnet', $Dotnet,
    '-ManagedApi', $ManagedApi, '-ProtectedAttestationKey', $ProtectedAttestationKey,
    '-FirewallRulePrefix', $FirewallRulePrefix, '-FirewallRuleGroup', $FirewallRuleGroup)
if ($FirewallAttestation)
{
    $workerArguments += @('-FirewallAttestation', $FirewallAttestation)
}
$configurationPath = Join-Path $fixtureRoot 'worker-config.json'
$resultPath = Join-Path $fixtureRoot 'integration-result.json'
$configuration = @{
    schemaVersion = 1
    isolatedFixture = $true
    exchangeRoot = $exchange
    validatorPublicKey = $AttestationPublicKey
    validatorFingerprint = (Get-FileHash -LiteralPath $Validator -Algorithm SHA256).Hash.ToLowerInvariant()
}
[IO.File]::WriteAllText($configurationPath, ($configuration | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$settingsBytes = $null
$scanner = $null
$preflight = $null
$worker = $null
$integration = $null
$stage = 'scanner freshness'
$passed = $false
try
{
    $scanner = Start-OwnedProcess $MalwareScanner @('--version')
    if (-not $scanner.Process.WaitForExit(15000) -or $scanner.Process.ExitCode -ne 0)
    {
        throw 'ClamAV definition version check failed or timed out.'
    }
    $ageDays = Assert-ScannerFreshness $scanner.Output.Result $MaximumDefinitionAgeDays ([DateTime]::Now)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'scanner-freshness.json'), (@{
        status = 'passed'
        definitionAgeDays = $ageDays
        maximumAgeDays = $MaximumDefinitionAgeDays
    } | ConvertTo-Json))
    $stage = 'preflight'
    $preflight = Start-OwnedProcess (Get-Process -Id $PID).Path ($workerArguments + '-ValidateOnly')
    if (-not $preflight.Process.WaitForExit(30000) -or $preflight.Process.ExitCode -ne 0)
    {
        throw 'Guarded offline worker preflight failed.'
    }
    $stage = 'protected settings'
    $encryptedSettings = [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'mysql-settings.dpapi'))
    try
    {
        $settingsBytes = [Security.Cryptography.ProtectedData]::Unprotect(
            $encryptedSettings,
            [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
            [Security.Cryptography.DataProtectionScope]::CurrentUser)
    }
    finally
    {
        [Array]::Clear($encryptedSettings, 0, $encryptedSettings.Length)
    }
    $stage = 'integration'
    $worker = Start-OwnedProcess (Get-Process -Id $PID).Path $workerArguments
    $integration = Start-OwnedProcess $Node (@($IntegrationScript) + $IntegrationArguments +
        @('--worker-config', $configurationPath, '--result', $resultPath)) -PrivateInput
    Write-PrivateSettings $integration $settingsBytes
    [Array]::Clear($settingsBytes, 0, $settingsBytes.Length)
    $settingsBytes = $null
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $integration.Process.WaitForExit(250))
    {
        if ($worker.Process.HasExited)
        {
            throw 'The guarded offline worker exited before integration completed.'
        }
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw 'Marketplace integration exceeded its bounded acceptance time.'
        }
    }
    if ($integration.Process.ExitCode -ne 0 -or $worker.Process.HasExited -or
        -not (Test-Path -LiteralPath $resultPath -PathType Leaf))
    {
        throw 'Marketplace integration did not produce successful acceptance evidence.'
    }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($result.status -cne 'passed')
    {
        throw 'Marketplace integration reported failed acceptance.'
    }
    $passed = $true
}
catch
{
    # Child diagnostics can contain signed download capabilities. Persist only
    # the safe stage; the integration harness owns sanitized detailed evidence.
    throw "Isolated marketplace acceptance failed during $stage."
}
finally
{
    if ($null -ne $settingsBytes)
    {
        [Array]::Clear($settingsBytes, 0, $settingsBytes.Length)
    }
    $cleanupFailed = $false
    foreach ($owned in @($integration, $worker, $preflight, $scanner))
    {
        try
        {
            Stop-OwnedProcess $owned
        }
        catch
        {
            $cleanupFailed = $true
        }
    }
    $summary = @{
        status = $(if ($passed -and -not $cleanupFailed) { 'passed' } else { 'failed' })
        stage = $stage
        ownedProcessesStopped = -not $cleanupFailed
        productionChanged = $false
        syntheticRequestsCreated = $false
    }
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'orchestration-result.json'), ($summary | ConvertTo-Json))
    if ($cleanupFailed)
    {
        throw 'Owned process cleanup was incomplete; inspect the private acceptance evidence.'
    }
}
Write-Output 'Isolated marketplace pipeline acceptance passed; owned processes stopped.'
