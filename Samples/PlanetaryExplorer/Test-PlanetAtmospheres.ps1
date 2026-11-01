[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireAtmosphereAuthoring-' + [Guid]::NewGuid().ToString('N'))
$author = Join-Path $PSScriptRoot 'Author-PlanetAtmospheres.ps1'

function Assert-Contract
{
    param([bool]$Condition, [string]$Message)

    if (-not $Condition)
    {
        throw $Message
    }
}

function Write-Json
{
    param([string]$Path, $Value)

    $Value | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function New-Fixture
{
    param([string]$Name, [int]$Schema = 1)

    $root = Join-Path $fixtureRoot $Name
    $settingsFolder = Join-Path $root 'ProjectSettings'
    New-Item -ItemType Directory -Path $settingsFolder -Force | Out-Null
    Write-Json (Join-Path $settingsFolder 'Project.keireproject') @{ id = 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30' }
    $profile = @{
        schemaVersion = 3
        enabled = $true
        planetCenter = @(123.0, -6360000.0, 789.0)
        planetRadius = 6360000.0
        height = 100000.0
        rayleighScaleHeight = 8000.0
        mieScaleHeight = 1200.0
        rayleighScattering = @(0.000005802, 0.000013558, 0.0000331)
        mieScattering = 0.000003996
        mieExtinction = 0.00000444
        mieAnisotropy = 0.8
        absorptionExtinction = @(0.00000065, 0.000001881, 0.000000085)
        absorptionPeakAltitude = 25000.0
        absorptionHalfWidth = 15000.0
        solarIrradiance = @(20.0, 20.0, 20.0)
        sunAngularRadius = 0.004675
        viewSamples = 24
        lightSamples = 8
        groundReflectance = @(0.18, 0.18, 0.18)
        groundIntensity = 0.5
    }
    Write-Json (Join-Path $settingsFolder 'Rendering.keiresettings') @{
        atmosphere = $profile
        clouds = @{ preserved = 'global cloud fixture' }
        fog = @{ preserved = 'global fog fixture' }
    }
    $settings = @{
        schemaVersion = $Schema
        geometry = 'planetary'
        gravity = 'uniform'
        selectedPlanet = '8546d07d-0afd-48ac-8082-9a91bccbfb28'
        planets = @(
            @{ planet = '8546d07d-0afd-48ac-8082-9a91bccbfb28'; frame = 2; radiusMetres = 6360000.0 },
            @{ planet = '8238b398-bb92-49fe-bbcf-8f0e81c060a5'; frame = 3; radiusMetres = 3389500.0 }
        )
    }
    if ($Schema -ge 2)
    {
        $settings.selection = 'nearest-surface'
    }
    if ($Schema -ge 3)
    {
        $settings.atmosphereOverrides = @()
    }
    if ($Schema -eq 4)
    {
        # Sentinel payloads test opaque preservation, not the engine weather codecs.
        $settings.cloudOverrides = @(@{
            planet = '8238b398-bb92-49fe-bbcf-8f0e81c060a5'
            clouds = @{ enabled = $true; volume = @{ enabled = $false; authoredValue = 0.08 } }
        })
        $settings.fogOverrides = @(@{
            planet = '8546d07d-0afd-48ac-8082-9a91bccbfb28'
            fog = @{ enabled = $false; volumes = @(@{ authoredValue = 42 }) }
        })
    }
    Write-Json (Join-Path $settingsFolder 'Planets.keiresettings') $settings
    return $root
}

function Assert-RejectedUnchanged
{
    param([string]$Project)

    $path = Join-Path $Project 'ProjectSettings/Planets.keiresettings'
    $before = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $rejected = $false
    try
    {
        & $author -Project $Project | Out-Null
    }
    catch
    {
        $rejected = $true
    }
    Assert-Contract $rejected 'Expected authoring to reject this fixture.'
    Assert-Contract ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $before) 'Rejected authoring changed planet settings.'
}

try
{
    foreach ($schema in @(1, 2, 3, 4))
    {
        $project = New-Fixture "Airless-$schema" $schema
        $path = Join-Path $project 'ProjectSettings/Planets.keiresettings'
        $renderingPath = Join-Path $project 'ProjectSettings/Rendering.keiresettings'
        $backup = Join-Path $project 'ProjectSettings/Planets.before-atmosphere-profiles.keiresettings.backup'
        $beforeHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $renderingHash = (Get-FileHash -LiteralPath $renderingPath -Algorithm SHA256).Hash
        $before = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        $global = (Get-Content -LiteralPath $renderingPath -Raw | ConvertFrom-Json).atmosphere
        & $author -Project $project | Out-Null
        $after = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        Assert-Contract ($after.schemaVersion -eq 4 -and @($after.PSObject.Properties).Count -eq 9) 'Output must be exactly planet schema 4.'
        foreach ($collection in @('cloudOverrides', 'fogOverrides'))
        {
            Assert-Contract ($after.$collection -is [Array]) 'Weather profiles must remain arrays.'
            if ($schema -eq 4)
            {
                Assert-Contract (($after.$collection | ConvertTo-Json -Depth 40 -Compress) -eq
                    ($before.$collection | ConvertTo-Json -Depth 40 -Compress)) 'Authored weather profile changed.'
            }
            else
            {
                Assert-Contract ($after.$collection.Count -eq 0) 'Legacy projects must inherit global weather.'
            }
        }
        Assert-Contract ($after.atmosphereOverrides.Count -eq 2) 'Expected two explicit profiles.'
        Assert-Contract (($after.atmosphereOverrides[0].atmosphere | ConvertTo-Json -Depth 20 -Compress) -eq
            ($global | ConvertTo-Json -Depth 20 -Compress)) 'Earth profile was not preserved exactly.'
        Assert-Contract (-not $after.atmosphereOverrides[1].atmosphere.enabled) 'Default Mars must be airless.'
        Assert-Contract ($after.atmosphereOverrides[1].planet -eq '8238b398-bb92-49fe-bbcf-8f0e81c060a5') 'Mars identity changed.'
        Assert-Contract ($after.selectedPlanet -eq $before.selectedPlanet) 'Selected planet changed.'
        Assert-Contract (($after.planets | ConvertTo-Json -Depth 20 -Compress) -eq
            ($before.planets | ConvertTo-Json -Depth 20 -Compress)) 'Planet geometry changed.'
        $expectedSelection = if ($schema -eq 1) { 'manual' } else { 'nearest-surface' }
        Assert-Contract ($after.selection -eq $expectedSelection) 'Selection policy was lost.'
        Assert-Contract ((Get-FileHash -LiteralPath $renderingPath -Algorithm SHA256).Hash -eq $renderingHash) 'Global rendering changed.'
        Assert-Contract ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -eq $beforeHash) 'Backup does not exactly match original bytes.'
        Assert-RejectedUnchanged $project
        Assert-Contract ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -eq $beforeHash) 'Repeated authoring overwrote the backup.'
    }

    $thin = New-Fixture 'Thin'
    & $author -Project $thin -MarsMode Thin -MarsDensityScale 0.1 | Out-Null
    $thinSettings = Get-Content -LiteralPath (Join-Path $thin 'ProjectSettings/Planets.keiresettings') -Raw | ConvertFrom-Json
    $thinMars = $thinSettings.atmosphereOverrides[1].atmosphere
    Assert-Contract $thinMars.enabled 'Thin mode must enable the optical medium.'
    Assert-Contract ([Math]::Abs($thinMars.mieExtinction - 0.000000444) -lt 0.000000000001) 'Thin density scaling failed.'
    Assert-Contract ($thinMars.planetRadius -eq 6360000.0) 'Hidden compatibility radius was rewritten.'

    $existing = New-Fixture 'Existing' 3
    $existingPath = Join-Path $existing 'ProjectSettings/Planets.keiresettings'
    $record = Get-Content -LiteralPath $existingPath -Raw | ConvertFrom-Json
    $record.atmosphereOverrides = @(@{ planet = $record.selectedPlanet; atmosphere = @{} })
    Write-Json $existingPath $record
    Assert-RejectedUnchanged $existing

    foreach ($invalidKind in @('Duplicate', 'Foreign', 'NotArray', 'ExtraField'))
    {
        $weather = New-Fixture "Weather-$invalidKind" 4
        $weatherPath = Join-Path $weather 'ProjectSettings/Planets.keiresettings'
        $record = Get-Content -LiteralPath $weatherPath -Raw | ConvertFrom-Json
        switch ($invalidKind)
        {
            'Duplicate' { $record.cloudOverrides += $record.cloudOverrides[0] }
            'Foreign' { $record.cloudOverrides[0].planet = '00000000-0000-0000-0000-000000000001' }
            'NotArray' { $record.cloudOverrides = $null }
            'ExtraField' { $record.cloudOverrides[0] | Add-Member -NotePropertyName unexpected -NotePropertyValue 1 }
        }
        Write-Json $weatherPath $record
        Assert-RejectedUnchanged $weather
    }

    $invalid = New-Fixture 'Invalid'
    $invalidRendering = Join-Path $invalid 'ProjectSettings/Rendering.keiresettings'
    $record = Get-Content -LiteralPath $invalidRendering -Raw | ConvertFrom-Json
    $record.atmosphere.mieExtinction = -1.0
    Write-Json $invalidRendering $record
    Assert-RejectedUnchanged $invalid
    Assert-Contract (-not (Test-Path -LiteralPath (Join-Path $invalid 'ProjectSettings/Planets.before-atmosphere-profiles.keiresettings.backup'))) 'Invalid source created a backup or started authoring.'
    Write-Output 'PASS atmosphere authoring: schema 1/2/3/4 migration, weather preservation/envelope rejection, explicit sky profiles, thin option, byte-exact backup/global preservation, existing override rejection and invalid input rejection. No engine imports or GPU checks performed.'
}
finally
{
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike 'KeireAtmosphereAuthoring-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixture)
    {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
