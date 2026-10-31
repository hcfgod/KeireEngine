[CmdletBinding()]
param(
    [string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer',
    [string]$ModelSource = 'D:/Projects/KeireValidation/PlanetaryShowcaseSources/NASA/SpaceShuttleB-Uncompressed.glb',
    [switch]$WireScene
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Project).Path
$target = Join-Path $root 'Assets/PlanetaryExplorer/NasaOrbiter'
if (Test-Path -LiteralPath $target)
{
    throw 'Orbiter assets already exist; refusing to overwrite authored work.'
}
if ((Get-FileHash -LiteralPath $ModelSource -Algorithm SHA256).Hash -ne
    'F983739A0185DA1756C9A28F55A2CDA2EA869EABE939953EFF88B4D61B4389B2')
{
    throw 'Run Fetch-NasaOrbiter.ps1 to prepare the validated uncompressed source.'
}
$meshId = 'fa2d173e-325d-4d69-8ced-6602b3a56414'
$prefabId = '9ead9170-ebd4-4e9a-b184-bc87dd142ac6'
$entityId = '196a238b-8971-4493-a814-e215d6ef6d73'
$scenePath = Join-Path $root 'Assets/PlanetaryExplorer.keirescene'
if ($WireScene)
{
    $scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
    $sceneMetadataPath = $scenePath + '.keiremeta'
    $metadata = Get-Content -LiteralPath $sceneMetadataPath -Raw | ConvertFrom-Json
    $ship = @($scene.entities | Where-Object name -eq 'Spacecraft')
    if ($ship.Count -ne 1 -or @($scene.entities | Where-Object id -eq $entityId).Count -ne 0)
    {
        throw 'Expected one spacecraft and no existing NASA orbiter instance.'
    }
    $backup = Join-Path $root 'AuthoringBackups/BeforeNasaOrbiter'
    if (Test-Path -LiteralPath $backup)
    {
        throw 'An orbiter authoring backup already exists; inspect the previous attempt before retrying.'
    }
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath $scenePath, $sceneMetadataPath -Destination $backup
}

$scale = 12.0 / 8.923474311828614
$entity = @{
    id = $entityId; name = 'NASA Orbiter'; active = $true; layer = 0; parent = $null
    components = @(
        @{
            type = '4b454952-4554-5241-4e53-464f524d0001'; version = 2; enabled = $true
            data = @{ position = @(0.0, 0.0, 0.0); rotation = @(0.0, 0.7071067811865476, -0.7071067811865476, 0.0)
                scale = @($scale, $scale, $scale) }
        },
        @{
            type = '4b454952-454d-4553-4852-454e44455201'; version = 4; enabled = $true
            data = @{ mesh = $meshId; material = $null; tint = @(1.0, 1.0, 1.0, 1.0)
                visible = $true; castShadows = $true; receiveShadows = $true; alwaysVisible = $false
                giReceive = 0; lightmapScale = 1.0; preserveLightmapUvs = $true; staticLighting = $false }
        }
    )
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
Copy-Item -LiteralPath $ModelSource -Destination (Join-Path $target 'NasaOrbiter.glb')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Source/NasaOrbiterSources.md') -Destination (Join-Path $target 'SOURCES.md')
@{
    schemaVersion = 1; id = $meshId; type = '4b454952-454d-4553-4841-535345540001'
    importer = 'Keire.Mesh'; importerVersion = 26; dependencies = @(); subAssets = @(); importSettings = @{}
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $target 'NasaOrbiter.glb.keiremeta') -Encoding UTF8
@{
    prefabSchemaVersion = 1; basePrefab = $null; schemaVersion = 3; name = 'NASA Orbiter'
    entities = @($entity); prefabInstances = @(); prefabOverrides = @()
} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $target 'NasaOrbiter.keireprefab') -Encoding UTF8
@{
    schemaVersion = 1; id = $prefabId; type = '4b454952-4550-5245-4641-424153535401'
    importer = 'Keire.Prefab'; importerVersion = 3; dependencies = @($meshId); subAssets = @()
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $target 'NasaOrbiter.keireprefab.keiremeta') -Encoding UTF8

if ($WireScene)
{
    foreach ($placeholder in $scene.entities | Where-Object { $_.parent -eq $ship[0].id -and $_.name -in @('Hull', 'Wings', 'Engine') })
    {
        $placeholder.active = $false
    }
    $entity.parent = $ship[0].id
    $scene.entities = @($scene.entities) + @($entity)
    $scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
    $metadata.dependencies = @($metadata.dependencies) + @($meshId)
    $metadata | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $sceneMetadataPath -Encoding UTF8
}
Write-Output 'Authored NASA orbiter mesh and reusable prefab. Scene wiring is optional; visual orientation and materials require acceptance.'
