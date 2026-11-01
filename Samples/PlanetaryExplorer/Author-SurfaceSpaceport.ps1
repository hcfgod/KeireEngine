[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Project).Path
$scenePath = Join-Path $root 'Assets/PlanetaryExplorer.keirescene'
$target = Join-Path $root 'Assets/PlanetaryExplorer/Spaceport'
$backup = Join-Path $root 'AuthoringBackups/BeforeSpaceport'
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $backup))
{
    throw 'Spaceport assets or backup already exist; refusing to overwrite authored work.'
}
$scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
$metadata = Get-Content -LiteralPath ($scenePath + '.keiremeta') -Raw | ConvertFrom-Json
if (@($scene.entities | Where-Object name -eq 'Earth Launch Region').Count -ne 1)
{
    throw 'Expected the Planetary Explorer globe/cap setup.'
}
$launch = $scene.entities | Where-Object name -eq 'Earth Launch Region'
$launchTransform = $launch.components | Where-Object type -eq '4b454952-4554-5241-4e53-464f524d0001'
$launchRenderer = $launch.components | Where-Object type -eq '4b454952-454d-4553-4852-454e44455201'
if ($launch.id -ne '93dd6968-c276-41ce-9677-aea7c9cda72e' -or $launch.parent -or
    ($launchTransform.data.position -join ',') -ne '0,0,0' -or
    ($launchTransform.data.rotation -join ',') -ne '0,0,0,1' -or
    ($launchTransform.data.scale -join ',') -ne '1,1,1' -or
    $launchRenderer.data.mesh -ne '2c3a6c74-583d-4319-afcf-2579ae7842de')
{
    throw 'Earth launch cap must retain the unrotated, unit-scale origin baseline.'
}
$capSource = Join-Path $root 'Assets/PlanetaryExplorer/GlobeMeshes/EarthCap.obj'
if (-not (Test-Path -LiteralPath $capSource))
{
    throw 'Missing original Earth cap source for terrain integration.'
}
if (-not ('PlanetaryExplorer.Authoring.SpaceportCapCutout' -as [type]))
{
    Add-Type -Path (Join-Path $PSScriptRoot 'Source/SpaceportCapCutout.cs')
}
$shader = 'b1b2c3d4-1000-4000-8000-000000000001'
$groups = @{}
$colliders = [Collections.Generic.List[object]]::new()
$materialDefinitions = @(
    @{ name = 'Concrete'; color = @(0.30, 0.34, 0.36, 1.0); metal = 0.0; rough = 0.88 },
    @{ name = 'Ceramic'; color = @(0.72, 0.75, 0.72, 1.0); metal = 0.25; rough = 0.45 },
    @{ name = 'Steel'; color = @(0.12, 0.18, 0.22, 1.0); metal = 0.75; rough = 0.36 },
    @{ name = 'Glass'; color = @(0.035, 0.18, 0.26, 1.0); metal = 0.55; rough = 0.18 },
    @{ name = 'Markings'; color = @(0.92, 0.67, 0.15, 1.0); metal = 0.0; rough = 0.65 },
    @{ name = 'Beacon'; color = @(0.15, 0.70, 0.90, 1.0); metal = 0.0; rough = 0.3 },
    @{ name = 'Terrain'; color = @(0.16, 0.24, 0.13, 1.0); metal = 0.0; rough = 1.0 }
)
foreach ($definition in $materialDefinitions)
{
    $groups[$definition.name] = [Collections.Generic.List[object]]::new()
}
function Get-StableId([string]$Name)
{
    $bytes = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes('Keire.Spaceport.v1.' + $Name))
    return [Guid]::new([byte[]]$bytes[0..15]).ToString()
}
function Write-Json([string]$Path, $Value)
{
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Add-Box([string]$Name, [string]$Material, [double[]]$Position, [double[]]$Size, [bool]$Collision = $false)
{
    $box = @{ name = $Name; position = $Position; size = $Size }
    $groups[$Material].Add($box)
    if ($Collision)
    {
        $colliders.Add($box)
    }
}
function New-Entity([string]$Name, [double[]]$Position, [double[]]$Scale, $Mesh, $Material, [bool]$Collision)
{
    $components = @(@{ type = '4b454952-4554-5241-4e53-464f524d0001'; version = 2; enabled = $true
        data = @{ position = $Position; rotation = @(0.0, 0.0, 0.0, 1.0); scale = $Scale } })
    if ($Mesh)
    {
        $components += @{ type = '4b454952-454d-4553-4852-454e44455201'; version = 4; enabled = $true
            data = @{ mesh = $Mesh; material = $Material; tint = @(1.0, 1.0, 1.0, 1.0); visible = $true
                castShadows = $true; receiveShadows = $true; alwaysVisible = $false; staticLighting = $false
                giReceive = 0; lightmapScale = 1.0; preserveLightmapUvs = $true } }
    }
    if ($Collision)
    {
        $components += @{ type = '4b454952-4543-4f4c-4c49-444552000001'; version = 2; enabled = $true
            data = @{ center = @(0.0, 0.0, 0.0); collisionMesh = $null; halfExtent = @(0.5, 0.5, 0.5)
                height = 1.0; radius = 0.5; layer = 1; mask = 4294967295; physicsMaterial = $null
                shape = 0; trigger = $false } }
    }
    return @{ id = (Get-StableId $Name); name = $Name; active = $true; layer = 0; parent = (Get-StableId 'Earth Spaceport')
        tags = @(); components = $components }
}
# Keep the origin launch corridor clear; the existing ship begins twelve metres above this pad.
Add-Box 'Primary launch apron' Concrete @(0, 0, 0) @(260, 4, 320) $true
Add-Box 'Northern service apron' Concrete @(0, 0, 520) @(900, 4, 650) $true
Add-Box 'Access road' Steel @(0, 2.08, 1250) @(32, 0.12, 1000)
foreach ($side in @(-1, 1))
{
    Add-Box "Apron edge $side" Markings @(($side * 125), 2.12, 0) @(1.5, 0.1, 310)
    for ($index = 0; $index -lt 7; $index++)
    {
        Add-Box "Pad edge beacon $side $index" Beacon @(($side * 115), 2.35, (-140 + $index * 45)) @(2, 0.5, 4)
    }
    for ($index = 0; $index -lt 3; $index++)
    {
        $x = $side * 300
        $z = 300 + $index * 175
        Add-Box "Hangar $side $index roof" Ceramic @($x, 45, $z) @(180, 6, 135) $true
        Add-Box "Hangar $side $index rear" Concrete @($x, 22, ($z + 64)) @(180, 44, 6) $true
        foreach ($wall in @(-1, 1))
        {
            Add-Box "Hangar $side $index wall $wall" Ceramic @(($x + $wall * 87), 22, $z) @(6, 44, 135) $true
            Add-Box "Hangar $side $index frame $wall" Steel @(($x + $wall * 80), 24, ($z - 68)) @(5, 48, 8)
        }
        Add-Box "Hangar $side $index door lintel" Steel @($x, 43, ($z - 68)) @(165, 8, 8)
        Add-Box "Hangar $side $index strip" Beacon @($x, 34, ($z - 72.2)) @(130, 1.2, 0.4)
        for ($rib = -3; $rib -le 3; $rib++)
        {
            Add-Box "Hangar $side $index roof rib $rib" Steel @(($x + $rib * 24), 49, $z) @(2, 2, 137)
        }
    }
}
Add-Box 'Operations tower base' Concrete @(180, 35, 10) @(46, 70, 46) $true
Add-Box 'Operations tower crown' Ceramic @(180, 76, 10) @(72, 12, 66) $true
Add-Box 'Operations observation windows' Glass @(180, 86, 10) @(68, 9, 62)
Add-Box 'Operations tower roof' Steel @(180, 92, 10) @(76, 3, 70)
Add-Box 'Operations antenna' Steel @(180, 113, 10) @(2, 40, 2)
Add-Box 'Operations warning beacon' Beacon @(180, 134, 10) @(3, 2, 3)
foreach ($x in @(-90, 90))
{
    Add-Box "Launch gantry $x" Steel @($x, 45, 20) @(9, 90, 12) $true
    for ($level = 1; $level -le 5; $level++)
    {
        Add-Box "Launch gantry deck $x $level" Ceramic @($x, ($level * 15), 20) @(20, 2, 24)
    }
}
for ($index = 0; $index -lt 18; $index++)
{
    Add-Box "Road dash $index" Markings @(0, 2.18, (840 + $index * 50)) @(1, 0.06, 22)
}
for ($index = 0; $index -lt 8; $index++)
{
    $x = -480 + ($index % 4) * 45
    $z = 50 + [Math]::Floor($index / 4) * 55
    Add-Box "Fuel vessel $index" Ceramic @($x, 16, $z) @(30, 32, 36) $true
    Add-Box "Fuel vessel band $index" Steel @($x, 18, $z) @(31, 2, 37)
    Add-Box "Fuel vessel pipe $index" Steel @($x, 3, ($z - 24)) @(4, 4, 16)
}
for ($index = 0; $index -lt 16; $index++)
{
    $x = 550 + ($index % 4) * 55
    $z = 200 + [Math]::Floor($index / 4) * 65
    Add-Box "Solar array support $index" Steel @($x, 3, $z) @(2, 6, 3)
    Add-Box "Solar array $index" Glass @($x, 6.5, $z) @(45, 1, 50)
    Add-Box "Solar array spine $index" Ceramic @($x, 7.1, $z) @(1, 0.2, 50)
}
# Per-material geometry batching keeps this authored detail to seven additional renderers.
New-Item -ItemType Directory -Path $target | Out-Null
$dependencies = [Collections.Generic.List[string]]::new()
$entities = [Collections.Generic.List[object]]::new()
$spaceportRoot = New-Entity 'Earth Spaceport' @(0,0,0) @(1,1,1) $null $null $false
$spaceportRoot.parent = $null
$entities.Add($spaceportRoot)
$invariant = [Globalization.CultureInfo]::InvariantCulture
function Write-Vertex($Writer, [double]$X, [double]$Y, [double]$Z)
{
    $Writer.WriteLine(('v {0} {1} {2}' -f $X.ToString('R', $invariant), $Y.ToString('R', $invariant), (-$Z).ToString('R', $invariant)))
}
foreach ($definition in $materialDefinitions)
{
    $name = $definition.name
    $meshId = Get-StableId ($name + '.Mesh')
    $materialId = Get-StableId ($name + '.Material')
    $path = Join-Path $target ($name + '.obj')
    $writer = [IO.StreamWriter]::new($path, $false, [Text.UTF8Encoding]::new($false))
    try
    {
        $vertex = 1
        foreach ($box in $groups[$name])
        {
            foreach ($corner in @(@(-1,-1,-1), @(1,-1,-1), @(1,1,-1), @(-1,1,-1), @(-1,-1,1), @(1,-1,1), @(1,1,1), @(-1,1,1)))
            {
                Write-Vertex $writer ($box.position[0] + $corner[0] * $box.size[0] / 2) ($box.position[1] + $corner[1] * $box.size[1] / 2) ($box.position[2] + $corner[2] * $box.size[2] / 2)
            }
            foreach ($face in @(@(0,3,2,1), @(4,5,6,7), @(0,1,5,4), @(3,7,6,2), @(0,4,7,3), @(1,2,6,5)))
            {
                $writer.WriteLine(('f {0} {1} {2}' -f ($vertex+$face[0]), ($vertex+$face[2]), ($vertex+$face[1])))
                $writer.WriteLine(('f {0} {1} {2}' -f ($vertex+$face[0]), ($vertex+$face[3]), ($vertex+$face[2])))
            }
            $vertex += 8
        }
        if ($name -eq 'Terrain')
        {
            $resolution = 160
            $halfWidth = 8000.0
            for ($zIndex = 0; $zIndex -le $resolution; $zIndex++)
            {
                for ($xIndex = 0; $xIndex -le $resolution; $xIndex++)
                {
                    $x = -$halfWidth + 2 * $halfWidth * $xIndex / $resolution
                    $z = -$halfWidth + 2 * $halfWidth * $zIndex / $resolution
                    $distance = [Math]::Sqrt($x*$x + $z*$z)
                    $blend = [Math]::Clamp(($distance - 1850) / 2000, 0, 1)
                    $edge = [Math]::Clamp(($halfWidth - [Math]::Max([Math]::Abs($x), [Math]::Abs($z))) / 800, 0, 1)
                    $ridge = [Math]::Pow(0.5 + 0.5 * [Math]::Sin($x/710 + [Math]::Sin($z/930)), 2)
                    $height = 0.5 - ($x*$x + $z*$z)/12720000 + $blend*$edge*(60 + 520*$ridge + 55*[Math]::Sin($z/310)*[Math]::Sin($x/450))
                    Write-Vertex $writer $x $height $z
                }
            }
            for ($zIndex = 0; $zIndex -lt $resolution; $zIndex++)
            {
                for ($xIndex = 0; $xIndex -lt $resolution; $xIndex++)
                {
                    $a = 1 + $zIndex * ($resolution + 1) + $xIndex
                    $b = $a + 1
                    $c = $a + $resolution + 1
                    $d = $c + 1
                    $writer.WriteLine("f $a $b $c")
                    $writer.WriteLine("f $b $d $c")
                }
            }
        }
    }
    finally
    {
        $writer.Dispose()
    }
    Write-Json ($path + '.keiremeta') @{ schemaVersion = 1; id = $meshId
        type = '4b454952-454d-4553-4841-535345540001'; importer = 'Keire.Mesh'; importerVersion = 26
        dependencies = @(); subAssets = @(); importSettings = @{} }
    $emission = if ($name -eq 'Beacon') { @(0.15, 1.5, 2.2, 1.0) } else { @(0.0, 0.0, 0.0, 1.0) }
    $materialPath = Join-Path $target ($name + '.keiremateriallegacy')
    Write-Json $materialPath @{ schemaVersion = 2; shader = $shader
        surface = @{ alphaMode = 0; alphaCutoff = 0.5; doubleSided = $false }
        properties = @{ Tint = $definition.color; MetallicFactor = $definition.metal
            RoughnessFactor = $definition.rough; EmissiveFactor = $emission } }
    Write-Json ($materialPath + '.keiremeta') @{ schemaVersion = 1; id = $materialId
        type = '4b454952-454d-4154-4552-49414c000001'; importer = 'Keire.Material'; importerVersion = 8
        dependencies = @($shader); subAssets = @() }
    $entities.Add((New-Entity ('Spaceport - ' + $name) @(0,0,0) @(1,1,1) $meshId $materialId $false))
    $dependencies.Add($meshId)
    $dependencies.Add($materialId)
}
$cutoutPath = Join-Path $target 'EarthCapWithSpaceport.obj'
Write-Output ([PlanetaryExplorer.Authoring.SpaceportCapCutout]::Generate($capSource, $cutoutPath, (Join-Path $target 'Terrain.obj')))
$cutoutId = Get-StableId 'EarthCapWithSpaceport.Mesh'
Write-Json ($cutoutPath + '.keiremeta') @{ schemaVersion = 1; id = $cutoutId
    type = '4b454952-454d-4553-4841-535345540001'; importer = 'Keire.Mesh'; importerVersion = 26
    dependencies = @(); subAssets = @(); importSettings = @{} }
$launchRenderer.data.mesh = $cutoutId
$dependencies.Add($cutoutId)
foreach ($box in $colliders)
{
    $entities.Add((New-Entity ('Spaceport collision - ' + $box.name) $box.position $box.size $null $null $true))
}
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $scenePath, ($scenePath + '.keiremeta') -Destination $backup
$scene.entities = @($scene.entities) + $entities.ToArray()
$metadata.dependencies = @($metadata.dependencies) + $dependencies.ToArray()
Write-Json $scenePath $scene
Write-Json ($scenePath + '.keiremeta') $metadata
Write-Output "Added $($entities.Count) entities: seven batched visual meshes, $($colliders.Count) static box colliders, a 16 km terrain patch, and a detailed surface spaceport. Terrain is visual only; flight remains kinematic."
