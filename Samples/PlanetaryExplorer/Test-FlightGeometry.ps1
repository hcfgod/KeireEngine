$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'Source/PlanetaryFlightGeometry.cs')
$earth = [PlanetaryExplorer.PlanetaryFlightGeometry]::Nearest(0, 12, 0)
$mars = [PlanetaryExplorer.PlanetaryFlightGeometry]::Nearest(40000000, 12, 0)
$side = [PlanetaryExplorer.PlanetaryFlightGeometry]::Nearest(43389512, -3389500, 0)
$center = [PlanetaryExplorer.PlanetaryFlightGeometry]::Nearest(40000000, -3389500, 0)
if ($earth.Name -ne 'Earth' -or $mars.Name -ne 'Mars' -or $mars.Altitude -ne 12 -or
    $side.UpX -ne 1 -or $center.UpY -ne 1)
{
    throw 'Planet selection, altitude, radial-up or center fallback failed.'
}
try
{
    [void][PlanetaryExplorer.PlanetaryFlightGeometry]::Nearest([double]::NaN, 0, 0)
    throw 'NaN accepted'
}
catch
{
    if ($_.Exception.Message -eq 'NaN accepted')
    {
        throw
    }
}
Write-Output 'Actual standalone geometry passed Earth/Mars selection, radial-up, altitude, center guard and NaN rejection.'
