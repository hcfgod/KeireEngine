[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Project).Path
$scenePath = Join-Path $root 'Assets/PlanetaryExplorer.keirescene'
if (Test-Path -LiteralPath $scenePath)
{
    throw 'The authored showcase already exists; refusing to overwrite it.'
}
$manifestPath = Join-Path $root 'ProjectSettings/Project.keireproject'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.id -ne 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30' -or
    $manifest.startupScene -ne '629b1ea1-105f-48f2-8991-0db2b01d9124')
{
    throw 'Expected untouched Planetary Explorer bootstrap startup identity.'
}
$sceneId = '62093bd5-cb66-45da-9285-dc84a50978dd'
$earthTexture = '60243654-aee7-46bd-ad64-bd1cfb581e60'
$marsTexture = '287b4c9b-e406-42c3-9e97-7bb6ecfe0aba'
$earthMaterial = 'c015e7da-6592-4c66-9164-db833350ce35'
$marsMaterial = '82ed96ed-0d4b-4167-8dc7-97ef51b5ac3c'
$shader = 'b1b2c3d4-1000-4000-8000-000000000001'

function Write-Json([string]$Path, $Value)
{
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Write-PlanetAssets([string]$Name, [string]$Image, [string]$TextureId, [string]$MaterialId)
{
    $imagePath = Join-Path $root "Assets/Textures/NASA/$Image"
    if (-not (Test-Path -LiteralPath $imagePath))
    {
        throw "Missing source image: $imagePath"
    }
    Write-Json ($imagePath + '.keiremeta') @{
        schemaVersion = 1; id = $TextureId; type = '4b454952-4554-4558-5455-524532440001'
        importer = 'Keire.Texture2D'; importerVersion = 5; dependencies = @(); subAssets = @()
        textureImportSettings = @{
            semantic = 'color'; colorSpace = 'srgb'; mips = 'generate'; maximumSize = 8192
            sampler = @{ addressU = 'repeat'; addressV = 'clamp'; addressW = 'clamp'
                anisotropy = 8; min = 'linear'; mag = 'linear'; mip = 'linear' }
        }
    }
    $materialPath = Join-Path $root "Assets/Materials/Planets/$Name.keiremateriallegacy"
    Write-Json $materialPath @{
        schemaVersion = 2; shader = $shader
        surface = @{ alphaMode = 0; alphaCutoff = 0.5; doubleSided = $false }
        properties = @{ MainTexture = $TextureId; Tint = @(1.0, 1.0, 1.0, 1.0)
            MetallicFactor = 0.0; RoughnessFactor = 0.9; EmissiveFactor = @(0.0, 0.0, 0.0, 1.0) }
    }
    Write-Json ($materialPath + '.keiremeta') @{
        schemaVersion = 1; id = $MaterialId; type = '4b454952-454d-4154-4552-49414c000001'
        importer = 'Keire.Material'; importerVersion = 8; dependencies = @($shader, $TextureId); subAssets = @()
    }
}

$planetSettingsPath = Join-Path $root 'ProjectSettings/Planets.keiresettings'
$planets = Get-Content -LiteralPath $planetSettingsPath -Raw | ConvertFrom-Json
if ($planets.planets.Count -ne 1 -or $planets.planets[0].frame -ne 2)
{
    throw 'Planet settings have been edited; refusing to replace them.'
}
foreach ($path in @('Assets/Materials/Planets/Earth.keiremateriallegacy',
    'Assets/Materials/Planets/Mars.keiremateriallegacy',
    'Assets/Textures/NASA/Earth-BlueMarble-January-5400.jpg.keiremeta',
    'Assets/Textures/NASA/Mars.jpg.keiremeta'))
{
    if (Test-Path -LiteralPath (Join-Path $root $path))
    {
        throw "Authored asset already exists: $path"
    }
}
New-Item -ItemType Directory -Path (Join-Path $root 'Assets/Materials/Planets') -Force | Out-Null
Write-PlanetAssets 'Earth' 'Earth-BlueMarble-January-5400.jpg' $earthTexture $earthMaterial
Write-PlanetAssets 'Mars' 'Mars.jpg' $marsTexture $marsMaterial

$baselinePath = Join-Path $root 'Assets/PlanetaryExplorerBaseline.keirescene'
$scene = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
$scene.name = 'PlanetaryExplorer'
$earth = $scene.entities | Where-Object { $_.name -eq 'Earth Surface' }
if (@($earth).Count -ne 1)
{
    throw 'Expected one Earth Surface entity in baseline.'
}
$renderer = $earth.components | Where-Object { $_.type -eq '4b454952-454d-4553-4852-454e44455201' }
$renderer.data.material = $earthMaterial
$renderer.data.tint = @(1.0, 1.0, 1.0, 1.0)
$mars = $earth | ConvertTo-Json -Depth 100 | ConvertFrom-Json
$mars.id = '8c2a7844-919d-407b-9b61-ac5c4e47b7cf'
$mars.name = 'Mars Surface - environment profile pending'
$transform = $mars.components | Where-Object { $_.type -eq '4b454952-4554-5241-4e53-464f524d0001' }
# Compressed showcase separation, not the real Earth/Mars ephemeris.
$transform.data.position = @(40000000.0, -3389500.0, 0.0)
$transform.data.scale = @(6779000.0, 6779000.0, 6779000.0)
($mars.components | Where-Object { $_.type -eq '4b454952-454d-4553-4852-454e44455201' }).data.material = $marsMaterial
$scene.entities = @($scene.entities) + @($mars)
Write-Json $scenePath $scene
$meta = Get-Content -LiteralPath ($baselinePath + '.keiremeta') -Raw | ConvertFrom-Json
$meta.id = $sceneId
$meta.dependencies = @($meta.dependencies) + @($earthMaterial, $marsMaterial)
Write-Json ($scenePath + '.keiremeta') $meta

# Manual Earth selection deliberately remains unchanged: Mars must not inherit Earth's cloud shell.
$marsDefinition = $planets.planets[0] | ConvertTo-Json -Depth 100 | ConvertFrom-Json
$marsDefinition.planet = '8238b398-bb92-49fe-bbcf-8f0e81c060a5'
$marsDefinition.frame = 3
$marsDefinition.radiusMetres = 3389500.0
$marsDefinition.gravitationalParameter = 42828370000000.0
$marsDefinition.sectors = @(610, -52, 0)
$marsDefinition.offsets = @(23040.0, 18372.0, 0.0)
$planets.planets = @($planets.planets) + @($marsDefinition)
Write-Json $planetSettingsPath $planets
$manifest.startupScene = $sceneId
Write-Json $manifestPath $manifest
Write-Json (Join-Path $root 'ProjectSettings/BuildScenes.keiresettings') @{
    schemaVersion = 1; scenes = @(@{ scene = $sceneId; enabled = $true })
}
Write-Output 'Authored Earth/Mars material sources and distinct showcase scene. Baseline scene preserved. No imports or visual validation performed.'
