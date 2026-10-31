[CmdletBinding()]
param(
    [string]$AssetTool = (Join-Path $PSScriptRoot '../../Build/Distributions/keire-editor-windows-x86_64-DevDist/bin/KeireAssetTool.exe'),
    [string]$ModelSource = 'D:/Projects/KeireValidation/PlanetaryShowcaseSources/NASA/SpaceShuttleB-Uncompressed.glb'
)

$ErrorActionPreference = 'Stop'
$toolPath = (Resolve-Path -LiteralPath $AssetTool).Path
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireOrbiterImport-' + [Guid]::NewGuid().ToString('N'))
try
{
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'Assets'),
        (Join-Path $fixtureRoot 'ProjectSettings') -Force | Out-Null
    @{
        schemaVersion = 4; id = [Guid]::NewGuid().ToString('D'); name = 'Orbiter import fixture'
        createdWithEngineVersion = '0.3.0'; minimumEngineVersion = '0.3.0'
        createdAt = '2026-01-01T00:00:00Z'; lastSavedWithEngineVersion = '0.3.0'; requiredModules = @()
    } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $fixtureRoot 'ProjectSettings/Project.keireproject') -Encoding UTF8
    $shipId = 'bd800ac4-c0fa-4d19-b7a5-6d2a599ba410'
    $scenePath = Join-Path $fixtureRoot 'Assets/PlanetaryExplorer.keirescene'
    @{
        entities = @(
            @{ id = $shipId; name = 'Spacecraft'; active = $true; parent = $null },
            @{ id = '7d9817f4-993b-463e-8ee6-c6d16cb568e7'; name = 'Hull'; active = $true; parent = $shipId },
            @{ id = 'dddc9f09-bc98-4b77-bc56-be1d14d107de'; name = 'Flight Camera'; active = $true; parent = $shipId }
        )
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $scenePath -Encoding UTF8
    @{ dependencies = @() } | ConvertTo-Json | Set-Content -LiteralPath "$scenePath.keiremeta" -Encoding UTF8
    $originalSceneHash = (Get-FileHash -LiteralPath $scenePath -Algorithm SHA256).Hash
    & (Join-Path $PSScriptRoot 'Author-NasaOrbiter.ps1') -Project $fixtureRoot -ModelSource $ModelSource -WireScene
    $wiredScene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
    $orbiter = @($wiredScene.entities | Where-Object name -eq 'NASA Orbiter')
    if ($orbiter.Count -ne 1 -or $orbiter[0].parent -ne $shipId -or
        ($wiredScene.entities | Where-Object name -eq 'Hull').active -ne $false -or
        ($wiredScene.entities | Where-Object name -eq 'Flight Camera').active -ne $true)
    {
        throw 'Orbiter wiring failed to preserve the camera or replace the placeholder visual.'
    }
    $backupPath = Join-Path $fixtureRoot 'AuthoringBackups/BeforeNasaOrbiter/PlanetaryExplorer.keirescene'
    if ((Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash -ne $originalSceneHash)
    {
        throw 'Orbiter authoring backup did not preserve the previous scene.'
    }
    # This deliberately minimal wiring fixture is not an engine scene; import only the authored model/prefab.
    Move-Item -LiteralPath $scenePath, "$scenePath.keiremeta" -Destination $fixtureRoot
    & $toolPath import --project $fixtureRoot --worker-timeout-seconds 30
    if ($LASTEXITCODE -ne 0)
    {
        throw 'NASA orbiter production import failed.'
    }
    $catalog = Get-Content -LiteralPath (Join-Path $fixtureRoot 'Library/AssetCache/Runtime/catalog.json') -Raw |
        ConvertFrom-Json
    $mesh = @($catalog.assets | Where-Object id -eq 'fa2d173e-325d-4d69-8ced-6602b3a56414')
    $prefab = @($catalog.assets | Where-Object id -eq '9ead9170-ebd4-4e9a-b184-bc87dd142ac6')
    if ($mesh.Count -ne 1 -or $mesh[0].type -ne '4b454952-454d-4553-4841-535345540001' -or
        $prefab.Count -ne 1 -or $prefab[0].type -ne '4b454952-4550-5245-4641-424153535401' -or
        $prefab[0].dependencies -notcontains $mesh[0].id)
    {
        throw 'Imported prefab/mesh identities or dependencies are incomplete.'
    }
    & $toolPath validate --catalog (Join-Path $fixtureRoot 'Library/AssetCache/Runtime/catalog.json')
    if ($LASTEXITCODE -ne 0)
    {
        throw 'NASA orbiter catalog validation failed.'
    }
    Write-Output 'PASS NASA orbiter wiring/backup, actual mesh/prefab import, and catalog dependency validation.'
}
finally
{
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'KeireOrbiterImport-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot)
    {
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
