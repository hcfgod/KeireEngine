[CmdletBinding()]
param(
    [string] $Dotnet = 'dotnet',
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

$serviceProject = Join-Path $serviceRoot 'src\KeireDistributionService\KeireDistributionService.csproj'
$publisherProject = Join-Path $serviceRoot 'src\KeireDistributionPublisher\KeireDistributionPublisher.csproj'

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
    Copy-Item -LiteralPath (Join-Path $serviceRoot 'Website') -Destination $packageDirectory -Recurse
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
    foreach ($scriptName in @('health-check.ps1', 'health-check.sh', 'publish-snapshot.ps1',
            'publish-snapshot.sh')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot "scripts\$scriptName") -Destination $scriptsDirectory
    }
    if ($runtimeIdentifier.StartsWith('win-', [StringComparison]::Ordinal)) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot 'scripts\start-windows-host.ps1') `
            -Destination $scriptsDirectory
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
            --executable 'scripts/publish-snapshot.sh'
        if ($LASTEXITCODE -ne 0) {
            throw "Archive creation failed for '$runtimeIdentifier'."
        }
    }

    Write-Host "Created $archive"
}
