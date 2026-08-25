[CmdletBinding()]
param(
    [string] $Dotnet = '',
    [string] $Npm = 'npm',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $OutputDirectory = '',
    [string[]] $RuntimeIdentifiers = @('win-x64', 'linux-x64')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serviceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $serviceRoot '..\..'))
. (Join-Path $repositoryRoot 'Scripts\Windows\common.ps1')
if ([string]::IsNullOrWhiteSpace($Dotnet)) {
    $workspaceDotnet = Join-Path $repositoryRoot 'Build\Dependencies\dotnet-sdk\dotnet.exe'
    $Dotnet = if (Test-Path -LiteralPath $workspaceDotnet -PathType Leaf) {
        $workspaceDotnet
    } else {
        (Get-Command dotnet -ErrorAction Stop).Source
    }
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $serviceRoot '..\..\Build\Distributions\KeireDistributionService'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$serviceProject = Join-Path $serviceRoot 'Source\KeireDistributionService\KeireDistributionService.csproj'
$publisherProject = Join-Path $serviceRoot 'Source\KeireDistributionPublisher\KeireDistributionPublisher.csproj'
$validatorProject = Join-Path $serviceRoot 'Source\KeireMarketplaceValidator\KeireMarketplaceValidator.csproj'
$validatorBrokerProject = Join-Path $serviceRoot 'Source\KeireMarketplaceValidatorBroker\KeireMarketplaceValidatorBroker.csproj'
$publicationSignerProject = Join-Path $serviceRoot 'Source\KeireMarketplacePublicationSigner\KeireMarketplacePublicationSigner.csproj'
$documentationSite = Join-Path $serviceRoot 'DocumentationSite'
$documentationOutput = Join-Path $documentationSite 'dist'

$env:ASTRO_TELEMETRY_DISABLED = '1'
& $Npm --prefix $documentationSite ci
if ($LASTEXITCODE -ne 0) {
    throw 'Documentation dependency restore failed.'
}
& $Npm --prefix $documentationSite test
if ($LASTEXITCODE -ne 0) {
    throw 'Documentation tests failed.'
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

    $validatorDirectory = Join-Path $packageDirectory 'tools\marketplace-validator\worker'
    & $Dotnet publish $validatorProject --configuration $Configuration --runtime $runtimeIdentifier `
        --self-contained true --output $validatorDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    if ($LASTEXITCODE -ne 0) {
        throw "Marketplace validator publish failed for '$runtimeIdentifier'."
    }

    $validatorBrokerDirectory = Join-Path $packageDirectory 'tools\marketplace-validator\broker'
    & $Dotnet publish $validatorBrokerProject --configuration $Configuration --runtime $runtimeIdentifier `
        --self-contained true --output $validatorBrokerDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    if ($LASTEXITCODE -ne 0) {
        throw "Marketplace validator broker publish failed for '$runtimeIdentifier'."
    }

    $publicationSignerDirectory = Join-Path $packageDirectory 'tools\marketplace-publication-signer'
    & $Dotnet publish $publicationSignerProject --configuration $Configuration --runtime $runtimeIdentifier `
        --self-contained true --output $publicationSignerDirectory `
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    if ($LASTEXITCODE -ne 0) {
        throw "Marketplace publication signer publish failed for '$runtimeIdentifier'."
    }

    Copy-Item -LiteralPath (Join-Path $serviceRoot 'README.md') -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'THIRD_PARTY_NOTICES.md') -Destination $packageDirectory
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Licenses') -Destination $packageDirectory -Recurse
    $packagedLicenses = Join-Path $packageDirectory 'Licenses'
    $documentationLicenses = @{
        'Astro.txt' = Join-Path $documentationSite 'node_modules\astro\LICENSE'
        'AstroNode.txt' = Join-Path $documentationSite 'node_modules\@astrojs\node\LICENSE'
        'AstroSitemap.txt' = Join-Path $documentationSite 'node_modules\@astrojs\sitemap\LICENSE'
        'Starlight.txt' = Join-Path $documentationSite 'node_modules\@astrojs\starlight\LICENSE'
        'ExpressiveCode.txt' = Join-Path $documentationSite 'node_modules\expressive-code\LICENSE'
        'BeautifulMermaid.txt' = Join-Path $documentationSite 'node_modules\beautiful-mermaid\LICENSE'
        'SupabaseSsr.txt' = Join-Path $documentationSite 'node_modules\@supabase\ssr\LICENSE'
        'SupabaseJavaScript.txt' = Join-Path $documentationSite 'node_modules\@supabase\supabase-js\LICENSE'
        'Sharp.txt' = Join-Path $documentationSite 'node_modules\sharp\LICENSE'
    }
    foreach ($license in $documentationLicenses.GetEnumerator()) {
        Copy-Item -LiteralPath $license.Value -Destination (Join-Path $packagedLicenses $license.Key)
    }
    $packagedWeb = Join-Path $packageDirectory 'Web'
    [IO.Directory]::CreateDirectory($packagedWeb) | Out-Null
    Copy-Item -LiteralPath $documentationOutput -Destination $packagedWeb -Recurse
    Copy-Item -LiteralPath (Join-Path $documentationSite 'package.json') -Destination $packagedWeb
    Copy-Item -LiteralPath (Join-Path $documentationSite 'package-lock.json') -Destination $packagedWeb
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
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Deployment\keire-web.service.example') `
        -Destination $deploymentDirectory
    foreach ($deploymentName in @('keire-marketplace-validator.service.example',
            'keire-marketplace-validator-broker.service.example',
            'keire-marketplace-publication-signer.service.example',
            'marketplace-validator.env.example', 'marketplace-validator-broker.env.example',
            'marketplace-publication-signer.env.example')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot "Deployment\$deploymentName") -Destination $deploymentDirectory
    }
    foreach ($scriptName in @('health-check.ps1', 'health-check.sh', 'monitor-distribution.ps1',
            'monitor-distribution.sh', 'backup-distribution.ps1', 'backup-distribution.sh',
            'backup-distribution-rclone.ps1', 'backup-distribution-rclone.sh', 'restore-distribution.ps1',
            'restore-distribution.sh', 'restore-distribution-rclone.ps1', 'restore-distribution-rclone.sh',
            'publish-snapshot.ps1', 'publish-snapshot.sh', 'start-wsl2-host-bridge.sh',
            'install-wsl2-host-bridge.sh', 'install-web-runtime.ps1', 'install-web-runtime.sh')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot "scripts\$scriptName") -Destination $scriptsDirectory
    }
    if ($runtimeIdentifier.StartsWith('win-', [StringComparison]::Ordinal)) {
        foreach ($deploymentName in @('configure-windows-validator-firewall.ps1',
                'install-windows-marketplace-validator-tasks.ps1',
                'install-windows-marketplace-publication-signer-task.ps1',
                'protect-windows-marketplace-secret.ps1',
                'provision-windows-marketplace-signing-keys.ps1',
                'protect-windows-validator-broker-secret.ps1',
                'start-windows-marketplace-publication-signer.ps1',
                'start-windows-marketplace-validator.ps1',
                'start-windows-marketplace-validator-broker.ps1')) {
            Copy-Item -LiteralPath (Join-Path $serviceRoot "Deployment\$deploymentName") -Destination $deploymentDirectory
        }
        foreach ($scriptName in @('start-windows-host.ps1', 'install-windows-startup-task.ps1',
                'install-windows-backup-task.ps1', 'migrate-windows-host.ps1', 'deploy-windows-web.ps1')) {
            Copy-Item -LiteralPath (Join-Path $serviceRoot "scripts\$scriptName") -Destination $scriptsDirectory
        }
    }

    if ($runtimeIdentifier.StartsWith('win-', [StringComparison]::Ordinal)) {
        $archive = "$packageDirectory.zip"
        Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archive -CompressionLevel Optimal
    } else {
        $archive = "$packageDirectory.tar.gz"
        $python = Get-PythonInvocation
        $pythonPrefix = @($python.PrefixArguments)
        $archiveWriter = Join-Path $serviceRoot '..\..\Scripts\Packaging\write-deterministic-tar.py'
        & $python.Executable @pythonPrefix $archiveWriter --source $packageDirectory --output $archive `
            --executable 'KeireDistributionService' `
            --executable 'tools/publisher/KeireDistributionPublisher' `
            --executable 'tools/marketplace-validator/worker/KeireMarketplaceValidator' `
            --executable 'tools/marketplace-validator/broker/KeireMarketplaceValidatorBroker' `
            --executable 'tools/marketplace-publication-signer/KeireMarketplacePublicationSigner' `
            --executable 'scripts/health-check.sh' `
            --executable 'scripts/monitor-distribution.sh' `
            --executable 'scripts/backup-distribution.sh' `
            --executable 'scripts/backup-distribution-rclone.sh' `
            --executable 'scripts/restore-distribution.sh' `
            --executable 'scripts/restore-distribution-rclone.sh' `
            --executable 'scripts/publish-snapshot.sh' `
            --executable 'scripts/start-wsl2-host-bridge.sh' `
            --executable 'scripts/install-wsl2-host-bridge.sh' `
            --executable 'scripts/install-web-runtime.sh'
        if ($LASTEXITCODE -ne 0) {
            throw "Archive creation failed for '$runtimeIdentifier'."
        }
    }

    Write-Host "Created $archive"
}
