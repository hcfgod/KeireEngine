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
        "--executable 'scripts/publish-snapshot.sh'",
        "scripts\start-windows-host.ps1"
    )) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows service packager is missing '$contract'."
    }
}
if ($packager.Contains('& tar -czf')) {
    throw "The Windows service packager must not inherit unusable Linux modes from NTFS."
}

$windowsHost = Join-Path $Root `
    "Services\KeireDistributionService\scripts\start-windows-host.ps1"
$hostFixture = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-distribution-windows-host-test-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $hostFixture "distribution"), `
        (Join-Path $hostFixture "logs") | Out-Null
    foreach ($fileName in @("KeireDistributionService.exe", "caddy.exe", "Caddyfile")) {
        [IO.File]::WriteAllText((Join-Path $hostFixture $fileName), "fixture", [Text.UTF8Encoding]::new($false))
    }
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

$fixture = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-distribution-service-package-test-" + [guid]::NewGuid().ToString("N"))
$source = Join-Path $fixture "keire-distribution-linux-x64"
$archiveA = Join-Path $fixture "package-a.tar.gz"
$archiveB = Join-Path $fixture "package-b.tar.gz"
try {
    New-Item -ItemType Directory -Force (Join-Path $source "Deployment"), `
        (Join-Path $source "scripts"), (Join-Path $source "tools\publisher") | Out-Null
    foreach ($relative in @(
            "Deployment\Caddyfile.example",
            "KeireDistributionService",
            "README.md",
            "scripts\health-check.sh",
            "scripts\publish-snapshot.sh",
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
        "--executable", "scripts/health-check.sh",
        "--executable", "scripts/publish-snapshot.sh"
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
        "keire-distribution-linux-x64/scripts/",
        "keire-distribution-linux-x64/scripts/health-check.sh",
        "keire-distribution-linux-x64/scripts/publish-snapshot.sh",
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
        "keire-distribution-linux-x64/scripts/" = "drwxr-xr-x"
        "keire-distribution-linux-x64/scripts/health-check.sh" = "-rwxr-xr-x"
        "keire-distribution-linux-x64/scripts/publish-snapshot.sh" = "-rwxr-xr-x"
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
