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
$previousNear = 0.0
foreach ($altitude in @(-10.0, 12.0, 100.0, 1000.0, 2512.0, 5012.0, 150000.0))
{
    $near = [PlanetaryExplorer.PlanetaryFlightGeometry]::CameraNearPlane($altitude)
    if ($near -lt 0.19999 -or $near -gt 10.0 -or $near -lt $previousNear)
    {
        throw 'Camera near plane must remain bounded and increase monotonically with altitude.'
    }
    $previousNear = $near
}
if ([Math]::Abs([PlanetaryExplorer.PlanetaryFlightGeometry]::CameraNearPlane(2512.0) - 5.1) -gt 0.00001 -or
    [PlanetaryExplorer.PlanetaryFlightGeometry]::CameraNearPlane(150000.0) -ne 10.0)
{
    throw 'Camera depth precision transition or orbital near plane failed.'
}
Write-Output 'Geometry passed planet selection, radial-up, altitude, center/NaN guards and altitude-aware camera depth bounds.'
