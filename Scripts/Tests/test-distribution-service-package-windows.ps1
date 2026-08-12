$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"
. (Join-Path $Windows "common.ps1")

function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}

$packager = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\scripts\package-service.ps1") -Raw
foreach ($contract in @(
        "write-deterministic-tar.py",
        "Build\Dependencies\dotnet-sdk\dotnet.exe",
        "--executable 'KeireDistributionService'",
        "--executable 'tools/publisher/KeireDistributionPublisher'",
        "--executable 'tools/marketplace-validator/worker/KeireMarketplaceValidator'",
        "--executable 'tools/marketplace-validator/broker/KeireMarketplaceValidatorBroker'",
        "--executable 'scripts/health-check.sh'",
        "--executable 'scripts/monitor-distribution.sh'",
        "--executable 'scripts/backup-distribution.sh'",
        "--executable 'scripts/backup-distribution-rclone.sh'",
        "--executable 'scripts/restore-distribution.sh'",
        "--executable 'scripts/restore-distribution-rclone.sh'",
        "--executable 'scripts/publish-snapshot.sh'",
        "--executable 'scripts/start-wsl2-host-bridge.sh'",
        "--executable 'scripts/install-wsl2-host-bridge.sh'",
        "--executable 'scripts/install-web-runtime.sh'",
        "install-web-runtime.ps1",
        "start-windows-host.ps1",
        "install-windows-startup-task.ps1",
        "install-windows-backup-task.ps1",
        "migrate-windows-host.ps1",
        "deploy-windows-web.ps1",
        "'Web'",
        "DocumentationSite",
        "keire-web.service.example",
        "keire-marketplace-validator.service.example",
        "keire-marketplace-validator-broker.service.example",
        "marketplace-validator-broker.env.example",
        "configure-windows-validator-firewall.ps1",
        "install-windows-marketplace-validator-tasks.ps1",
        "protect-windows-validator-broker-secret.ps1",
        "start-windows-marketplace-validator.ps1",
        "start-windows-marketplace-validator-broker.ps1",
        "BeautifulMermaid.txt",
        "Documentation production build failed"
    )) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows service packager is missing '$contract'."
    }
}
$unixPackager = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\scripts\package-service.sh") -Raw
if (-not $unixPackager.Contains('install-windows-marketplace-validator-tasks.ps1')) {
    throw "The Unix service packager does not include the Windows validator task installer in Windows packages."
}
$validatorTaskInstaller = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\install-windows-marketplace-validator-tasks.ps1") -Raw
foreach ($contract in @(
        'NT AUTHORITY\LOCAL SERVICE',
        'NT AUTHORITY\NETWORK SERVICE',
        'FirewallAttestation',
        'New-ScheduledTaskTrigger -AtStartup',
        '-AllowStartIfOnBatteries',
        '-DontStopIfGoingOnBatteries',
        '-RestartCount 999',
        '-ExecutionTimeLimit ([TimeSpan]::Zero)',
        'Start-ScheduledTask -TaskName $WorkerTaskName',
        'Start-ScheduledTask -TaskName $BrokerTaskName'
    )) {
    if (-not $validatorTaskInstaller.Contains($contract)) {
        throw "The Windows validator task installer is missing '$contract'."
    }
}
$firewallConfigurator = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\configure-windows-validator-firewall.ps1") -Raw
foreach ($contract in @(
        'Attestation',
        'Get-FileHash -LiteralPath $resolved -Algorithm SHA256',
        'SetAccessRuleProtection($true, $false)',
        'WellKnownSidType]::LocalServiceSid'
    )) {
    if (-not $firewallConfigurator.Contains($contract)) {
        throw "The Windows validator firewall configurator is missing '$contract'."
    }
}
$brokerSecretProtector = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\protect-windows-validator-broker-secret.ps1") -Raw
foreach ($contract in @(
        'Read-Host "Enter VALIDATOR_BROKER_SECRET" -AsSecureString',
        'Add-Type -AssemblyName System.Security',
        '[System.Security.Cryptography.ProtectedData]::Protect',
        '[System.Security.Cryptography.DataProtectionScope]::LocalMachine',
        'SetAccessRuleProtection($true, $false)',
        'NT AUTHORITY\NETWORK SERVICE'
    )) {
    if (-not $brokerSecretProtector.Contains($contract)) {
        throw "The Windows validator secret protector is missing '$contract'."
    }
}
$brokerLauncher = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\start-windows-marketplace-validator-broker.ps1") -Raw
foreach ($contract in @(
        'Add-Type -AssemblyName System.Security',
        'ValidateOnly',
        'Get-FileHash -LiteralPath $Validator -Algorithm SHA256',
        'AreAccessRulesProtected',
        'exact least-privilege contract',
        '[System.Security.Cryptography.ProtectedData]::Unprotect',
        'KEIRE_VALIDATOR_BROKER_SECRET',
        'Remove-Item Env:KEIRE_VALIDATOR_BROKER_SECRET'
    )) {
    if (-not $brokerLauncher.Contains($contract)) {
        throw "The Windows validator broker launcher is missing '$contract'."
    }
}
if ($packager.Contains('& tar -czf')) {
    throw "The Windows service packager must not inherit unusable Linux modes from NTFS."
}
$validatorLauncher = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\start-windows-marketplace-validator.ps1") -Raw
foreach ($contract in @(
        'KEIRE_VALIDATOR_NETWORK_ISOLATED',
        'ValidateOnly',
        'FirewallAttestation',
        'Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256',
        'exact least-privilege contract',
        'Get-NetFirewallApplicationFilter',
        'Keire Marketplace Validator - Offline Worker',
        'Keire Marketplace Validator - Asset Tool',
        'Keire Marketplace Validator - Malware Scanner',
        'Keire Marketplace Validator - Managed Compiler'
    )) {
    if (-not $validatorLauncher.Contains($contract)) {
        throw "The Windows offline validator launcher is missing '$contract'."
    }
}
$caddyTemplate = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\Caddyfile.example") -Raw
foreach ($contract in @(
        '@distribution_api path /v1 /v1/* /v2 /v2/* /health/live /health/ready',
        'reverse_proxy 127.0.0.1:4321',
        'health_uri /health/',
        'Content-Security-Policy',
        '@immutable_web_assets path /_astro/* /docs/_astro/*',
        "script-src 'self' 'wasm-unsafe-eval'"
    )) {
    if (-not $caddyTemplate.Contains($contract)) {
        throw "The Caddy website boundary is missing '$contract'."
    }
}

$windowsHost = Join-Path $Root `
    "Services\KeireDistributionService\scripts\start-windows-host.ps1"
$hostFixture = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-distribution-windows-host-test-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $hostFixture "distribution\snapshots"), `
        (Join-Path $hostFixture "logs"), (Join-Path $hostFixture "Website"), `
        (Join-Path $hostFixture "scripts"), (Join-Path $hostFixture "tools") | Out-Null
    foreach ($fileName in @("KeireDistributionService.exe", "caddy.exe", "Caddyfile")) {
        [IO.File]::WriteAllText((Join-Path $hostFixture $fileName), "fixture", [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllText((Join-Path $hostFixture "distribution\current"), "fixture`n",
        [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath $windowsHost -Destination (Join-Path $hostFixture "scripts\start-windows-host.ps1")
    $settingsPath = Join-Path $hostFixture "host-settings.json"
    @{
        schemaVersion = 1
        host = "distribution.example.test"
        storageRoot = "distribution"
        httpPort = 50254
        httpsPort = 50255
        serviceExecutable = "KeireDistributionService.exe"
        caddyExecutable = "caddy.exe"
        caddyConfig = "Caddyfile"
        logDirectory = "logs"
    } | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    Invoke-CheckedWindowsCommand {
        & $windowsHost -SettingsPath $settingsPath -ValidateOnly
    } "Windows distribution host settings validation"
    New-Item -ItemType Directory -Force (Join-Path $hostFixture "Web\dist\server") | Out-Null
    [IO.File]::WriteAllText((Join-Path $hostFixture "Web\dist\server\entry.mjs"), "export {};`n",
        [Text.UTF8Encoding]::new($false))
    $webSettingsPath = Join-Path $hostFixture "host-settings-web.json"
    @{
        schemaVersion = 2
        host = "distribution.example.test"
        storageRoot = "distribution"
        httpPort = 50254
        httpsPort = 50255
        serviceExecutable = "KeireDistributionService.exe"
        caddyExecutable = "caddy.exe"
        caddyConfig = "Caddyfile"
        logDirectory = "logs"
        webRoot = "Web"
        nodeExecutable = (Get-Command node -ErrorAction Stop).Source
        supabaseUrl = "https://example.supabase.co"
        supabasePublishableKey = "sb_publishable_0123456789abcdef"
    } | ConvertTo-Json | Set-Content -LiteralPath $webSettingsPath -Encoding UTF8
    Invoke-CheckedWindowsCommand {
        & $windowsHost -SettingsPath $webSettingsPath -ValidateOnly
    } "Windows unified web host settings validation"
    $hostScript = Get-Content -LiteralPath $windowsHost -Raw
    foreach ($ownershipContract in @(
            '[switch] $ProbeOnly',
            'Test-PortOwnedByExecutable',
            'Test-PortOwnedByCommandLine',
            'Assert-ConfiguredHostReady',
            '$webEntry',
            'PUBLIC_SITE_URL'
        )) {
        if (-not $hostScript.Contains($ownershipContract)) {
            throw "The Windows host supervisor is missing ownership contract '$ownershipContract'."
        }
    }
    $startupTask = Join-Path $Root `
        "Services\KeireDistributionService\scripts\install-windows-startup-task.ps1"
    Invoke-CheckedWindowsCommand {
        & $startupTask -SettingsPath $settingsPath -TaskName "Keire Distribution Host Test" -ValidateOnly
    } "Windows pre-login startup task validation"
    Copy-Item -LiteralPath $startupTask `
        -Destination (Join-Path $hostFixture "scripts\install-windows-startup-task.ps1")
    $migration = Join-Path $Root `
        "Services\KeireDistributionService\scripts\migrate-windows-host.ps1"
    $migrationScript = Get-Content -LiteralPath $migration -Raw
    foreach ($cutoverContract in @(
            'Get-TaskSettingsPath',
            'Stop-ConfiguredHostProcesses',
            '-ProbeOnly'
        )) {
        if (-not $migrationScript.Contains($cutoverContract)) {
            throw "The Windows host migration is missing cutover contract '$cutoverContract'."
        }
    }
    Invoke-CheckedWindowsCommand {
        & $migration -SourceHostRoot $hostFixture -SourceDistributionRoot (Join-Path $hostFixture "distribution") `
            -DestinationRoot (Join-Path $env:ProgramData "Keire Distribution Host Test") -ValidateOnly
    } "Windows protected host migration validation"
    $webDeployment = Join-Path $Root `
        "Services\KeireDistributionService\scripts\deploy-windows-web.ps1"
    $webDeploymentScript = Get-Content -LiteralPath $webDeployment -Raw
    foreach ($deploymentContract in @(
            'dist.rollback-',
            'Stop-ExpectedWebProcess',
            'Stop-ConfiguredHost',
            'Start-ConfiguredHost',
            'Get-ScheduledTask',
            'The dependency lock changed',
            'AllowRuntimeUpdate',
            'npm.cmd',
            'locked web runtime deployment completed',
            '-ProbeOnly'
        )) {
        if (-not $webDeploymentScript.Contains($deploymentContract)) {
            throw "The Windows web deployment is missing '$deploymentContract'."
        }
    }

    $invalidSettings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    $invalidSettings.host = "https://not-a-host.example"
    $invalidSettings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    Assert-Throws {
        & $windowsHost -SettingsPath $settingsPath -ValidateOnly 2>$null
    } "Invalid Windows distribution host name rejection"
}
finally {
    Remove-Item -LiteralPath $hostFixture -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($scriptName in @('monitor-distribution.ps1', 'monitor-distribution.sh', 'backup-distribution.ps1',
        'backup-distribution.sh', 'backup-distribution-rclone.ps1', 'backup-distribution-rclone.sh',
        'restore-distribution.ps1', 'restore-distribution.sh', 'restore-distribution-rclone.ps1',
        'restore-distribution-rclone.sh', 'install-windows-backup-task.ps1')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root "Services\KeireDistributionService\scripts\$scriptName") `
            -PathType Leaf)) {
        throw "The distribution operations script is missing: '$scriptName'."
    }
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-distribution-service-package-test-" + [guid]::NewGuid().ToString("N"))
$source = Join-Path $fixture "keire-distribution-linux-x64"
$archiveA = Join-Path $fixture "package-a.tar.gz"
$archiveB = Join-Path $fixture "package-b.tar.gz"
try {
    New-Item -ItemType Directory -Force (Join-Path $source "Deployment"), `
        (Join-Path $source "scripts"), (Join-Path $source "tools\publisher"), `
        (Join-Path $source "Web\dist\client\_astro"), `
        (Join-Path $source "Web\dist\client\pagefind"), (Join-Path $source "Web\dist\server") | Out-Null
    foreach ($relative in @(
            "Deployment\Caddyfile.example",
            "KeireDistributionService",
            "README.md",
            "Web\dist\client\_astro\site.css",
            "Web\dist\client\pagefind\pagefind.js",
            "Web\dist\server\entry.mjs",
            "Web\package-lock.json",
            "Web\package.json",
            "scripts\backup-distribution.sh",
            "scripts\backup-distribution-rclone.sh",
            "scripts\health-check.sh",
            "scripts\monitor-distribution.sh",
            "scripts\publish-snapshot.sh",
            "scripts\restore-distribution.sh",
            "scripts\restore-distribution-rclone.sh",
            "tools\publisher\KeireDistributionPublisher",
            "tools\publisher\regular.dat"
        )) {
        [IO.File]::WriteAllText((Join-Path $source $relative), $relative, [Text.UTF8Encoding]::new($false))
    }

    $python = (Get-Command python -ErrorAction Stop).Source
    $writer = Join-Path $Root "Scripts\Packaging\write-deterministic-tar.py"
    $arguments = @(
        $writer,
        "--source", $source,
        "--executable", "KeireDistributionService",
        "--executable", "tools/publisher/KeireDistributionPublisher",
        "--executable", "scripts/backup-distribution.sh",
        "--executable", "scripts/backup-distribution-rclone.sh",
        "--executable", "scripts/health-check.sh",
        "--executable", "scripts/monitor-distribution.sh",
        "--executable", "scripts/publish-snapshot.sh",
        "--executable", "scripts/restore-distribution.sh"
        "--executable", "scripts/restore-distribution-rclone.sh"
    )
    Invoke-CheckedWindowsCommand { & $python @arguments --output $archiveA } `
        "First deterministic service archive"
    [IO.File]::SetLastWriteTimeUtc((Join-Path $source "README.md"), [DateTime]::UtcNow.AddYears(-10))
    Invoke-CheckedWindowsCommand { & $python @arguments --output $archiveB } `
        "Second deterministic service archive"

    $firstDigest = (Get-FileHash -LiteralPath $archiveA -Algorithm SHA256).Hash
    $secondDigest = (Get-FileHash -LiteralPath $archiveB -Algorithm SHA256).Hash
    if ($firstDigest -ne $secondDigest) {
        throw "Service archives built from identical bytes are not deterministic."
    }

    $names = @(& tar -tzf $archiveA)
    if ($LASTEXITCODE -ne 0) { throw "The deterministic service archive could not be listed." }
    $expectedNames = @(
        "keire-distribution-linux-x64/",
        "keire-distribution-linux-x64/Deployment/",
        "keire-distribution-linux-x64/Deployment/Caddyfile.example",
        "keire-distribution-linux-x64/KeireDistributionService",
        "keire-distribution-linux-x64/README.md",
        "keire-distribution-linux-x64/Web/",
        "keire-distribution-linux-x64/Web/dist/",
        "keire-distribution-linux-x64/Web/dist/client/",
        "keire-distribution-linux-x64/Web/dist/client/_astro/",
        "keire-distribution-linux-x64/Web/dist/client/_astro/site.css",
        "keire-distribution-linux-x64/Web/dist/client/pagefind/",
        "keire-distribution-linux-x64/Web/dist/client/pagefind/pagefind.js",
        "keire-distribution-linux-x64/Web/dist/server/",
        "keire-distribution-linux-x64/Web/dist/server/entry.mjs",
        "keire-distribution-linux-x64/Web/package-lock.json",
        "keire-distribution-linux-x64/Web/package.json",
        "keire-distribution-linux-x64/scripts/",
        "keire-distribution-linux-x64/scripts/backup-distribution-rclone.sh",
        "keire-distribution-linux-x64/scripts/backup-distribution.sh",
        "keire-distribution-linux-x64/scripts/health-check.sh",
        "keire-distribution-linux-x64/scripts/monitor-distribution.sh",
        "keire-distribution-linux-x64/scripts/publish-snapshot.sh",
        "keire-distribution-linux-x64/scripts/restore-distribution-rclone.sh",
        "keire-distribution-linux-x64/scripts/restore-distribution.sh",
        "keire-distribution-linux-x64/tools/",
        "keire-distribution-linux-x64/tools/publisher/",
        "keire-distribution-linux-x64/tools/publisher/KeireDistributionPublisher",
        "keire-distribution-linux-x64/tools/publisher/regular.dat"
    )
    if ($names.Count -ne $expectedNames.Count) {
        throw "The deterministic service archive inventory count is incorrect."
    }
    for ($index = 0; $index -lt $expectedNames.Count; ++$index) {
        if ($names[$index] -ne $expectedNames[$index]) {
            throw "The deterministic service archive inventory is not sorted at index $index."
        }
    }

    $listing = @(& tar -tvzf $archiveA)
    if ($LASTEXITCODE -ne 0) { throw "The deterministic service archive modes could not be listed." }
    $expectedModes = @{
        "keire-distribution-linux-x64/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Deployment/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Deployment/Caddyfile.example" = "-rw-r--r--"
        "keire-distribution-linux-x64/KeireDistributionService" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/README.md" = "-rw-r--r--"
        "keire-distribution-linux-x64/Web/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/client/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/client/_astro/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/client/_astro/site.css" = "-rw-r--r--"
        "keire-distribution-linux-x64/Web/dist/client/pagefind/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/client/pagefind/pagefind.js" = "-rw-r--r--"
        "keire-distribution-linux-x64/Web/dist/server/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Web/dist/server/entry.mjs" = "-rw-r--r--"
        "keire-distribution-linux-x64/Web/package-lock.json" = "-rw-r--r--"
        "keire-distribution-linux-x64/Web/package.json" = "-rw-r--r--"
        "keire-distribution-linux-x64/scripts/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/scripts/backup-distribution-rclone.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/backup-distribution.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/health-check.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/monitor-distribution.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/publish-snapshot.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/restore-distribution-rclone.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/restore-distribution.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/tools/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/tools/publisher/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/tools/publisher/KeireDistributionPublisher" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/tools/publisher/regular.dat" = "-rw-r--r--"
    }
    foreach ($entry in $expectedModes.GetEnumerator()) {
        $matching = @($listing | Where-Object {
                $_.EndsWith(" $($entry.Key)", [StringComparison]::Ordinal)
            })
        if ($matching.Count -ne 1 -or $matching[0].Substring(0, 10) -ne $entry.Value) {
            throw "The deterministic service archive mode is incorrect for '$($entry.Key)'."
        }
    }

    $missingArchive = Join-Path $fixture "missing-executable.tar.gz"
    Assert-Throws {
        Invoke-CheckedWindowsCommand {
            & $python @arguments --output $missingArchive --executable "missing-tool" 2>$null
        } "Missing service executable rejection"
    } "Missing service executable rejection"
    if (Test-Path -LiteralPath $missingArchive) {
        throw "Missing executable validation left a partial service archive."
    }

    $nestedArchive = Join-Path $source "nested.tar.gz"
    Assert-Throws {
        Invoke-CheckedWindowsCommand { & $python @arguments --output $nestedArchive 2>$null } `
            "Nested service archive rejection"
    } "Nested service archive rejection"
    if (Test-Path -LiteralPath $nestedArchive) {
        throw "Nested output validation contaminated the service package source."
    }

    Assert-Throws {
        Invoke-CheckedWindowsCommand { & $python @arguments --output $archiveA 2>$null } `
            "Existing service archive rejection"
    } "Existing service archive rejection"
}
finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows distribution service package checks passed."
