[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Validator,
    [Parameter(Mandatory)] [string] $Package,
    [Parameter(Mandatory)] [string] $AssetTool,
    [Parameter(Mandatory)] [string] $MalwareScanner,
    [Parameter(Mandatory)] [string] $Dotnet,
    [Parameter(Mandatory)] [string] $ManagedApi,
    [Parameter(Mandatory)] [string] $ProtectedAttestationKey,
    [Parameter(Mandatory)] [string] $AttestationPublicKey,
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}$')]
    [string]$FirewallRulePrefix = 'Keire Marketplace Validator',

    [ValidatePattern('^$|^[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}$')]
    [string]$FirewallRuleGroup = '',

    [string] $FirewallAttestation,
    [ValidateSet('clean', 'blocked')] [string] $ExpectedOutcome = 'clean',
    [ValidateRange(30, 1800)] [int] $TimeoutSeconds = 900,
    [ValidateRange(1, 30)] [int] $MaximumDefinitionAgeDays = 7
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows)
{
    throw 'This acceptance runner requires the guarded Windows worker launcher.'
}
foreach ($path in @($Validator, $Package, $AssetTool, $MalwareScanner, $Dotnet, $ManagedApi,
        $ProtectedAttestationKey, $AttestationPublicKey, $OutputDirectory))
{
    if (-not [IO.Path]::IsPathFullyQualified($path))
    {
        throw 'Use absolute paths for every acceptance input and output.'
    }
}
if (Test-Path -LiteralPath $OutputDirectory)
{
    throw 'Use a new output directory; previous evidence is preserved.'
}
$fixtureRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
$system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetAccessRuleProtection($true, $false)
foreach ($principal in @($identity, $system))
{
    $rule = [Security.AccessControl.FileSystemAccessRule]::new($principal, 'FullControl',
        'ContainerInherit,ObjectInherit', 'None', 'Allow')
    $acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $fixtureRoot -AclObject $acl
$exchange = Join-Path $fixtureRoot 'exchange'
$work = Join-Path $fixtureRoot 'work'
foreach ($directory in @($exchange, $work, (Join-Path $exchange 'packages'), (Join-Path $exchange 'requests'),
        (Join-Path $exchange 'reports'), (Join-Path $exchange 'evidence')))
{
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
$launcher = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Deployment/start-windows-marketplace-validator.ps1'))
$arguments = @('-NoProfile', '-NonInteractive', '-File', $launcher, '-Validator', $Validator,
    '-ExchangeRoot', $exchange, '-WorkRoot', $work, '-AssetTool', $AssetTool,
    '-MalwareScanner', $MalwareScanner, '-Dotnet', $Dotnet, '-ManagedApi', $ManagedApi,
    '-ProtectedAttestationKey', $ProtectedAttestationKey,
    '-FirewallRulePrefix', $FirewallRulePrefix, '-FirewallRuleGroup', $FirewallRuleGroup)
if ($FirewallAttestation)
{
    $arguments += @('-FirewallAttestation', $FirewallAttestation)
}

function Start-GuardedWorker([string[]] $WorkerArguments)
{
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $WorkerArguments)
    {
        $start.ArgumentList.Add($argument)
    }
    return [Diagnostics.Process]::Start($start)
}

$scannerStart = [Diagnostics.ProcessStartInfo]::new($MalwareScanner)
$scannerStart.UseShellExecute = $false
$scannerStart.CreateNoWindow = $true
$scannerStart.RedirectStandardOutput = $true
$scannerStart.RedirectStandardError = $true
$scannerStart.ArgumentList.Add('--version')
$scanner = [Diagnostics.Process]::Start($scannerStart)
$scannerOutput = $scanner.StandardOutput.ReadToEndAsync()
$scannerErrors = $scanner.StandardError.ReadToEndAsync()
if (-not $scanner.WaitForExit(15000))
{
    $scanner.Kill($true)
    throw 'ClamAV definition version check timed out.'
}
$versionParts = $scannerOutput.Result.Trim().Split('/', 3)
$definitionDate = [DateTime]::MinValue
if ($scanner.ExitCode -ne 0 -or $versionParts.Count -ne 3 -or
    -not [DateTime]::TryParseExact(($versionParts[2] -replace '\s+', ' '),
        [string[]]@('ddd MMM d HH:mm:ss yyyy', 'ddd MMM dd HH:mm:ss yyyy'),
        [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AllowWhiteSpaces,
        [ref]$definitionDate))
{
    throw 'ClamAV did not report a verifiable definition date.'
}
$definitionAgeDays = ([DateTime]::Now - $definitionDate).TotalDays
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'scanner-version.json'), (@{
    version = $scannerOutput.Result.Trim()
    definitionAgeDays = $definitionAgeDays
    maximumAgeDays = $MaximumDefinitionAgeDays
} | ConvertTo-Json))
if ($definitionAgeDays -lt -1 -or $definitionAgeDays -gt $MaximumDefinitionAgeDays)
{
    throw 'ClamAV definitions are stale or future-dated. Update definitions before claiming worker acceptance.'
}

$validation = Start-GuardedWorker ($arguments + '-ValidateOnly')
$validationOutput = $validation.StandardOutput.ReadToEndAsync()
$validationErrors = $validation.StandardError.ReadToEndAsync()
if (-not $validation.WaitForExit(30000))
{
    $validation.Kill($true)
    throw 'Worker isolation preflight timed out.'
}
if ($validation.ExitCode -ne 0)
{
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'preflight-error.txt'), $validationErrors.Result)
    throw 'Worker isolation preflight failed. Inspect the private preflight error file; no worker was started.'
}

$uploadId = [Guid]::NewGuid().ToString('D')
$versionId = [Guid]::NewGuid().ToString('D')
$packageCopy = Join-Path $exchange "packages/$uploadId.keireassetpackage"
Copy-Item -LiteralPath $Package -Destination $packageCopy
$packageSize = (Get-Item -LiteralPath $packageCopy).Length
$packageHash = (Get-FileHash -LiteralPath $packageCopy -Algorithm SHA256).Hash.ToLowerInvariant()
$workerHash = (Get-FileHash -LiteralPath $Validator -Algorithm SHA256).Hash.ToLowerInvariant()
$job = @{
    schemaVersion = 2
    uploadId = $uploadId
    versionId = $versionId
    storageBucket = 'marketplace-quarantine'
    storagePath = "qa/$uploadId/package.keireassetpackage"
    expectedSizeBytes = $packageSize
    expectedSha256 = $packageHash
}
$pendingRequest = Join-Path $exchange "requests/$uploadId.pending"
[IO.File]::WriteAllText($pendingRequest, ($job | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $pendingRequest -Destination (Join-Path $exchange "requests/$uploadId.json")
$report = Join-Path $exchange "reports/$uploadId.json"
$evidence = Join-Path $exchange "evidence/$uploadId.json"
$worker = Start-GuardedWorker $arguments
$workerOutput = $worker.StandardOutput.ReadToEndAsync()
$workerErrors = $worker.StandardError.ReadToEndAsync()
try
{
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not (Test-Path -LiteralPath $report -PathType Leaf))
    {
        if ($worker.HasExited)
        {
            throw 'The real offline worker exited before producing a report.'
        }
        if ([DateTime]::UtcNow -ge $deadline)
        {
            throw 'The real offline worker exceeded its bounded acceptance time.'
        }
        Start-Sleep -Milliseconds 250
    }
    $verification = @{
        report = $report
        evidence = $evidence
        publicKey = $AttestationPublicKey
        expectedOutcome = $ExpectedOutcome
        workerSha256 = $workerHash
        upload = @{
            id = $uploadId
            version_id = $versionId
            storage_bucket = $job.storageBucket
            storage_path = $job.storagePath
            expected_sha256 = $packageHash
            expected_size_bytes = $packageSize
        }
    }
    $verifyStart = [Diagnostics.ProcessStartInfo]::new((Get-Command node.exe).Source)
    $verifyStart.UseShellExecute = $false
    $verifyStart.CreateNoWindow = $true
    $verifyStart.RedirectStandardInput = $true
    $verifyStart.RedirectStandardOutput = $true
    $verifyStart.RedirectStandardError = $true
    $verifyStart.ArgumentList.Add((Join-Path $PSScriptRoot 'VerifyOfflineWorkerReport.mjs'))
    $verifier = [Diagnostics.Process]::Start($verifyStart)
    $verifiedOutput = $verifier.StandardOutput.ReadToEndAsync()
    $verifiedErrors = $verifier.StandardError.ReadToEndAsync()
    $verifier.StandardInput.Write(($verification | ConvertTo-Json -Depth 8 -Compress))
    $verifier.StandardInput.Close()
    if (-not $verifier.WaitForExit(30000))
    {
        $verifier.Kill($true)
        throw 'Offline worker signature verification timed out.'
    }
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'verification.json'), $verifiedOutput.Result)
    if ($verifier.ExitCode -ne 0)
    {
        [IO.File]::WriteAllText((Join-Path $fixtureRoot 'verification-error.txt'), $verifiedErrors.Result)
        throw 'The actual worker report failed signature, integrity, or expected-result acceptance.'
    }
    Write-Output $verifiedOutput.Result
}
finally
{
    if (-not $worker.HasExited)
    {
        $worker.Kill($true)
    }
    $worker.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'worker.stdout.log'), $workerOutput.Result)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'worker.stderr.log'), $workerErrors.Result)
}
