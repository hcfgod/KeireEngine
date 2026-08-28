$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Net.Http

$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Supervisor = Join-Path $Root 'Services\KeireDistributionService\scripts\start-windows-host.ps1'

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $Supervisor,
    [ref] $tokens,
    [ref] $parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "The Windows host supervisor has PowerShell parse errors: $($parseErrors[0].Message)"
}

foreach ($functionName in @(
        'Clear-HttpProbeFailure',
        'Record-HttpProbeException',
        'Record-HttpProbeStatus',
        'Get-HttpProbeFailureDetail',
        'Test-HttpEndpoint',
        'Test-HttpEndpointWithRetry',
        'Test-PortOwnedByExecutable',
        'Test-CaddyPortOwnership',
        'Test-CaddyReady'
    )) {
    $definition = $ast.Find({
            param($node)
            return $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq $functionName
        }, $true)
    if ($null -eq $definition) {
        throw "The Windows host supervisor is missing function '$functionName'."
    }
    . ([scriptblock]::Create($definition.Extent.Text))
}

if (-not ('KeireWindowsHostProbeTestHandler' -as [type])) {
    $probeTestAssemblies = @(
        [Net.Http.HttpClient].Assembly.Location,
        [Net.HttpStatusCode].Assembly.Location
    ) | Select-Object -Unique
    Add-Type -ReferencedAssemblies $probeTestAssemblies -TypeDefinition @'
using System;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;

public sealed class KeireWindowsHostProbeTestHandler : HttpMessageHandler
{
    private readonly int m_FailureResponses;
    private readonly bool m_ThrowException;

    public KeireWindowsHostProbeTestHandler(int failureResponses, bool throwException)
    {
        m_FailureResponses = failureResponses;
        m_ThrowException = throwException;
    }

    public int Calls { get; private set; }

    protected override Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request,
        CancellationToken cancellationToken)
    {
        ++Calls;
        if (m_ThrowException)
        {
            Exception leaf = new InvalidOperationException("sensitive transport detail");
            return Task.FromException<HttpResponseMessage>(
                new HttpRequestException("sensitive request detail", leaf));
        }

        HttpStatusCode status = Calls <= m_FailureResponses
            ? HttpStatusCode.ServiceUnavailable
            : HttpStatusCode.OK;
        return Task.FromResult(new HttpResponseMessage(status));
    }
}
'@
}

$script:FirstHttpProbeFailure = $null
$script:ProbeConnections = @{}
$script:ProbeProcessPaths = @{}

function Get-NetTCPConnection {
    [CmdletBinding()]
    param(
        [string] $State,
        [int] $LocalPort
    )

    if ($script:ProbeConnections.ContainsKey($LocalPort)) {
        foreach ($processId in @($script:ProbeConnections[$LocalPort])) {
            [pscustomobject]@{ OwningProcess = $processId }
        }
    }
}

function Get-Process {
    [CmdletBinding()]
    param([int] $Id)

    if (-not $script:ProbeProcessPaths.ContainsKey($Id)) {
        throw [Management.Automation.ProcessCommandException]::new("Unknown test process '$Id'.")
    }
    return [pscustomobject]@{ Path = $script:ProbeProcessPaths[$Id] }
}

$successHandler = [KeireWindowsHostProbeTestHandler]::new(1, $false)
$successClient = [Net.Http.HttpClient]::new($successHandler)
try {
    $successClient.Timeout = [TimeSpan]::FromSeconds(5)
    Assert-True (Test-HttpEndpointWithRetry $successClient 'http://127.0.0.1/test' `
            'transient test endpoint' 3 0) `
        'A transient HTTP failure did not recover within the bounded retry count.'
    Assert-True ($successHandler.Calls -eq 2) 'The transient HTTP probe used the wrong attempt count.'
    Assert-True ([string]::IsNullOrEmpty((Get-HttpProbeFailureDetail))) `
        'A successful retry retained stale failure metadata.'
}
finally {
    $successClient.Dispose()
    $successHandler.Dispose()
}

$failureHandler = [KeireWindowsHostProbeTestHandler]::new(0, $true)
$failureClient = [Net.Http.HttpClient]::new($failureHandler)
try {
    $failureClient.Timeout = [TimeSpan]::FromSeconds(5)
    Assert-True (-not (Test-HttpEndpointWithRetry $failureClient 'http://127.0.0.1/test' `
                'failing test endpoint' 1 0)) `
        'An exception-producing HTTP probe unexpectedly succeeded.'
    $failureDetail = Get-HttpProbeFailureDetail
    Assert-True ($failureDetail.Contains('System.InvalidOperationException')) `
        'The HTTP probe did not retain the sanitized leaf exception type.'
    Assert-True ($failureDetail.Contains('0x')) 'The HTTP probe did not retain a sanitized HRESULT.'
    Assert-True (-not $failureDetail.Contains('sensitive')) `
        'The HTTP probe exposed an exception message instead of sanitized metadata.'
}
finally {
    $failureClient.Dispose()
    $failureHandler.Dispose()
}

$caddyPath = [IO.Path]::GetFullPath('C:\KeireProbeFixture\caddy.exe')
$script:ProbeConnections = @{ 5100 = 4242; 5101 = 4242; 5102 = 4242 }
$script:ProbeProcessPaths = @{ 4242 = $caddyPath }
Assert-True (Test-CaddyPortOwnership 5100 5101 5102 $caddyPath) `
    'Exact Caddy ownership was rejected.'
$script:ProbeProcessPaths[4242] = [IO.Path]::GetFullPath('C:\KeireProbeFixture\other.exe')
Assert-True (-not (Test-CaddyPortOwnership 5100 5101 5102 $caddyPath)) `
    'A foreign Caddy listener owner was accepted.'
$script:ProbeProcessPaths[4242] = $caddyPath
$script:ProbeConnections[5102] = @(4242, 4343)
$script:ProbeProcessPaths[4343] = [IO.Path]::GetFullPath('C:\KeireProbeFixture\other.exe')
Assert-True (-not (Test-CaddyPortOwnership 5100 5101 5102 $caddyPath)) `
    'A loopback admin port shared with a foreign listener owner was accepted.'
$script:ProbeConnections[5102] = 4242

$caddyHandler = [KeireWindowsHostProbeTestHandler]::new(0, $false)
$caddyClient = [Net.Http.HttpClient]::new($caddyHandler)
try {
    $caddyClient.Timeout = [TimeSpan]::FromSeconds(5)
    Assert-True (Test-CaddyReady $caddyClient 'http://127.0.0.1:5102/config/' `
            5100 5101 5102 $caddyPath 1 0) `
        'Owned Caddy listeners with a healthy loopback admin endpoint were rejected.'
    Assert-True ($caddyHandler.Calls -eq 1) 'The Caddy readiness probe did not use its loopback admin endpoint.'

    $script:ProbeProcessPaths[4242] = [IO.Path]::GetFullPath('C:\KeireProbeFixture\other.exe')
    Assert-True (-not (Test-CaddyReady $caddyClient 'http://127.0.0.1:5102/config/' `
                5100 5101 5102 $caddyPath 1 0)) `
        'Caddy readiness accepted a foreign listener owner.'
    Assert-True ($caddyHandler.Calls -eq 1) `
        'Caddy readiness contacted the admin endpoint before proving exact listener ownership.'
}
finally {
    $caddyClient.Dispose()
    $caddyHandler.Dispose()
}

$source = Get-Content -LiteralPath $Supervisor -Raw
foreach ($requiredContract in @(
        '$localCaddyReadyUri',
        'http://127.0.0.1:$caddyAdminPort/config/',
        'Test-CaddyPortOwnership',
        'Wait-CaddyReady',
        '$env:KEIRE_CADDY_ADMIN',
        '[TimeSpan]::FromSeconds(5)',
        'Get-HttpProbeFailureDetail'
    )) {
    Assert-True ($source.Contains($requiredContract)) `
        "The Windows host supervisor is missing '$requiredContract'."
}
foreach ($forbiddenContract in @(
        '$publicReadyUri',
        'ServerCertificateCustomValidationCallback',
        'DangerousAcceptAnyServerCertificateValidator',
        'SkipCertificateCheck'
    )) {
    Assert-True (-not $source.Contains($forbiddenContract)) `
        "The Windows host supervisor retains forbidden probe contract '$forbiddenContract'."
}

Write-Host 'Windows host supervisor probe checks passed.'
