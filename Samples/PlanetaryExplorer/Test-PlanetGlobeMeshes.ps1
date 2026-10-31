[CmdletBinding()]
param(
    [string]$AssetTool = (Join-Path $PSScriptRoot '../../Build/Distributions/keire-editor-windows-x86_64-DevDist/bin/KeireAssetTool.exe'),
    [switch]$SkipImport
)

$ErrorActionPreference = 'Stop'
if (-not ('PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator' -as [type]))
{
    Add-Type -Path (Join-Path $PSScriptRoot 'Source/PlanetGlobeMeshGenerator.cs')
}
foreach ($radius in @(6360000.0, 3389500.0))
{
    $cap = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Create($radius, 4.0, 128, 256, $true)
    $body = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Create($radius, 4.0, 128, 256, $false)
    $capSag = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Validate($cap)
    $bodySag = [PlanetaryExplorer.Authoring.PlanetGlobeMeshGenerator]::Validate($body)
    if ($capSag -gt 2.3 -or $bodySag -gt 834 -or $cap.Indices.Length / 3 -ne 65280 -or
        $body.Indices.Length / 3 -ne 65280)
    {
        throw 'Globe geometry exceeded its documented error or triangle budget.'
    }
    for ($longitude = 0; $longitude -le 256; $longitude++)
    {
        $capVertex = $cap.Vertices[128 * 257 + $longitude]
        $bodyVertex = $body.Vertices[$longitude]
        if ([Math]::Abs($capVertex.Y + $radius - $bodyVertex.Y) -gt 0.00001 -or
            $capVertex.X -ne $bodyVertex.X -or $capVertex.Z -ne $bodyVertex.Z -or
            $capVertex.U -ne $bodyVertex.U -or $capVertex.V -ne $bodyVertex.V)
        {
            throw 'Cap/body boundary position or texture coordinates do not match.'
        }
        if ([Math]::Abs([double][float]$capVertex.Y + $radius - [double][float]$bodyVertex.Y) -gt 0.5)
        {
            throw 'Float mesh seam exceeds the distant-boundary tolerance.'
        }
    }
    # First four rings contain the launch point's approximately 13 km surface horizon.
    $nearFieldMaximumSag = 0.0
    for ($index = 0; $index -lt 7 * 256 * 3; $index += 3)
    {
        $a = $cap.Vertices[$cap.Indices[$index]]
        $b = $cap.Vertices[$cap.Indices[$index + 1]]
        $c = $cap.Vertices[$cap.Indices[$index + 2]]
        $x = ($a.X + $b.X + $c.X) / 3.0
        $y = ($a.Y + $b.Y + $c.Y) / 3.0 + $radius
        $z = ($a.Z + $b.Z + $c.Z) / 3.0
        $nearFieldMaximumSag = [Math]::Max($nearFieldMaximumSag, $radius - [Math]::Sqrt($x * $x + $y * $y + $z * $z))
    }
    if ($nearFieldMaximumSag -gt 0.25)
    {
        throw 'Launch-region surface error exceeds 25 cm.'
    }
    Write-Output ("PASS radius {0}: cap/body winding, bounds, UV seam and local error {1:F4} m." -f $radius, $nearFieldMaximumSag)
}
if ($SkipImport)
{
    return
}

$toolPath = (Resolve-Path -LiteralPath $AssetTool).Path
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireGlobeImport-' + [Guid]::NewGuid().ToString('N'))
try
{
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'Assets'),
        (Join-Path $fixtureRoot 'ProjectSettings') -Force | Out-Null
    @{
        schemaVersion = 4; id = [Guid]::NewGuid().ToString('D'); name = 'Globe import fixture'
        createdWithEngineVersion = '0.3.0'; minimumEngineVersion = '0.3.0'
        createdAt = '2026-01-01T00:00:00Z'; lastSavedWithEngineVersion = '0.3.0'; requiredModules = @()
    } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $fixtureRoot 'ProjectSettings/Project.keireproject') -Encoding UTF8
    $entities = @()
    foreach ($planet in @(
        @{ name = 'Earth'; id = '1b4da05b-c858-4114-a2a8-732399f42849'; radius = 6360000.0; x = 0.0 },
        @{ name = 'Mars'; id = '8c2a7844-919d-407b-9b61-ac5c4e47b7cf'; radius = 3389500.0; x = 40000000.0 }))
    {
        $diameter = 2.0 * $planet.radius
        $entities += @{
            id = $planet.id; name = $planet.name; active = $true; layer = 0; parent = $null
            components = @(
                @{ type = '4b454952-4554-5241-4e53-464f524d0001'; version = 2; enabled = $true
                    data = @{ position = @($planet.x, -$planet.radius, 0.0); rotation = @(0, 0, 0, 1)
                        scale = @($diameter, $diameter, $diameter) } },
                @{ type = '4b454952-454d-4553-4852-454e44455201'; version = 4; enabled = $true
                    data = @{ mesh = '4b454952-4553-5048-4552-454d45534801'; material = $null
                        tint = @(1.0, 1.0, 1.0, 1.0); visible = $true; castShadows = $true; receiveShadows = $true
                        alwaysVisible = $false; giReceive = 0; lightmapScale = 1.0
                        preserveLightmapUvs = $true; staticLighting = $false } }
            )
        }
    }
    $scenePath = Join-Path $fixtureRoot 'Assets/PlanetaryExplorer.keirescene'
    @{ schemaVersion = 3; name = 'Globe import fixture'; entities = $entities; prefabInstances = @() } |
        ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $scenePath -Encoding UTF8
    @{ schemaVersion = 1; id = '62093bd5-cb66-45da-9285-dc84a50978dd'; dependencies = @()
        importer = 'Keire.Scene'; importerVersion = 10; subAssets = @(); type = '4b454952-4553-4345-4e45-415353455401' } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath ($scenePath + '.keiremeta') -Encoding UTF8
    & (Join-Path $PSScriptRoot 'Author-PlanetGlobeMeshes.ps1') -Project $fixtureRoot
    & $toolPath import --project $fixtureRoot --worker-timeout-seconds 60
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Generated globe production import failed.'
    }
    $catalog = Get-Content -LiteralPath (Join-Path $fixtureRoot 'Library/AssetCache/Runtime/catalog.json') -Raw | ConvertFrom-Json
    if (@($catalog.assets | Where-Object type -eq '4b454952-454d-4553-4841-535345540001').Count -ne 4)
    {
        throw 'Expected all four generated globe meshes in the catalog.'
    }
    & $toolPath validate --catalog (Join-Path $fixtureRoot 'Library/AssetCache/Runtime/catalog.json')
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Generated globe catalog failed validation.'
    }
    Write-Output 'PASS sample globe authoring, production mesh/scene import and dependency catalog validation.'
}
finally
{
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'KeireGlobeImport-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot)
    {
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
