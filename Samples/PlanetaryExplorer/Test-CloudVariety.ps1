[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireCloudVariety-' + [Guid]::NewGuid().ToString('N'))
$author = Join-Path $PSScriptRoot 'Author-CloudVariety.ps1'
$shapeFields = @('baseHeightMeters', 'baseHeightVariationMeters', 'thicknessMeters', 'patternSizeMeters',
    'coverage', 'regionalCoverageStrength', 'independentWeather', 'cloudType', 'typeVariation',
    'detailFrequency', 'detailAmount', 'erosionAmount', 'heightProfile')

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

    $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function New-Fixture
{
    param([string]$Name, [int]$ViewSteps = 48)

    $project = Join-Path $fixtureRoot $Name
    $folder = Join-Path $project 'ProjectSettings'
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    Write-Json (Join-Path $folder 'Project.keireproject') @{ id = 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30' }
    Write-Json (Join-Path $folder 'Rendering.keiresettings') @{
        schemaVersion = 123
        atmosphere = @{ unchanged = 'atmosphere sentinel' }
        fog = @{ unchanged = 'fog sentinel' }
        stars = @{ unchanged = 'star sentinel' }
        unrelated = @{ nested = @(1, 2, 3) }
        clouds = @{
            version = 3
            layers = @(@{ enabled = $false; opacity = 0.6 }, @{ enabled = $false; opacity = 0.7 })
            volume = @{
                version = 9
                enabled = $true
                baseHeightMeters = 1700.0
                baseHeightVariationMeters = 250.0
                thicknessMeters = 1200.0
                patternSizeMeters = 3000.0
                coverage = 0.6
                independentWeather = $false
                cloudType = 1.0
                typeVariation = 0.2
                detailAmount = 0.2
                erosionAmount = 0.2
                heightProfile = @(0.0, 0.15, 0.7, 1.0)
                viewSteps = $ViewSteps
                shadowSteps = 6
                ambientOcclusionStrength = 0.0
                extinctionPerMeter = 0.004
                anisotropy = 0.65
                seed = 17
                windMetersPerSecond = @(8.0, 0.0, 3.0)
                scatteringAlbedo = @(0.95, 0.94, 0.93)
                maximumDistanceMeters = 100000.0
                variationPeriod = 64
                variationStrength = 0.75
                detailFrequency = 4
                densityContrast = 1.0
                regionalCoveragePeriod = 2
                regionalCoverageStrength = 1.0
                multipleScatteringContribution = 0.5
                multipleScatteringOcclusion = 0.5
            }
        }
    }
    return $project
}

function Assert-RejectedUnchanged
{
    param([string]$Project)

    $path = Join-Path $Project 'ProjectSettings/Rendering.keiresettings'
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
    Assert-Contract $rejected 'Expected this fixture to be rejected.'
    Assert-Contract ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $before) 'Rejected authoring changed rendering settings.'
}

try
{
    $project = New-Fixture 'Valid'
    $path = Join-Path $project 'ProjectSettings/Rendering.keiresettings'
    $backup = Join-Path $project 'ProjectSettings/Rendering.before-cloud-variety.keiresettings.backup'
    $beforeHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $before = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    & $author -Project $project | Out-Null
    $after = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $volume = $after.clouds.volume
    Assert-Contract ($volume.baseHeightMeters -eq 1500.0 -and $volume.baseHeightVariationMeters -eq 800.0) 'Base placement mismatch.'
    Assert-Contract ($volume.thicknessMeters -eq 6000.0 -and $volume.patternSizeMeters -eq 2500.0) 'Body aspect ratio mismatch.'
    Assert-Contract ($volume.coverage -eq 0.5 -and $volume.regionalCoverageStrength -eq 0.7 -and $volume.independentWeather) 'Coverage/weather mismatch.'
    Assert-Contract ($volume.cloudType -eq 0.55 -and $volume.typeVariation -eq 1.0) 'Regional cloud type mismatch.'
    Assert-Contract ($volume.detailAmount -eq 0.6 -and $volume.detailFrequency -eq 2 -and $volume.erosionAmount -eq 0.25) 'Detail/erosion mismatch.'
    Assert-Contract (($volume.heightProfile -join ',') -eq '0,0.08,0.4,1') 'Vertical profile mismatch.'
    Assert-Contract ($volume.heightProfile[0] -ge 0 -and $volume.heightProfile[-1] -le 1) 'Profile outside normalized range.'
    for ($index = 1; $index -lt 4; ++$index)
    {
        Assert-Contract ($volume.heightProfile[$index] -gt $volume.heightProfile[$index - 1]) 'Profile must remain ordered.'
    }
    Assert-Contract ($volume.thicknessMeters * 0.35 -eq 2100.0) 'Expected shallow envelope differs.'
    $maximumRegionalCoverage = $volume.coverage + (1.0 - $volume.coverage) * $volume.regionalCoverageStrength
    Assert-Contract ($maximumRegionalCoverage -lt 1.0) 'Rich weather regions must retain a nonzero shape threshold.'
    Assert-Contract ($volume.variationPeriod -eq 64 -and $volume.regionalCoveragePeriod -eq 2) 'Warp/weather spatial scales changed.'
    Assert-Contract ($volume.baseHeightMeters - $volume.baseHeightVariationMeters -gt 0) 'Cloud base crosses sea-level in this fixture.'
    Assert-Contract ($volume.viewSteps -eq 48 -and $volume.shadowSteps -eq 6 -and $volume.ambientOcclusionStrength -eq 0.0) 'Quality budget changed.'
    Assert-Contract ($volume.extinctionPerMeter -eq $before.clouds.volume.extinctionPerMeter) 'Density/opacity control changed.'
    foreach ($field in $shapeFields)
    {
        $before.clouds.volume.PSObject.Properties.Remove($field)
        $after.clouds.volume.PSObject.Properties.Remove($field)
    }
    Assert-Contract (($before | ConvertTo-Json -Depth 30 -Compress) -eq ($after | ConvertTo-Json -Depth 30 -Compress)) 'Fields outside the explicit shape set changed.'
    Assert-Contract ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -eq $beforeHash) 'Backup is not byte-exact.'
    Assert-RejectedUnchanged $project
    Assert-Contract ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -eq $beforeHash) 'Repeated authoring replaced the backup.'

    $quality = New-Fixture 'OtherQuality' 64
    Assert-RejectedUnchanged $quality
    Assert-Contract (-not (Test-Path -LiteralPath (Join-Path $quality 'ProjectSettings/Rendering.before-cloud-variety.keiresettings.backup'))) 'Rejected quality fixture created a backup.'
    $foreign = New-Fixture 'Foreign'
    Write-Json (Join-Path $foreign 'ProjectSettings/Project.keireproject') @{ id = '11111111-2222-4333-8444-555555555555' }
    Assert-RejectedUnchanged $foreign
    $unsupported = New-Fixture 'Unsupported'
    $unsupportedPath = Join-Path $unsupported 'ProjectSettings/Rendering.keiresettings'
    $record = Get-Content -LiteralPath $unsupportedPath -Raw | ConvertFrom-Json
    $record.clouds.volume.version = 8
    Write-Json $unsupportedPath $record
    Assert-RejectedUnchanged $unsupported
    foreach ($fraction in @(0.01, 0.08, 0.35, 1.0))
    {
        $current = New-Fixture ('Version10-' + $fraction)
        $currentPath = Join-Path $current 'ProjectSettings/Rendering.keiresettings'
        $record = Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
        $record.clouds.volume.version = 10
        $record.clouds.volume | Add-Member -NotePropertyName minimumTypeThickness -NotePropertyValue $fraction
        Write-Json $currentPath $record
        & $author -Project $current | Out-Null
        $updated = Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
        Assert-Contract ($updated.clouds.volume.version -eq 10) 'Authoring changed volume schema.'
        Assert-Contract ($updated.clouds.volume.minimumTypeThickness -eq $fraction) 'Authoring replaced the custom minimum height.'
        Assert-RejectedUnchanged $current
    }
    $invalidIndex = 0
    foreach ($invalid in @($null, 0.0, 0.009, 1.01, '0.35', $true))
    {
        $current = New-Fixture ('InvalidVersion10-' + $invalidIndex++)
        $currentPath = Join-Path $current 'ProjectSettings/Rendering.keiresettings'
        $record = Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
        $record.clouds.volume.version = 10
        if ($null -ne $invalid)
        {
            $record.clouds.volume | Add-Member -NotePropertyName minimumTypeThickness -NotePropertyValue $invalid
        }
        Write-Json $currentPath $record
        Assert-RejectedUnchanged $current
        Assert-Contract (-not (Test-Path -LiteralPath (Join-Path $current 'ProjectSettings/Rendering.before-cloud-variety.keiresettings.backup'))) 'Invalid volume created a backup.'
    }
    Write-Output 'PASS cloud variety: exact shape values, normalized ranges, untouched quality/density/other systems, byte-exact backup, repeat/foreign/version/quality rejection. Visual and performance acceptance remain pending.'
}
finally
{
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike 'KeireCloudVariety-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixture)
    {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
