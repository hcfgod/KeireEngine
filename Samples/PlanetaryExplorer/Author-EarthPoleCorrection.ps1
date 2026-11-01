[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path -LiteralPath $Project).Path
$source = Join-Path $root 'Assets/Textures/NASA/Earth-BlueMarble-January-5400.jpg'
$expectedHash = '99F5FAAD74EFE985FBF1714C8BE7296CA9999759A1215B65F99B7F1DF278DDE5'
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $expectedHash)
{
    throw 'This correction applies only to the audited NASA January 5400-pixel source. Source left unchanged.'
}
$destination = Join-Path $root 'Assets/Textures/NASA/Earth-BlueMarble-January-5400-PoleCorrected.png'
$materialPath = Join-Path $root 'Assets/Materials/Planets/Earth.keiremateriallegacy'
$backup = Join-Path $root 'AuthoringBackups/BeforeEarthPoleCorrection'
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $backup))
{
    throw 'Pole correction or backup already exists; refusing to overwrite authored work.'
}
$material = Get-Content -LiteralPath $materialPath -Raw | ConvertFrom-Json
$materialMeta = Get-Content -LiteralPath ($materialPath + '.keiremeta') -Raw | ConvertFrom-Json
$oldTexture = '60243654-aee7-46bd-ad64-bd1cfb581e60'
$newTexture = '81d7ad0d-bae0-46c0-9d15-cd91d70d4001'
if ($material.properties.MainTexture -ne $oldTexture)
{
    throw 'Earth material no longer references the audited source; refusing to replace an authored texture.'
}
$textureMeta = Get-Content -LiteralPath ($source + '.keiremeta') -Raw | ConvertFrom-Json
$bitmap = [System.Drawing.Bitmap]::new($source)
try
{
    if ($bitmap.Width -ne 5400 -or $bitmap.Height -ne 2700)
    {
        throw 'Unexpected source dimensions.'
    }
    # The source has a bright non-geographic border artifact in its northeast corner.
    # This hash-bound 7.2-degree longitude / 6-degree latitude ocean patch is repaired
    # from the same latitude's unaffected ocean. No mesh or global UV remapping occurs.
    for ($y = 0; $y -lt 90; $y++)
    {
        $ocean = $bitmap.GetPixel(100, $y)
        for ($x = 5292; $x -lt 5400; $x++)
        {
            $bitmap.SetPixel($x, $y, $ocean)
        }
    }
    $bitmap.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally
{
    $bitmap.Dispose()
}
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $materialPath, ($materialPath + '.keiremeta') -Destination $backup
$textureMeta.id = $newTexture
$textureMeta | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath ($destination + '.keiremeta') -Encoding UTF8
$material.properties.MainTexture = $newTexture
$materialMeta.dependencies = @($materialMeta.dependencies | ForEach-Object {
    if ($_ -eq $oldTexture)
    {
        $newTexture
    }
    else
    {
        $_
    }
})
$material | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $materialPath -Encoding UTF8
$materialMeta | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath ($materialPath + '.keiremeta') -Encoding UTF8
Write-Output 'Authored a separate pole-corrected Earth texture; original NASA image and material backup preserved. Reimport before visual acceptance.'

