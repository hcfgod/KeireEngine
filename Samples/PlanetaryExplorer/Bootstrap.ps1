[CmdletBinding()]
param(
    [string]$SourceProject = 'D:/Projects/KeireValidation/EarthSpaceFlight',
    [string]$Destination = 'D:/Projects/KeireValidation/PlanetaryExplorer',
    [string]$NasaSources = 'D:/Projects/KeireValidation/PlanetaryShowcaseSources/NASA'
)

$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $SourceProject).Path
$destinationRoot = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationRoot)
{
    throw "Destination already exists; refusing to overwrite authored work: $destinationRoot"
}
if ($destinationRoot.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
{
    throw 'Destination must not be inside the source project.'
}

$assetRoot = Join-Path $sourceRoot 'Assets'
$selected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$assetById = @{}
foreach ($metadata in Get-ChildItem -LiteralPath $assetRoot -Filter '*.keiremeta' -File -Recurse)
{
    $record = Get-Content -LiteralPath $metadata.FullName -Raw | ConvertFrom-Json
    if ($record.id)
    {
        $assetById[[string]$record.id] = $metadata.FullName.Substring(0, $metadata.FullName.Length - '.keiremeta'.Length)
    }
}

function Add-SourceAsset([string]$Path)
{
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        throw "Missing authored source dependency: $Path"
    }
    [void]$selected.Add($Path)
    if (Test-Path -LiteralPath ($Path + '.keiremeta'))
    {
        [void]$selected.Add($Path + '.keiremeta')
    }
}

Add-SourceAsset (Join-Path $assetRoot 'GravityAcceptanceQA.keirescene')
foreach ($folder in @('SpaceFlight', 'FirstPersonController', 'Input', 'Keire', 'Shaders', 'UI'))
{
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $assetRoot $folder) -File -Recurse)
    {
        Add-SourceAsset $file.FullName
    }
}
$settings = @('Project.keireproject', 'Authoring.keiresettings', 'BuildProfiles.keiresettings', 'BuildScenes.keiresettings', 'EnvironmentLighting.keiresettings',
    'EnvironmentTime.keiresettings', 'Planets.keiresettings', 'Player.keiresettings',
    'Rendering.keiresettings', 'Scripting.keiresettings', 'SharedShaders.lock')
foreach ($name in $settings)
{
    $path = Join-Path $sourceRoot "ProjectSettings/$name"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        throw "Missing project setting: $path"
    }
    [void]$selected.Add($path)
}
# Follow asset IDs in textual authored sources and importer metadata, not cached products.
$scanned = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
do
{
    $pending = @($selected | Where-Object { -not $scanned.Contains($_) })
    foreach ($path in $pending)
    {
        [void]$scanned.Add($path)
        if ([IO.Path]::GetExtension($path) -in @('.png', '.jpg', '.jpeg', '.fbx', '.glb', '.wav', '.ogg'))
        {
            continue
        }
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $path -Raw), '[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}'))
        {
            if ($assetById.ContainsKey($match.Value))
            {
                Add-SourceAsset $assetById[$match.Value]
            }
        }
    }
} while ($pending.Count -gt 0)

foreach ($name in @('Earth-BlueMarble-January-5400.jpg', 'Mars.jpg', 'SOURCES.md'))
{
    if (-not (Test-Path -LiteralPath (Join-Path $NasaSources $name) -PathType Leaf))
    {
        throw "Missing NASA source: $name"
    }
}

New-Item -ItemType Directory -Path $destinationRoot | Out-Null
$inventory = @()
foreach ($path in @($selected | Sort-Object))
{
    $relative = $path.Substring($sourceRoot.Length + 1)
    $target = Join-Path $destinationRoot $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $path -Destination $target
    $inventory += [pscustomobject]@{ source = $relative; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}

$projectId = 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30'
$sceneId = '629b1ea1-105f-48f2-8991-0db2b01d9124'
$projectPath = Join-Path $destinationRoot 'ProjectSettings/Project.keireproject'
$project = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
$project.id = $projectId
$project.name = 'Planetary Explorer'
$project.startupScene = $sceneId
$project | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $projectPath -Encoding UTF8
$playerPath = Join-Path $destinationRoot 'ProjectSettings/Player.keiresettings'
$player = Get-Content -LiteralPath $playerPath -Raw | ConvertFrom-Json
$player.applicationIdentifier = 'com.keire.samples.planetaryexplorer'
$player.productName = 'Planetary Explorer'
$player.windowTitle = 'Planetary Explorer'
$player | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $playerPath -Encoding UTF8
$authoringPath = Join-Path $destinationRoot 'ProjectSettings/Authoring.keiresettings'
$authoring = Get-Content -LiteralPath $authoringPath -Raw | ConvertFrom-Json
$authoring.externalEditorExecutable = ''
$authoring.externalEditorId = 'system'
$authoring | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $authoringPath -Encoding UTF8
$scenePath = Join-Path $destinationRoot 'Assets/GravityAcceptanceQA.keirescene'
$scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
$scene.name = 'PlanetaryExplorerBaseline'
$scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
$metadataPath = $scenePath + '.keiremeta'
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$metadata.id = $sceneId
$metadata | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $metadataPath -Encoding UTF8
Move-Item -LiteralPath $scenePath -Destination (Join-Path $destinationRoot 'Assets/PlanetaryExplorerBaseline.keirescene')
Move-Item -LiteralPath $metadataPath -Destination (Join-Path $destinationRoot 'Assets/PlanetaryExplorerBaseline.keirescene.keiremeta')
@{ schemaVersion = 1; scenes = @(@{ scene = $sceneId; enabled = $true }) } |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $destinationRoot 'ProjectSettings/BuildScenes.keiresettings') -Encoding UTF8

$textureTarget = Join-Path $destinationRoot 'Assets/Textures/NASA'
New-Item -ItemType Directory -Path $textureTarget -Force | Out-Null
foreach ($name in @('Earth-BlueMarble-January-5400.jpg', 'Mars.jpg', 'SOURCES.md'))
{
    Copy-Item -LiteralPath (Join-Path $NasaSources $name) -Destination (Join-Path $textureTarget $name)
}
$inventory | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $destinationRoot 'BootstrapInventory.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $destinationRoot 'README.md')
Write-Output "Created $destinationRoot with $($inventory.Count) selected source files. No imports or builds were run."
