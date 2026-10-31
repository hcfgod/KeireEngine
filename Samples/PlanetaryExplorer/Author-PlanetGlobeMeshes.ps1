[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
if (-not ('PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator' -as [type]))
{
    Add-Type -Path (Join-Path $PSScriptRoot 'Source/PlanetGlobeMeshGenerator.cs')
}
$root = (Resolve-Path -LiteralPath $Project).Path
$target = Join-Path $root 'Assets/PlanetaryExplorer/GlobeMeshes'
$backup = Join-Path $root 'AuthoringBackups/BeforeGlobeMeshes'
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $backup))
{
    throw 'Generated globe assets or their backup already exist; refusing to overwrite authored work.'
}
$scenePath = Join-Path $root 'Assets/PlanetaryExplorer.keirescene'
$scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
$metadata = Get-Content -LiteralPath ($scenePath + '.keiremeta') -Raw | ConvertFrom-Json
$definitions = @(
    @{ name = 'Earth'; entity = '1b4da05b-c858-4114-a2a8-732399f42849'; radius = 6360000.0
        capEntity = '93dd6968-c276-41ce-9677-aea7c9cda72e'; capMesh = '2c3a6c74-583d-4319-afcf-2579ae7842de'
        bodyMesh = '86ff78dc-2c80-4645-a0e1-42ffb77021d8' },
    @{ name = 'Mars'; entity = '8c2a7844-919d-407b-9b61-ac5c4e47b7cf'; radius = 3389500.0
        capEntity = 'ebac720c-736e-436e-8b6c-96e8f4283f13'; capMesh = 'a9a6d656-a465-4e32-98c6-8295b2267cd0'
        bodyMesh = '17f4ac43-2dd2-4960-8450-7181cd3de3d9' }
)
foreach ($definition in $definitions)
{
    $entities = @($scene.entities | Where-Object id -eq $definition.entity)
    if ($entities.Count -ne 1)
    {
        throw "Expected one $($definition.name) surface."
    }
    $transform = $entities[0].components | Where-Object type -eq '4b454952-4554-5241-4e53-464f524d0001'
    $renderer = $entities[0].components | Where-Object type -eq '4b454952-454d-4553-4852-454e44455201'
    if ($entities[0].parent -or ($transform.data.rotation -join ',') -ne '0,0,0,1' -or
        $renderer.data.mesh -ne '4b454952-4553-5048-4552-454d45534801' -or
        @($transform.data.scale | Where-Object { [Math]::Abs($_ - 2.0 * $definition.radius) -gt 0.01 }).Count -ne 0)
    {
        throw "$($definition.name) no longer matches the unrotated built-in sphere baseline."
    }
}

New-Item -ItemType Directory -Path $target -Force | Out-Null
foreach ($definition in $definitions)
{
    foreach ($part in @('Cap', 'Body'))
    {
        $isCap = $part -eq 'Cap'
        $mesh = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Create($definition.radius, 4.0, 128, 256, $isCap)
        $sag = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Validate($mesh)
        $path = Join-Path $target "$($definition.name)$part.obj"
        $mesh.WriteObj($path)
        $assetId = if ($isCap) { $definition.capMesh } else { $definition.bodyMesh }
        @{
            schemaVersion = 1; id = $assetId; type = '4b454952-454d-4553-4841-535345540001'
            importer = 'Keire.Mesh'; importerVersion = 26; dependencies = @(); subAssets = @(); importSettings = @{}
        } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath ($path + '.keiremeta') -Encoding UTF8
        Write-Output ("{0} {1}: {2} triangles, maximum centroid sag {3:F3} m." -f
            $definition.name, $part, ($mesh.Indices.Length / 3), $sag)
    }
    $body = $scene.entities | Where-Object id -eq $definition.entity
    $cap = $body | ConvertTo-Json -Depth 100 | ConvertFrom-Json
    $cap.id = $definition.capEntity
    $cap.name = "$($definition.name) Launch Region"
    $capTransform = $cap.components | Where-Object type -eq '4b454952-4554-5241-4e53-464f524d0001'
    $capTransform.data.position[1] += $definition.radius
    $capTransform.data.scale = @(1.0, 1.0, 1.0)
    ($cap.components | Where-Object type -eq '4b454952-454d-4553-4852-454e44455201').data.mesh = $definition.capMesh
    ($body.components | Where-Object type -eq '4b454952-4554-5241-4e53-464f524d0001').data.scale = @(1.0, 1.0, 1.0)
    ($body.components | Where-Object type -eq '4b454952-454d-4553-4852-454e44455201').data.mesh = $definition.bodyMesh
    $scene.entities = @($scene.entities) + @($cap)
    $metadata.dependencies = @($metadata.dependencies) + @($definition.capMesh, $definition.bodyMesh)
}
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $scenePath, ($scenePath + '.keiremeta') -Destination $backup
$scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
$metadata | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath ($scenePath + '.keiremeta') -Encoding UTF8
Write-Output 'Replaced the two sample spheres with non-overlapping polar caps and globe bodies. Materials preserved; no engine primitives changed.'
