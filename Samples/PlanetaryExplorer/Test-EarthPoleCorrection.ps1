[CmdletBinding()]
param([string]$SourceProject = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture = Join-Path $temporaryRoot ('KeirePoleCorrection-' + [Guid]::NewGuid().ToString('N'))
$relativeSource = 'Assets/Textures/NASA/Earth-BlueMarble-January-5400.jpg'
$relativeMaterial = 'Assets/Materials/Planets/Earth.keiremateriallegacy'
try
{
    foreach ($relative in @($relativeSource, ($relativeSource + '.keiremeta'),
        $relativeMaterial, ($relativeMaterial + '.keiremeta')))
    {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $SourceProject $relative) -Destination $target
    }
    $materialPath = Join-Path $fixture $relativeMaterial
    $material = Get-Content -LiteralPath $materialPath -Raw | ConvertFrom-Json
    $material.properties.MainTexture = '60243654-aee7-46bd-ad64-bd1cfb581e60'
    $material | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $materialPath -Encoding UTF8
    $source = Join-Path $fixture $relativeSource
    $originalHash = (Get-FileHash -LiteralPath $source).Hash
    & (Join-Path $PSScriptRoot 'Author-EarthPoleCorrection.ps1') -Project $fixture
    if ((Get-FileHash -LiteralPath $source).Hash -ne $originalHash)
    {
        throw 'Original source was modified.'
    }
    $original = [System.Drawing.Bitmap]::new($source)
    $corrected = [System.Drawing.Bitmap]::new((Join-Path $fixture 'Assets/Textures/NASA/Earth-BlueMarble-January-5400-PoleCorrected.png'))
    try
    {
        for ($y = 0; $y -lt 90; $y++)
        {
            for ($x = 5292; $x -lt 5400; $x++)
            {
                if ($corrected.GetPixel($x, $y).ToArgb() -ne $original.GetPixel(100, $y).ToArgb())
                {
                    throw 'Known border artifact did not receive the same-latitude ocean correction.'
                }
            }
        }
        for ($y = 0; $y -lt 2700; $y += 17)
        {
            for ($x = 0; $x -lt 5400; $x += 23)
            {
                if ($x -ge 5292 -and $y -lt 90)
                {
                    continue
                }
                if ($corrected.GetPixel($x, $y).ToArgb() -ne $original.GetPixel($x, $y).ToArgb())
                {
                    throw 'Pixels outside the correction patch changed.'
                }
            }
        }
    }
    finally
    {
        $original.Dispose()
        $corrected.Dispose()
    }
    $material = Get-Content -LiteralPath $materialPath -Raw | ConvertFrom-Json
    if ($material.properties.MainTexture -ne '81d7ad0d-bae0-46c0-9d15-cd91d70d4001')
    {
        throw 'Earth material does not reference the new texture.'
    }
    $rejected = $false
    try
    {
        & (Join-Path $PSScriptRoot 'Author-EarthPoleCorrection.ps1') -Project $fixture
    }
    catch
    {
        $rejected = $_.Exception.Message -like '*already exists*'
    }
    if (-not $rejected)
    {
        throw 'Repeated authoring must not overwrite the corrected asset.'
    }
    Write-Output 'PASS source preservation, corrected polar pixels, unaffected image samples, material reference and overwrite guard.'
}
finally
{
    $resolved = [IO.Path]::GetFullPath($fixture)
    $prefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'KeirePoleCorrection-*')
    {
        throw 'Refusing to remove a fixture outside the temporary directory.'
    }
    if (Test-Path -LiteralPath $resolved)
    {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

