[CmdletBinding()]
param([string]$Destination = 'D:/Projects/KeireValidation/PlanetaryShowcaseSources/NASA')

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$original = Join-Path $Destination 'SpaceShuttleB.glb'
$converted = Join-Path $Destination 'SpaceShuttleB-Uncompressed.glb'
$sourceHash = 'FBB2443EFD9FC1F06C1C267EFD2EA0E02C3D3CC442F925B1D5D222B4DE4B736A'
$convertedHash = 'F983739A0185DA1756C9A28F55A2CDA2EA869EABE939953EFF88B4D61B4389B2'
if (-not (Test-Path -LiteralPath $original))
{
    Invoke-WebRequest -Uri 'https://assets.science.nasa.gov/content/dam/science/cds/3d/resources/model/space-shuttle-%28b%29/Space%20Shuttle%20%28B%29.glb' `
        -OutFile $original
}
if ((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne $sourceHash)
{
    throw 'NASA orbiter source changed; review the new source before using it.'
}
if (-not (Test-Path -LiteralPath $converted))
{
    & npx --yes '@gltf-transform/cli@4.5.0' copy $original $converted
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Orbiter decompression failed.'
    }
}
if ((Get-FileHash -LiteralPath $converted -Algorithm SHA256).Hash -ne $convertedHash)
{
    throw 'Converted orbiter differs from the validated derivative.'
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Source/NasaOrbiterSources.md') `
    -Destination (Join-Path $Destination 'SpaceShuttleB-SOURCES.md')
Write-Output "Validated original and uncompressed NASA orbiter at $Destination."
