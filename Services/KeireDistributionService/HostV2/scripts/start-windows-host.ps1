[CmdletBinding()]
param(
    [string] $SettingsPath = '',
    [switch] $KeepAlive,
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http

function Get-RequiredSetting([object] $Settings, [string] $Name) {
    $property = $Settings.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "Host settings are missing '$Name'."
    }
    return $property.Value
}

function Resolve-ConfiguredPath([string] $Value, [string] $BaseDirectory) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw 'A configured path cannot be empty.'
    }
    if ([IO.Path]::IsPathRooted($Value)) {
        return [IO.Path]::GetFullPath($Value)
    }
    return [IO.Path]::GetFullPath((Join-Path $BaseDirectory $Value))
}

function Test-HttpEndpoint([Net.Http.HttpClient] $Client, [string] $Uri) {
    try {
        $response = $Client.GetAsync($Uri).GetAwaiter().GetResult()
        try {
            return [int] $response.StatusCode -ge 200 -and [int] $response.StatusCode -lt 300
        }
        finally {
            $response.Dispose()
        }
    }
    catch {
        return $false
    }
}

function Wait-HttpEndpoint(
    [Net.Http.HttpClient] $Client,
    [string] $Uri,
    [Diagnostics.Process] $Process,
    [int] $TimeoutSeconds
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($Process.HasExited) {
            throw "The process for '$Uri' exited with code $($Process.ExitCode)."
        }
        if (Test-HttpEndpoint $Client $Uri) {
            return
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "The process did not make '$Uri' healthy within $TimeoutSeconds seconds."
}

function Test-ListeningPort([int] $Port) {
    return $null -ne (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue |
        Select-Object -First 1)
}

if ([string]::IsNullOrWhiteSpace($SettingsPath)) {
    $SettingsPath = Join-Path $PSScriptRoot 'host-settings.json'
}
$SettingsPath = [IO.Path]::GetFullPath($SettingsPath)
if (-not (Test-Path -LiteralPath $SettingsPath -PathType Leaf)) {
    throw "Windows distribution host settings do not exist: '$SettingsPath'."
}

$settingsDirectory = Split-Path -Parent $SettingsPath
$settings = Get-Content -LiteralPath $SettingsPath -Raw | ConvertFrom-Json
$schemaVersion = [int] (Get-RequiredSetting $settings 'schemaVersion')
if ($schemaVersion -notin @(1, 2)) {
    throw "Unsupported Windows distribution host settings schema '$schemaVersion'."
}

$hostName = [string] (Get-RequiredSetting $settings 'host')
if ($hostName -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$' -or $hostName.Contains('..')) {
    throw "The configured distribution host '$hostName' is not a DNS name."
}
$httpPort = [int] (Get-RequiredSetting $settings 'httpPort')
$httpsPort = [int] (Get-RequiredSetting $settings 'httpsPort')
foreach ($port in @($httpPort, $httpsPort)) {
    if ($port -lt 1 -or $port -gt 65535) {
        throw "The configured Caddy port '$port' is outside 1-65535."
    }
}
if ($httpPort -eq $httpsPort) {
    throw 'The configured Caddy HTTP and HTTPS ports must be different.'
}

$storageRoot = Resolve-ConfiguredPath ([string] (Get-RequiredSetting $settings 'storageRoot')) $settingsDirectory
$serviceExecutable = Resolve-ConfiguredPath `
    ([string] (Get-RequiredSetting $settings 'serviceExecutable')) $settingsDirectory
$caddyExecutable = Resolve-ConfiguredPath `
    ([string] (Get-RequiredSetting $settings 'caddyExecutable')) $settingsDirectory
$caddyConfig = Resolve-ConfiguredPath ([string] (Get-RequiredSetting $settings 'caddyConfig')) $settingsDirectory
$logDirectory = Resolve-ConfiguredPath ([string] (Get-RequiredSetting $settings 'logDirectory')) $settingsDirectory
$webRoot = ''
$nodeExecutable = ''
$supabaseUrl = ''
$supabasePublishableKey = ''
if ($schemaVersion -ge 2) {
    $webRoot = Resolve-ConfiguredPath ([string] (Get-RequiredSetting $settings 'webRoot')) $settingsDirectory
    $nodeExecutable = Resolve-ConfiguredPath `
        ([string] (Get-RequiredSetting $settings 'nodeExecutable')) $settingsDirectory
    $supabaseUrl = [string] (Get-RequiredSetting $settings 'supabaseUrl')
    $supabasePublishableKey = [string] (Get-RequiredSetting $settings 'supabasePublishableKey')
    if ($supabaseUrl -notmatch '^https://[A-Za-z0-9.-]+/?$') {
        throw "The configured Supabase URL must be an HTTPS origin: '$supabaseUrl'."
    }
    if ($supabasePublishableKey -notmatch '^sb_publishable_[A-Za-z0-9_-]{16,}$') {
        throw 'The configured Supabase publishable key is invalid.'
    }
}

foreach ($requiredFile in @($serviceExecutable, $caddyExecutable, $caddyConfig)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "A required Windows distribution host file does not exist: '$requiredFile'."
    }
}
if ($schemaVersion -ge 2) {
    foreach ($requiredFile in @($nodeExecutable, (Join-Path $webRoot 'dist\server\entry.mjs'))) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "A required Windows web platform file does not exist: '$requiredFile'."
        }
    }
}
if (-not (Test-Path -LiteralPath $storageRoot -PathType Container)) {
    throw "The distribution storage root does not exist: '$storageRoot'."
}
[IO.Directory]::CreateDirectory($logDirectory) | Out-Null

if ($ValidateOnly) {
    Write-Host "Windows distribution host settings are valid for https://$hostName/."
    exit 0
}

$handler = [Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$httpClient = [Net.Http.HttpClient]::new($handler)
$httpClient.Timeout = [TimeSpan]::FromSeconds(2)
$localReadyUri = 'http://127.0.0.1:5088/health/ready'
$localWebReadyUri = 'http://127.0.0.1:4321/health/'
$publicReadyUri = "https://$hostName/health/ready"
$startupTimeoutSeconds = 45

function Write-HostEvent([string] $Message) {
    $timestamp = [DateTimeOffset]::Now.ToString('o')
    [IO.File]::AppendAllText(
        (Join-Path $logDirectory 'host-supervisor.log'),
        "$timestamp $Message$([Environment]::NewLine)",
        [Text.UTF8Encoding]::new($false))
}

function Start-DistributionService {
    if (Test-ListeningPort 5088) {
        if (-not (Test-HttpEndpoint $httpClient $localReadyUri)) {
            Write-HostEvent 'Port 5088 is listening, but the distribution service is not ready; leaving it untouched.'
        }
        return
    }

    $env:ASPNETCORE_ENVIRONMENT = 'Production'
    $env:Distribution__StorageRoot = $storageRoot
    $process = Start-Process -FilePath $serviceExecutable -WorkingDirectory $settingsDirectory `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory 'service-stdout.log') `
        -RedirectStandardError (Join-Path $logDirectory 'service-stderr.log')
    Write-HostEvent "Started the distribution service as process $($process.Id)."
    Wait-HttpEndpoint $httpClient $localReadyUri $process $startupTimeoutSeconds
}

function Start-WebPlatform {
    if ($schemaVersion -lt 2) {
        return
    }
    if (Test-ListeningPort 4321) {
        if (-not (Test-HttpEndpoint $httpClient $localWebReadyUri)) {
            Write-HostEvent 'Port 4321 is listening, but the web platform is not healthy; leaving it untouched.'
        }
        return
    }

    $env:NODE_ENV = 'production'
    $env:HOST = '127.0.0.1'
    $env:PORT = '4321'
    $env:KEIRE_DISTRIBUTION_HEALTH_URL = $localReadyUri
    $env:PUBLIC_SUPABASE_URL = $supabaseUrl.TrimEnd('/')
    $env:PUBLIC_SUPABASE_PUBLISHABLE_KEY = $supabasePublishableKey
    $process = Start-Process -FilePath $nodeExecutable -ArgumentList 'dist/server/entry.mjs' `
        -WorkingDirectory $webRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory 'web-stdout.log') `
        -RedirectStandardError (Join-Path $logDirectory 'web-stderr.log')
    Write-HostEvent "Started the web platform as process $($process.Id)."
    Wait-HttpEndpoint $httpClient $localWebReadyUri $process $startupTimeoutSeconds
}

function Start-CaddyProxy {
    $httpListening = Test-ListeningPort $httpPort
    $httpsListening = Test-ListeningPort $httpsPort
    if ($httpListening -or $httpsListening) {
        if (-not ($httpListening -and $httpsListening)) {
            Write-HostEvent `
                "Only one configured Caddy port is listening; leaving ports $httpPort and $httpsPort untouched."
            return
        }
        if (-not (Test-HttpEndpoint $httpClient $publicReadyUri)) {
            Write-HostEvent "Port $httpsPort is listening, but the public HTTPS endpoint is not ready; leaving it untouched."
        }
        return
    }

    $env:KEIRE_DISTRIBUTION_HOST = $hostName
    $env:KEIRE_CADDY_HTTP_PORT = [string] $httpPort
    $env:KEIRE_CADDY_HTTPS_PORT = [string] $httpsPort
    $env:KEIRE_CADDY_LOG = Join-Path $logDirectory 'distribution-access.json'
    $arguments = "run --config `"$caddyConfig`" --adapter caddyfile"
    $process = Start-Process -FilePath $caddyExecutable -ArgumentList $arguments `
        -WorkingDirectory $settingsDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDirectory 'caddy-stdout.log') `
        -RedirectStandardError (Join-Path $logDirectory 'caddy-stderr.log')
    Write-HostEvent "Started Caddy as process $($process.Id)."
    Wait-HttpEndpoint $httpClient $publicReadyUri $process $startupTimeoutSeconds
}

$mutex = [Threading.Mutex]::new($false, 'Local\KeireDistributionHostSupervisor')
$ownsMutex = $false
try {
    $ownsMutex = $mutex.WaitOne(0)
    if (-not $ownsMutex) {
        Write-HostEvent 'Another distribution host supervisor is already running.'
        exit 0
    }

    do {
        try {
            Start-DistributionService
            Start-WebPlatform
            Start-CaddyProxy
        }
        catch {
            Write-HostEvent "Host startup check failed: $($_.Exception.Message)"
            if (-not $KeepAlive) {
                throw
            }
        }

        if ($KeepAlive) {
            Start-Sleep -Seconds 15
        }
    } while ($KeepAlive)
}
finally {
    $httpClient.Dispose()
    $handler.Dispose()
    if ($ownsMutex) {
        $mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}
