[CmdletBinding()]
param(
    [string] $Dotnet = 'dotnet',
    [string] $Npm = 'npm',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $OutputDirectory = '',
    [string[]] $RuntimeIdentifiers = @('win-x64', 'linux-x64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serviceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $serviceRoot '..\..\Build\Distributions\KeireDistributionService'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$serviceProject = Join-Path $serviceRoot 'Source\KeireDistributionService\KeireDistributionService.csproj'
$publisherProject = Join-Path $serviceRoot 'Source\KeireDistributionPublisher\KeireDistributionPublisher.csproj'
$documentationSite = Join-Path $serviceRoot 'DocumentationSite'
$documentationOutput = Join-Path $documentationSite 'dist'

$env:ASTRO_TELEMETRY_DISABLED = '1'
& $Npm --prefix $documentationSite ci
if ($LASTEXITCODE -ne 0) {
    throw 'Documentation dependency restore failed.'
}
& $Npm --prefix $documentationSite run build
if ($LASTEXITCODE -ne 0) {
    throw 'Documentation production build failed.'
}

foreach ($runtimeIdentifier in $RuntimeIdentifiers) {
    if ($runtimeIdentifier -notmatch '^(win|linux)-(x64|arm64)$') {
        throw "Unsupported runtime identifier '$runtimeIdentifier'."
    }

    $packageDirectory = Join-Path $outputRoot "keire-distribution-$runtimeIdentifier"
    if (Test-Path -LiteralPath $packageDirectory) {
        throw "Package destination already exists: '$packageDirectory'. Choose a clean output directory."
    }

    [IO.Directory]::CreateDirectory($packageDirectory) | Out-Null
    & $Dotnet publish $serviceProject --configuration $Configuration --runtime $runtimeIdentifier `
        --self-contained true --output $packageDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    if ($LASTEXITCODE -ne 0) {
        throw "Service publish failed for '$runtimeIdentifier'."
    }

    $toolsDirectory = Join-Path $packageDirectory 'tools\publisher'
    & $Dotnet publish $publisherProject --configuration $Configuration --runtime $runtimeIdentifier `
        --self-contained true --output $toolsDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    if ($LASTEXITCODE -ne 0) {
        throw "Publisher publish failed for '$runtimeIdentifier'."
    }

    Copy-Item -LiteralPath (Join-Path $serviceRoot 'README.md') -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'THIRD_PARTY_NOTICES.md') -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Licenses') -Destination $packageDirectory -Recurse
    $packagedLicenses = Join-Path $packageDirectory 'Licenses'
    $documentationLicenses = @{
        'Astro.txt' = Join-Path $documentationSite 'node_modules\astro\LICENSE'
        'Starlight.txt' = Join-Path $documentationSite 'node_modules\@astrojs\starlight\LICENSE'
        'ExpressiveCode.txt' = Join-Path $documentationSite 'node_modules\expressive-code\LICENSE'
        'BeautifulMermaid.txt' = Join-Path $documentationSite 'node_modules\beautiful-mermaid\LICENSE'
    }
    foreach ($license in $documentationLicenses.GetEnumerator()) {
        Copy-Item -LiteralPath $license.Value -Destination (Join-Path $packagedLicenses $license.Key)
    }
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Website') -Destination $packageDirectory -Recurse
    $packagedDocumentation = Join-Path $packageDirectory 'Website\docs'
    [IO.Directory]::CreateDirectory($packagedDocumentation) | Out-Null
    Get-ChildItem -LiteralPath $documentationOutput | Copy-Item -Destination $packagedDocumentation -Recurse -Force
    $deploymentDirectory = Join-Path $packageDirectory 'Deployment'
    $scriptsDirectory = Join-Path $packageDirectory 'scripts'
    [IO.Directory]::CreateDirectory($deploymentDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($scriptsDirectory) | Out-Null
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Deployment\Caddyfile.example') `
        -Destination $deploymentDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Deployment\appsettings.Production.example.json') `
        -Destination $deploymentDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Deployment\keire-distribution.service.example') `
        -Destination $deploymentDirectory
    foreach ($scriptName in @('health-check.ps1', 'health-check.sh', 'monitor-distribution.ps1',
            'monitor-distribution.sh', 'backup-distribution.ps1', 'backup-distribution.sh',
            'backup-distribution-rclone.ps1', 'backup-distribution-rclone.sh', 'restore-distribution.ps1',
            'restore-distribution.sh', 'restore-distribution-rclone.ps1', 'restore-distribution-rclone.sh',
            'publish-snapshot.ps1', 'publish-snapshot.sh', 'start-wsl2-host-bridge.sh',
            'install-wsl2-host-bridge.sh')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot "scripts\$scriptName") -Destination $scriptsDirectory
    }
    if ($runtimeIdentifier.StartsWith('win-', [StringComparison]::Ordinal)) {
        foreach ($scriptName in @('start-windows-host.ps1', 'install-windows-startup-task.ps1',
                'install-windows-backup-task.ps1', 'migrate-windows-host.ps1')) {
            Copy-Item -LiteralPath (Join-Path $serviceRoot "scripts\$scriptName") -Destination $scriptsDirectory
        }
    }

    if ($runtimeIdentifier.StartsWith('win-', [StringComparison]::Ordinal)) {
        $archive = "$packageDirectory.zip"
        Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archive -CompressionLevel Optimal
    } else {
        $archive = "$packageDirectory.tar.gz"
        $python = (Get-Command python -ErrorAction Stop).Source
        $archiveWriter = Join-Path $serviceRoot '..\..\Scripts\Packaging\write-deterministic-tar.py'
        & $python $archiveWriter --source $packageDirectory --output $archive `
            --executable 'KeireDistributionService' `
            --executable 'tools/publisher/KeireDistributionPublisher' `
            --executable 'scripts/health-check.sh' `
            --executable 'scripts/monitor-distribution.sh' `
            --executable 'scripts/backup-distribution.sh' `
            --executable 'scripts/backup-distribution-rclone.sh' `
            --executable 'scripts/restore-distribution.sh' `
            --executable 'scripts/restore-distribution-rclone.sh' `
            --executable 'scripts/publish-snapshot.sh' `
            --executable 'scripts/start-wsl2-host-bridge.sh' `
            --executable 'scripts/install-wsl2-host-bridge.sh'
        if ($LASTEXITCODE -ne 0) {
            throw "Archive creation failed for '$runtimeIdentifier'."
        }
    }

    Write-Host "Created $archive"
}
