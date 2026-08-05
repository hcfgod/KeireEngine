[CmdletBinding()]
param(
    [ValidateSet('x86_64', 'arm64')]
    [string]$Architecture = 'x86_64',
    [string]$OutputDirectory,
    [switch]$KeepStaging
)

$ErrorActionPreference = 'Stop'
$BuildArchitecture = if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x86_64' }
$ManifestArchitecture = if ($Architecture -eq 'arm64') { 'arm64' } else { 'x86_64' }
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'Build\PlayerSupport'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

& (Join-Path $repositoryRoot 'Scripts\project.ps1') build -Generator ninja -Configuration Debug -Architecture x86_64 -Toolset msc -Target KeireAssetTool
if ($LASTEXITCODE -ne 0) { throw 'Could not build the host KeireAssetTool.' }
$assetTool = Join-Path $repositoryRoot 'Build\Bin\Debug-windows-x86_64\KeireAssetTool\KeireAssetTool.exe'
$metadata = (& $assetTool describe-player-support-host | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0) { throw 'Could not query player support metadata.' }

foreach ($configuration in @('Debug', 'Release', 'Dist')) {
    & (Join-Path $repositoryRoot 'Scripts\project.ps1') build -Generator ninja -Configuration $configuration -Architecture $BuildArchitecture -Toolset msc -Target KeireRuntime
    if ($LASTEXITCODE -ne 0) { throw "Could not build the $configuration $Architecture player template." }
}

$packId = "windows-$ManifestArchitecture-$($metadata.engineVersion)"
$staging = Join-Path $OutputDirectory ".staging-$([guid]::NewGuid().ToString('N'))"
$payload = Join-Path $staging 'payload'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
try {
    $variants = @()
    foreach ($variant in @(@{ Build = 'Debug'; Name = 'Development' },
                            @{ Build = 'Release'; Name = 'Release' },
                            @{ Build = 'Dist'; Name = 'Dist' })) {
        $source = Join-Path $repositoryRoot "Build\Bin\$($variant.Build)-windows-$BuildArchitecture\KeireRuntime"
        if (-not (Test-Path -LiteralPath (Join-Path $source 'KeireRuntime.exe') -PathType Leaf)) {
            throw "The $($variant.Build) player template is incomplete: $source"
        }
        $destination = Join-Path $payload $variant.Name
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
        $symbols = @()
        foreach ($symbol in @('KeireRuntime.pdb', 'KeireRuntime.ilk')) {
            if (Test-Path -LiteralPath (Join-Path $destination $symbol) -PathType Leaf) { $symbols += $symbol }
        }
        $configurationName = if ($variant.Name -eq 'Development') { 'development' } else { $variant.Name.ToLowerInvariant() }
        $variants += [ordered]@{
            configuration = $configurationName
            root = $variant.Name
            executable = 'KeireRuntime.exe'
            bundle = ''
            symbols = $symbols
        }
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        playerAbi = [int]$metadata.playerAbi
        id = $packId
        engineVersion = [string]$metadata.engineVersion
        platform = 'windows'
        architecture = $ManifestArchitecture
        moduleFingerprint = [string]$metadata.moduleFingerprint
        sourceModules = @($metadata.sourceModules)
        variants = $variants
        files = @()
        brandingSlots = @($variants | ForEach-Object {
            [ordered]@{
                path = "$($_.root)/$($_.executable)"
                kind = 'windows-resource-update'
                offset = 0
                size = 1
            }
        })
    }
    $manifestPath = Join-Path $staging 'manifest.json'
    [IO.File]::WriteAllText($manifestPath, (($manifest | ConvertTo-Json -Depth 8) + "`n"), [Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $archive = Join-Path $OutputDirectory "$packId.keireplayersupport"
    & $assetTool pack-player-support --catalog $manifestPath --input $payload --output $archive --compression-level 9
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the Build Support package.' }
    & $assetTool verify-player-support --input $archive
    if ($LASTEXITCODE -ne 0) { throw 'Build Support package verification failed.' }

    $archiveFile = Get-Item -LiteralPath $archive
    $entry = [ordered]@{
        id = $packId
        platform = 'windows'
        architecture = $ManifestArchitecture
        file = $archiveFile.Name
        size = [uint64]$archiveFile.Length
        sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $catalogPath = Join-Path $OutputDirectory 'player-support-catalog.json'
    $packages = @()
    if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
        $existingCatalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
        if ([string]$existingCatalog.engineVersion -ne [string]$metadata.engineVersion) {
            throw 'Existing player-support-catalog.json targets a different engine version.'
        }
        $packages = @($existingCatalog.packages | Where-Object { $_.id -ne $packId })
    }
    $packages += $entry
    $catalog = [ordered]@{ schemaVersion = 1; engineVersion = [string]$metadata.engineVersion; packages = @($packages | Sort-Object id) }
    $catalogTemporary = "$catalogPath.$([guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($catalogTemporary, (($catalog | ConvertTo-Json -Depth 6) + "`n"), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $catalogTemporary -Destination $catalogPath -Force
    Write-Host "Created $archive"
}
finally {
    if (-not $KeepStaging -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}
