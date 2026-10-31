[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Project).Path
$target = Join-Path $root 'Assets/PlanetaryExplorer'
if (Test-Path -LiteralPath $target)
{
    throw 'Manual flight sources already exist; refusing to overwrite them.'
}
$scenePath = Join-Path $root 'Assets/PlanetaryExplorer.keirescene'
$scene = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
$camera = $scene.entities | Where-Object { $_.name -eq 'Flight Camera' }
$director = $camera.components | Where-Object { $_.type -eq 'eb170000-2026-4000-8000-000000000001' }
if (@($director).Count -ne 1)
{
    throw 'Expected one baseline FlightDirector component.'
}
New-Item -ItemType Directory -Path $target | Out-Null
$ids = @{
    'PlanetaryFlightGeometry.cs' = '34b081f6-4109-4c49-a81c-4d982f6f6646'
    'FlightAutopilot.cs' = 'f2a58024-6bfc-4ce3-a32b-5011fb0813a6'
    'ShipCameraRig.cs' = '58628dd8-3ef6-486f-b49b-1f642ed978ae'
    'ShipFlightController.cs' = 'f8e65aac-fcf0-4c36-acfa-a5e91a5b9e20'
}
foreach ($name in $ids.Keys)
{
    $path = Join-Path $target $name
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Source/$name") -Destination $path
    @{ schemaVersion = 1; id = $ids[$name]; type = '4b454952-4554-4558-5441-535345540001'
        importer = 'Keire.Text'; importerVersion = 1; dependencies = @(); subAssets = @() } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath ($path + '.keiremeta') -Encoding UTF8
}
$uiId = 'bf735fe6-d2c7-4920-a208-069dbf998b8b'
& (Join-Path $PSScriptRoot 'Update-FlightHud.ps1') -Project $root
# Use the supported camera range. Distant Mars requires travel before its mesh is visible.
($camera.components | Where-Object { $_.type -eq '4b454952-4543-414d-4552-410000000001' }).data.farPlane = 10000000.0
$director.type = '517719fc-10d3-47c3-a6a7-8c4639b4cb5e'
($camera.components | Where-Object { $_.type -eq '4b454952-4555-4944-4f43-554d454e5401' }).data.visualTree = $uiId
$scene | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $scenePath -Encoding UTF8
$sceneMetadata = Get-Content -LiteralPath ($scenePath + '.keiremeta') -Raw | ConvertFrom-Json
$sceneMetadata.dependencies = @($sceneMetadata.dependencies) + @($uiId)
$sceneMetadata | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath ($scenePath + '.keiremeta') -Encoding UTF8
Write-Output 'Manual ship source and HUD wired into showcase only. Original baseline scene, FlightDirector and UI preserved. Not compiled or play-tested.'
