[CmdletBinding()]
param(
    [string] $SettingsPath = '',
    [switch] $KeepAlive,
    [switch] $ProbeOnly,
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

function Test-PortOwnedByExecutable([int] $Port, [string] $ExecutablePath) {
    $resolvedExecutable = [IO.Path]::GetFullPath($ExecutablePath)
    foreach ($connection in Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        try {
            $process = Get-Process -Id $connection.OwningProcess -ErrorAction Stop
            if ([string]::Equals($process.Path, $resolvedExecutable, [StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
        catch [System.ComponentModel.Win32Exception] {
            continue
        }
        catch [Microsoft.PowerShell.Commands.ProcessCommandException] {
            continue
        }
    }
    return $false
}

function Test-PortOwnedByCommandLine([int] $Port, [string] $RequiredFragment) {
    foreach ($connection in Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($connection.OwningProcess)" `
            -ErrorAction SilentlyContinue
        if ($null -ne $process -and -not [string]::IsNullOrWhiteSpace($process.CommandLine) -and
            $process.CommandLine.IndexOf($RequiredFragment, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
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
$webEntry = ''
$supabaseUrl = ''
$supabasePublishableKey = ''
if ($schemaVersion -ge 2) {
    $webRoot = Resolve-ConfiguredPath ([string] (Get-RequiredSetting $settings 'webRoot')) $settingsDirectory
    $nodeExecutable = Resolve-ConfiguredPath `
        ([string] (Get-RequiredSetting $settings 'nodeExecutable')) $settingsDirectory
    $webEntry = Join-Path $webRoot 'dist\server\entry.mjs'
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
    foreach ($requiredFile in @($nodeExecutable, $webEntry)) {
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
$publicReadyUri = if ($schemaVersion -ge 2) { "https://$hostName/health/" } else { "https://$hostName/health/ready" }
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
        if (-not (Test-PortOwnedByExecutable 5088 $serviceExecutable)) {
            throw "Port 5088 is owned by a different distribution service executable."
        }
        if (-not (Test-HttpEndpoint $httpClient $localReadyUri)) {
            throw 'The configured distribution service owns port 5088 but is not ready.'
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
        if (-not (Test-PortOwnedByCommandLine 4321 $webEntry)) {
            throw "Port 4321 is owned by a web renderer from a different deployment root."
        }
        if (-not (Test-HttpEndpoint $httpClient $localWebReadyUri)) {
            throw 'The configured web renderer owns port 4321 but is not healthy.'
        }
        return
    }

    $env:NODE_ENV = 'production'
    $env:HOST = '127.0.0.1'
    $env:PORT = '4321'
    $env:KEIRE_DISTRIBUTION_HEALTH_URL = $localReadyUri
    $env:PUBLIC_SUPABASE_URL = $supabaseUrl.TrimEnd('/')
    $env:PUBLIC_SUPABASE_PUBLISHABLE_KEY = $supabasePublishableKey
    $env:PUBLIC_SITE_URL = "https://$hostName/"
    $nodeArguments = "`"$webEntry`""
    $process = Start-Process -FilePath $nodeExecutable -ArgumentList $nodeArguments `
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
            throw "Only one configured Caddy port is listening; refusing to accept a partial proxy deployment."
        }
        if (-not (Test-PortOwnedByExecutable $httpPort $caddyExecutable) -or
            -not (Test-PortOwnedByExecutable $httpsPort $caddyExecutable)) {
            throw "The configured Caddy executable does not own both public proxy ports."
        }
        if (-not (Test-HttpEndpoint $httpClient $publicReadyUri)) {
            throw 'The configured Caddy proxy owns its ports but the public platform is not ready.'
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

function Assert-ConfiguredHostReady {
    if (-not (Test-PortOwnedByExecutable 5088 $serviceExecutable) -or
        -not (Test-HttpEndpoint $httpClient $localReadyUri)) {
        throw 'The configured distribution service is not the healthy owner of port 5088.'
    }
    if ($schemaVersion -ge 2 -and
        (-not (Test-PortOwnedByCommandLine 4321 $webEntry) -or
            -not (Test-HttpEndpoint $httpClient $localWebReadyUri))) {
        throw 'The configured web deployment is not the healthy owner of port 4321.'
    }
    if (-not (Test-PortOwnedByExecutable $httpPort $caddyExecutable) -or
        -not (Test-PortOwnedByExecutable $httpsPort $caddyExecutable) -or
        -not (Test-HttpEndpoint $httpClient $publicReadyUri)) {
        throw 'The configured Caddy deployment does not exclusively own a healthy public proxy.'
    }
}

if ($ProbeOnly) {
    try {
        Assert-ConfiguredHostReady
        Write-Host "Windows distribution host ownership and readiness are valid for https://$hostName/."
        exit 0
    }
    finally {
        $httpClient.Dispose()
        $handler.Dispose()
    }
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
