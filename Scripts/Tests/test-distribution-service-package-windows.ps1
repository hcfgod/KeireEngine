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
        "--executable 'KeireDistributionService'",
        "--executable 'tools/publisher/KeireDistributionPublisher'",
        "--executable 'scripts/health-check.sh'",
        "--executable 'scripts/monitor-distribution.sh'",
        "--executable 'scripts/backup-distribution.sh'",
        "--executable 'scripts/backup-distribution-rclone.sh'",
        "--executable 'scripts/restore-distribution.sh'",
        "--executable 'scripts/restore-distribution-rclone.sh'",
        "--executable 'scripts/publish-snapshot.sh'",
        "--executable 'scripts/start-wsl2-host-bridge.sh'",
        "--executable 'scripts/install-wsl2-host-bridge.sh'",
        "start-windows-host.ps1",
        "install-windows-startup-task.ps1",
        "install-windows-backup-task.ps1",
        "migrate-windows-host.ps1",
        "Website",
        "DocumentationSite",
        "BeautifulMermaid.txt",
        "Documentation production build failed"
    )) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows service packager is missing '$contract'."
    }
}
if ($packager.Contains('& tar -czf')) {
    throw "The Windows service packager must not inherit unusable Linux modes from NTFS."
}
$caddyTemplate = Get-Content -LiteralPath `
    (Join-Path $Root "Services\KeireDistributionService\Deployment\Caddyfile.example") -Raw
foreach ($contract in @(
        '@distribution_api path /v1 /v1/* /v2 /v2/* /health /health/*',
        'root * "{$KEIRE_WEBSITE_ROOT:Website}"',
        'try_files {path} {path}/index.html',
        'Content-Security-Policy',
        '@docs_immutable path /docs/_astro/*',
        "script-src 'self' 'wasm-unsafe-eval'",
        'handle_errors'
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
    $startupTask = Join-Path $Root `
        "Services\KeireDistributionService\scripts\install-windows-startup-task.ps1"
    Invoke-CheckedWindowsCommand {
        & $startupTask -SettingsPath $settingsPath -TaskName "Keire Distribution Host Test" -ValidateOnly
    } "Windows pre-login startup task validation"
    Copy-Item -LiteralPath $startupTask `
        -Destination (Join-Path $hostFixture "scripts\install-windows-startup-task.ps1")
    $migration = Join-Path $Root `
        "Services\KeireDistributionService\scripts\migrate-windows-host.ps1"
    Invoke-CheckedWindowsCommand {
        & $migration -SourceHostRoot $hostFixture -SourceDistributionRoot (Join-Path $hostFixture "distribution") `
            -DestinationRoot (Join-Path $env:ProgramData "Keire Distribution Host Test") -ValidateOnly
    } "Windows protected host migration validation"

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
        (Join-Path $source "Website\assets"), (Join-Path $source "Website\docs\_astro"), `
        (Join-Path $source "Website\docs\assets"), (Join-Path $source "Website\docs\pagefind") | Out-Null
    foreach ($relative in @(
            "Deployment\Caddyfile.example",
            "KeireDistributionService",
            "README.md",
            "Website\assets\site.css",
            "Website\docs\_astro\docs.css",
            "Website\docs\assets\inter-variable.ttf",
            "Website\docs\index.html",
            "Website\docs\pagefind\pagefind.js",
            "Website\index.html",
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
        "keire-distribution-linux-x64/Website/",
        "keire-distribution-linux-x64/Website/assets/",
        "keire-distribution-linux-x64/Website/assets/site.css",
        "keire-distribution-linux-x64/Website/docs/",
        "keire-distribution-linux-x64/Website/docs/_astro/",
        "keire-distribution-linux-x64/Website/docs/_astro/docs.css",
        "keire-distribution-linux-x64/Website/docs/assets/",
        "keire-distribution-linux-x64/Website/docs/assets/inter-variable.ttf",
        "keire-distribution-linux-x64/Website/docs/index.html",
        "keire-distribution-linux-x64/Website/docs/pagefind/",
        "keire-distribution-linux-x64/Website/docs/pagefind/pagefind.js",
        "keire-distribution-linux-x64/Website/index.html",
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
        "keire-distribution-linux-x64/Website/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/assets/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/assets/site.css" = "-rw-r--r--"
        "keire-distribution-linux-x64/Website/docs/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/docs/_astro/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/docs/_astro/docs.css" = "-rw-r--r--"
        "keire-distribution-linux-x64/Website/docs/assets/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/docs/assets/inter-variable.ttf" = "-rw-r--r--"
        "keire-distribution-linux-x64/Website/docs/index.html" = "-rw-r--r--"
        "keire-distribution-linux-x64/Website/docs/pagefind/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/Website/docs/pagefind/pagefind.js" = "-rw-r--r--"
        "keire-distribution-linux-x64/Website/index.html" = "-rw-r--r--"
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
