[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Project).Path
$target = Join-Path $root 'Assets/PlanetaryExplorer'
if (-not (Test-Path -LiteralPath $target -PathType Container))
{
    throw 'Install manual flight before updating its HUD.'
}

$uiId = 'bf735fe6-d2c7-4920-a208-069dbf998b8b'
$styleId = 'e5208ad4-7632-459f-9e67-b96ce72fc39d'
foreach ($extension in @('keireui', 'keirestyle'))
{
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Source/FlightHud.$extension") `
        -Destination (Join-Path $target "FlightHud.$extension")
}
@{
    schemaVersion = 1; id = $styleId; type = '4b454952-4555-4953-5459-4c4500000001'
    importer = 'Keire.UiStyleSheet'; importerVersion = 2; dependencies = @(); subAssets = @(); importSettings = @{}
} | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $target 'FlightHud.keirestyle.keiremeta') -Encoding UTF8
@{
    schemaVersion = 1; id = $uiId; type = '4b454952-4555-4954-5245-450000000001'
    importer = 'Keire.UiVisualTree'; importerVersion = 1; dependencies = @($styleId); subAssets = @(); importSettings = @{}
} | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $target 'FlightHud.keireui.keiremeta') -Encoding UTF8
Write-Output 'Updated the flight HUD and its private stylesheet only; scenes and baseline UI were not changed.'
