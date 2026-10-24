$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$launcher = Join-Path $repository 'Services/KeireDistributionService/scripts/start-windows-host.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-host-backends-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$scripts = Join-Path $fixture 'scripts'
$server = Join-Path $fixture 'dist/server'
[IO.Directory]::CreateDirectory($scripts) | Out-Null
[IO.Directory]::CreateDirectory($server) | Out-Null
$stub = Join-Path $fixture 'placeholder.exe'
$entry = Join-Path $scripts 'platform-production-start.mjs'
$legacyEntry = Join-Path $server 'entry.mjs'
$configurationPath = Join-Path $fixture 'platform.json'
$settingsPath = Join-Path $fixture 'host.json'
$writerRetirementHelper = Join-Path $scripts 'retire-platform-writer.ps1'
foreach ($file in @($stub, $entry, $legacyEntry)) {
    [IO.File]::WriteAllText($file, 'Validation-only fixture. Never execute.')
}
$configuration = @{ schemaVersion = 1; siteOrigin = 'https://qa.invalid'; port = 4321; webRoot = $fixture }
$configuration.spoolDirectory = $fixture
$settings = @{
    schemaVersion = 3; host = 'qa.invalid'; httpPort = 18080; httpsPort = 18443
    storageRoot = $fixture; serviceExecutable = $stub; caddyExecutable = $stub
    caddyConfig = $stub; logDirectory = $fixture; webRoot = $fixture; nodeExecutable = $stub
    platformBackend = 'mysql'; platformConfigurationPath = $configurationPath
}
$powerShell = (Get-Process -Id $PID).Path
function Invoke-Validation([bool] $ExpectedSuccess) {
    $configuration | ConvertTo-Json | Set-Content -LiteralPath $configurationPath
    $settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath
    $null = & $powerShell -NoProfile -NonInteractive -File $launcher -SettingsPath $settingsPath -ValidateOnly 2>&1
    if (($LASTEXITCODE -eq 0) -ne $ExpectedSuccess) {
        throw 'Host backend validation produced an unexpected result.'
    }
}
try {
    Copy-Item -LiteralPath (Join-Path $repository 'Services/KeireDistributionService/scripts/retire-platform-writer.ps1') `
        -Destination $writerRetirementHelper
    Invoke-Validation $true
    $configuration.siteOrigin = 'https://different.invalid'
    Invoke-Validation $false
    $configuration.siteOrigin = 'https://qa.invalid'
    $configuration.port = 4322
    Invoke-Validation $false
    $configuration.port = 4321
    $settings.platformBackend = 'unsupported'
    Invoke-Validation $false
    $settings.platformBackend = 'supabase'
    $settings.supabaseUrl = 'https://supabase.invalid'
    $settings.supabasePublishableKey = 'sb_publishable_synthetic_fixture_key_1234'
    Invoke-Validation $true
    $settings.schemaVersion = 2
    $settings.Remove('platformBackend')
    Invoke-Validation $true
    Write-Output 'Six Windows host backend validation checks passed; no services launched.'
} finally {
    foreach ($file in @($stub, $entry, $legacyEntry, $configurationPath, $settingsPath, $writerRetirementHelper)) {
        Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    }
    foreach ($directory in @($scripts, $server, (Join-Path $fixture 'dist'), $fixture)) {
        [IO.Directory]::Delete($directory, $false)
    }
}
