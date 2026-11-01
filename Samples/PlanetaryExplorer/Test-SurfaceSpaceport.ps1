[CmdletBinding()]
param(
    [string]$AssetTool = (Join-Path $PSScriptRoot '../../Build/Distributions/keire-editor-windows-x86_64-DevDist/bin/KeireAssetTool.exe'),
    [switch]$SkipImport
)
$ErrorActionPreference = 'Stop'
$temporaryRoot = [IO.Path]::GetTempPath()
$fixture = Join-Path $temporaryRoot ('KeireSpaceport-' + [Guid]::NewGuid().ToString('N'))
try
{
    New-Item -ItemType Directory -Path (Join-Path $fixture 'Assets'), (Join-Path $fixture 'ProjectSettings') -Force | Out-Null
    $scenePath = Join-Path $fixture 'Assets/PlanetaryExplorer.keirescene'
    $original = @{ schemaVersion = 3; name = 'Spaceport fixture'; prefabInstances = @()
        entities = @(@{ name = 'Earth Launch Region'; id = '93dd6968-c276-41ce-9677-aea7c9cda72e'
            active = $true; layer = 0; parent = $null; tags = @(); components = @(
                @{ type = '4b454952-4554-5241-4e53-464f524d0001'; data = @{
                    position = @(0,0,0); rotation = @(0,0,0,1); scale = @(1,1,1) } },
                @{ type = '4b454952-454d-4553-4852-454e44455201'; data = @{
                    mesh = '2c3a6c74-583d-4319-afcf-2579ae7842de' } }) }) }
    $original | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $scenePath -Encoding utf8
    @{ schemaVersion = 1; id = '62093bd5-cb66-45da-9285-dc84a50978dd'; dependencies = @()
        importer = 'Keire.Scene'; importerVersion = 10; subAssets = @(); type = '4b454952-4553-4345-4e45-415353455401' } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath ($scenePath + '.keiremeta') -Encoding utf8
    $before = (Get-FileHash -LiteralPath $scenePath).Hash
    & (Join-Path $PSScriptRoot 'Author-SurfaceSpaceport.ps1') -Project $fixture
    $backup = Join-Path $fixture 'AuthoringBackups/BeforeSpaceport/PlanetaryExplorer.keirescene'
    if ((Get-FileHash -LiteralPath $backup).Hash -ne $before)
    {
        throw 'Original scene backup was not byte preserving.'
    }
    $authored = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
    $visuals = @($authored.entities | Where-Object name -like 'Spaceport - *')
    $colliders = @($authored.entities | Where-Object name -like 'Spaceport collision - *')
    if ($visuals.Count -ne 7 -or $colliders.Count -lt 30)
    {
        throw 'Spaceport batching or collision coverage regressed.'
    }
    if (@($authored.entities.id | Sort-Object -Unique).Count -ne $authored.entities.Count)
    {
        throw 'Duplicate authored entity identity.'
    }
    $target = Join-Path $fixture 'Assets/PlanetaryExplorer/Spaceport'
    foreach ($mesh in Get-ChildItem -LiteralPath $target -Filter '*.obj')
    {
        $vertices = 0
        $faces = 0
        foreach ($line in [IO.File]::ReadLines($mesh.FullName))
        {
            $parts = $line.Split(' ')
            if ($parts[0] -eq 'v')
            {
                $vertices++
                foreach ($coordinate in $parts[1..3])
                {
                    if (-not [double]::IsFinite([double]::Parse($coordinate, [Globalization.CultureInfo]::InvariantCulture)))
                    {
                        throw 'Non-finite mesh position.'
                    }
                }
            }
            if ($parts[0] -eq 'f')
            {
                $faces++
                foreach ($index in $parts[1..3])
                {
                    if ([int]$index -lt 1 -or [int]$index -gt $vertices)
                    {
                        throw 'Mesh triangle index out of bounds.'
                    }
                }
            }
        }
        if ($faces -lt 1 -or ($mesh.BaseName -eq 'Terrain' -and ($vertices -ne 25921 -or $faces -ne 51200)))
        {
            throw 'Mesh geometry count regressed.'
        }
    }
    $spaceportRoot = @($authored.entities | Where-Object name -eq 'Earth Spaceport')
    if ($spaceportRoot.Count -ne 1 -or $spaceportRoot[0].parent -or
        @($visuals + $colliders | Where-Object parent -ne $spaceportRoot[0].id).Count -ne 0)
    {
        throw 'Spaceport entities must remain grouped under one identity-transform root.'
    }
    # The second concrete box is the service apron at +520 Z. OBJ import reflects Z;
    # its source coordinates must therefore cover -845..-195, matching the collider after import.
    $concreteVertices = @(Get-Content (Join-Path $target 'Concrete.obj') | Where-Object { $_.StartsWith('v ') })
    $serviceZ = @($concreteVertices[8..15] | ForEach-Object { [double]($_.Split(' ')[3]) })
    if (($serviceZ | Measure-Object -Minimum).Minimum -ne -845 -or
        ($serviceZ | Measure-Object -Maximum).Maximum -ne -195)
    {
        throw 'Imported service apron visual/collider handedness mismatch.'
    }
    $terrainLines = @(Get-Content (Join-Path $target 'Terrain.obj'))
    $terrainVertices = @($terrainLines | Where-Object { $_.StartsWith('v ') })
    $firstFace = ($terrainLines | Where-Object { $_.StartsWith('f ') } | Select-Object -First 1).Split(' ')
    $points = @($firstFace[1..3] | ForEach-Object { ,($terrainVertices[[int]$_ - 1].Split(' ')[1..3] | ForEach-Object { [double]$_ }) })
    $normalY = ($points[1][2] - $points[0][2]) * ($points[2][0] - $points[0][0]) -
        ($points[1][0] - $points[0][0]) * ($points[2][2] - $points[0][2])
    if ($normalY -le 0)
    {
        throw 'Terrain source winding must face upward before the importer handedness conversion.'
    }
    $after = (Get-FileHash -LiteralPath $scenePath).Hash
    $rejected = $false
    try
    {
        & (Join-Path $PSScriptRoot 'Author-SurfaceSpaceport.ps1') -Project $fixture
    }
    catch
    {
        $rejected = $_.Exception.Message -like '*refusing to overwrite*'
    }
    if (-not $rejected -or (Get-FileHash -LiteralPath $scenePath).Hash -ne $after)
    {
        throw 'Second authoring pass must reject without changing authored data.'
    }
    if (-not $SkipImport)
    {
        # Separate mesh-only import proves the real importer accepts generated geometry without compiling unrelated shaders.
        $meshProject = Join-Path $fixture 'MeshImport'
        New-Item -ItemType Directory -Path (Join-Path $meshProject 'Assets'), (Join-Path $meshProject 'ProjectSettings') -Force | Out-Null
        @{ schemaVersion = 4; id = [Guid]::NewGuid().ToString('D'); name = 'Spaceport mesh fixture'
            createdWithEngineVersion = '0.4.8'; minimumEngineVersion = '0.4.8'; createdAt = '2026-01-01T00:00:00Z'
            lastSavedWithEngineVersion = '0.4.8'; requiredModules = @() } | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath (Join-Path $meshProject 'ProjectSettings/Project.keireproject') -Encoding utf8
        foreach ($mesh in Get-ChildItem -LiteralPath $target -Filter '*.obj')
        {
            Copy-Item -LiteralPath $mesh.FullName, ($mesh.FullName + '.keiremeta') -Destination (Join-Path $meshProject 'Assets')
        }
        & $AssetTool import --project $meshProject --worker-timeout-seconds 60
        if ($LASTEXITCODE -ne 0)
        {
            throw 'Production mesh import failed.'
        }
        $catalogPath = Join-Path $meshProject 'Library/AssetCache/Runtime/catalog.json'
        $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
        if (@($catalog.assets | Where-Object type -eq '4b454952-454d-4553-4841-535345540001').Count -ne 7)
        {
            throw 'Expected seven imported mesh assets.'
        }
        & $AssetTool validate --catalog $catalogPath
        if ($LASTEXITCODE -ne 0)
        {
            throw 'Mesh catalog validation failed.'
        }
    }
    Write-Output 'PASS: authoring preservation, deterministic identities, renderer budget, collider coverage, geometry and overwrite rejection.'
}
finally
{
    $resolved = [IO.Path]::GetFullPath($fixture)
    $prefix = [IO.Path]::GetFullPath($temporaryRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notlike 'KeireSpaceport-*')
    {
        throw 'Refusing to remove an unexpected fixture path.'
    }
    if (Test-Path -LiteralPath $resolved)
    {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
