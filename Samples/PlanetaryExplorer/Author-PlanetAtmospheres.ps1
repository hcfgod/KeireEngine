[CmdletBinding()]
param(
    [string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer',
    [ValidateSet('Airless', 'Thin')][string]$MarsMode = 'Airless',
    [ValidateRange(0.0, 1.0)][double]$MarsDensityScale = 0.02
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path -LiteralPath $Project).Path
$settingsPath = Join-Path $root 'ProjectSettings/Planets.keiresettings'
$backupPath = Join-Path $root 'ProjectSettings/Planets.before-atmosphere-profiles.keiresettings.backup'
$manifest = Get-Content -LiteralPath (Join-Path $root 'ProjectSettings/Project.keireproject') -Raw | ConvertFrom-Json
if ($manifest.id -ne 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30')
{
    throw 'This authoring step is restricted to the separate Planetary Explorer sample.'
}
if (Test-Path -LiteralPath $backupPath)
{
    throw 'An atmosphere-authoring backup already exists; refusing to overwrite it or reapply profiles.'
}

function Assert-NumberRange
{
    param($Value, [double]$Minimum, [double]$Maximum, [string]$Name)

    if ($null -eq $Value -or $Value -is [string] -or $Value -is [bool] -or $Value -is [Array])
    {
        throw "$Name must be a JSON number."
    }
    $number = [double]$Value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt $Minimum -or $number -gt $Maximum)
    {
        throw "$Name must be finite and within $Minimum through $Maximum."
    }
}

function Assert-Atmosphere
{
    param($Profile, [double]$PhysicalRadius)

    $expected = @('schemaVersion', 'enabled', 'planetCenter', 'planetRadius', 'height', 'rayleighScaleHeight',
        'mieScaleHeight', 'rayleighScattering', 'mieScattering', 'mieExtinction', 'mieAnisotropy',
        'absorptionExtinction', 'absorptionPeakAltitude', 'absorptionHalfWidth', 'solarIrradiance',
        'sunAngularRadius', 'viewSamples', 'lightSamples', 'groundReflectance', 'groundIntensity')
    $actual = @($Profile.PSObject.Properties.Name)
    if (@(Compare-Object $expected $actual).Count -ne 0 -or $Profile.schemaVersion -ne 3 -or $Profile.enabled -isnot [bool])
    {
        throw 'Expected a complete atmosphere schema 3 record.'
    }
    Assert-NumberRange $PhysicalRadius 1000.0 1000000000.0 'Physical planet radius'
    Assert-NumberRange $Profile.planetRadius 1000.0 1000000000.0 'Legacy atmosphere radius'
    Assert-NumberRange $Profile.height 100.0 $PhysicalRadius 'Atmosphere thickness'
    Assert-NumberRange $Profile.rayleighScaleHeight 1.0 $Profile.height 'Rayleigh scale height'
    Assert-NumberRange $Profile.mieScaleHeight 1.0 $Profile.height 'Mie scale height'
    Assert-NumberRange $Profile.mieScattering 0.0 0.01 'Mie scattering'
    Assert-NumberRange $Profile.mieExtinction $Profile.mieScattering 0.01 'Mie extinction'
    Assert-NumberRange $Profile.mieAnisotropy -0.95 0.95 'Mie anisotropy'
    Assert-NumberRange $Profile.absorptionPeakAltitude 0.0 $Profile.height 'Absorption peak altitude'
    Assert-NumberRange $Profile.absorptionHalfWidth 1.0 $Profile.height 'Absorption half width'
    Assert-NumberRange $Profile.sunAngularRadius 0.00001 0.05 'Sun angular radius'
    Assert-NumberRange $Profile.groundIntensity 0.0 100.0 'Ground intensity'
    Assert-NumberRange $Profile.viewSamples 4 64 'View samples'
    Assert-NumberRange $Profile.lightSamples 2 32 'Light samples'
    if ($Profile.viewSamples -ne [Math]::Truncate($Profile.viewSamples) -or
        $Profile.lightSamples -ne [Math]::Truncate($Profile.lightSamples))
    {
        throw 'Atmosphere sample counts must be integers.'
    }
    foreach ($field in @('planetCenter', 'rayleighScattering', 'absorptionExtinction', 'solarIrradiance', 'groundReflectance'))
    {
        if ($Profile.$field -isnot [Array] -or $Profile.$field.Count -ne 3)
        {
            throw "$field must contain three numbers."
        }
        $minimum = 0.0
        $maximum = 0.01
        if ($field -eq 'planetCenter')
        {
            $minimum = -1000000000000000.0
            $maximum = 1000000000000000.0
        }
        elseif ($field -eq 'solarIrradiance')
        {
            $maximum = 100000.0
        }
        elseif ($field -eq 'groundReflectance')
        {
            $maximum = 1.0
        }
        foreach ($channel in $Profile.$field)
        {
            Assert-NumberRange $channel $minimum $maximum $field
        }
    }
}

$originalHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash
$settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if ($settings.schemaVersion -notin @(1, 2, 3))
{
    throw 'Unsupported planet settings schema; no files changed.'
}
$expectedFields = @('schemaVersion', 'geometry', 'gravity', 'selectedPlanet', 'planets')
if ($settings.schemaVersion -ge 2)
{
    $expectedFields += 'selection'
}
if ($settings.schemaVersion -eq 3)
{
    $expectedFields += 'atmosphereOverrides'
}
if (@(Compare-Object $expectedFields @($settings.PSObject.Properties.Name)).Count -ne 0)
{
    throw 'Unexpected planet settings fields; refusing a lossy migration.'
}
if ($settings.schemaVersion -eq 3 -and
    ($settings.atmosphereOverrides -isnot [Array] -or $settings.atmosphereOverrides.Count -ne 0))
{
    throw 'Existing atmosphere overrides will not be overwritten.'
}
$earth = @($settings.planets | Where-Object { $_.frame -eq 2 -and $_.planet -eq '8546d07d-0afd-48ac-8082-9a91bccbfb28' })
$mars = @($settings.planets | Where-Object { $_.frame -eq 3 -and $_.planet -eq '8238b398-bb92-49fe-bbcf-8f0e81c060a5' })
if (@($settings.planets).Count -ne 2 -or $earth.Count -ne 1 -or $mars.Count -ne 1)
{
    throw 'Expected the authored Earth and Mars identities/frames; no files changed.'
}
$rendering = Get-Content -LiteralPath (Join-Path $root 'ProjectSettings/Rendering.keiresettings') -Raw | ConvertFrom-Json
$earthProfile = $rendering.atmosphere | ConvertTo-Json -Depth 20 | ConvertFrom-Json
Assert-Atmosphere $earthProfile $earth[0].radiusMetres
$marsProfile = $earthProfile | ConvertTo-Json -Depth 20 | ConvertFrom-Json
$marsProfile.enabled = $MarsMode -eq 'Thin'
$marsProfile.groundReflectance = @(0.35, 0.16, 0.08)
# This is an illustrative thin-air option, not a calibrated Mars atmosphere or independent weather profile.
$marsProfile.height = [Math]::Min(80000.0, $mars[0].radiusMetres)
$marsProfile.rayleighScaleHeight = [Math]::Min(11000.0, $marsProfile.height)
$marsProfile.mieScaleHeight = [Math]::Min(5000.0, $marsProfile.height)
$marsProfile.absorptionPeakAltitude = [Math]::Min($marsProfile.absorptionPeakAltitude, $marsProfile.height)
$marsProfile.absorptionHalfWidth = [Math]::Min($marsProfile.absorptionHalfWidth, $marsProfile.height)
$marsProfile.rayleighScattering = @($earthProfile.rayleighScattering | ForEach-Object { $_ * $MarsDensityScale })
$marsProfile.mieScattering = $earthProfile.mieScattering * $MarsDensityScale
$marsProfile.mieExtinction = $earthProfile.mieExtinction * $MarsDensityScale
$marsProfile.absorptionExtinction = @(0.0, 0.0, 0.0)
Assert-Atmosphere $marsProfile $mars[0].radiusMetres

if ($settings.schemaVersion -eq 1)
{
    $settings | Add-Member -NotePropertyName selection -NotePropertyValue 'manual'
}
$settings.schemaVersion = 3
$profiles = @(
    @{ planet = $earth[0].planet; atmosphere = $earthProfile },
    @{ planet = $mars[0].planet; atmosphere = $marsProfile }
)
$settings | Add-Member -NotePropertyName atmosphereOverrides -NotePropertyValue $profiles -Force
$temporaryPath = Join-Path (Split-Path -Parent $settingsPath) ('Planets.atmospheres-' + [Guid]::NewGuid().ToString('N') + '.tmp')
try
{
    [IO.File]::WriteAllText($temporaryPath, ($settings | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
    if ((Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -ne $originalHash)
    {
        throw 'Planet settings changed during preparation; refusing to replace concurrent edits.'
    }
    # Copy without overwrite so even a concurrent authoring attempt cannot replace the recovery file.
    [IO.File]::Copy($settingsPath, $backupPath, $false)
    [IO.File]::Replace($temporaryPath, $settingsPath, [NullString]::Value)
}
finally
{
    if (Test-Path -LiteralPath $temporaryPath)
    {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Write-Output "Authored explicit Earth and $MarsMode Mars sky profiles; backup: $backupPath"
Write-Output 'Global rendering, clouds/fog, planet selection, scenes and geometry are unchanged. Requires the schema-3/profile-capable editor; visual acceptance is still required.'
