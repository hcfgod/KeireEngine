[CmdletBinding()]
param([string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path -LiteralPath $Project).Path
$settingsPath = Join-Path $root 'ProjectSettings/Rendering.keiresettings'
$backupPath = Join-Path $root 'ProjectSettings/Rendering.before-cloud-variety.keiresettings.backup'
$manifest = Get-Content -LiteralPath (Join-Path $root 'ProjectSettings/Project.keireproject') -Raw | ConvertFrom-Json
if ($manifest.id -ne 'd713f0a3-bcd2-4d04-9a9d-42f6fd724c30')
{
    throw 'This shape authoring step is restricted to the separate Planetary Explorer sample.'
}
if (Test-Path -LiteralPath $backupPath)
{
    throw 'A cloud-variety backup already exists; refusing to overwrite it or reapply this starting point.'
}

$originalHash = (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash
$settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
if ($settings.clouds.version -ne 3 -or $settings.clouds.volume.version -notin @(9, 10))
{
    throw 'Expected cloud settings version 3 with volume version 9 or 10; no migration will be guessed.'
}
$volume = $settings.clouds.volume
$minimumHeight = $volume.PSObject.Properties['minimumTypeThickness']
if ($volume.version -eq 10)
{
    if ($null -eq $minimumHeight -or $minimumHeight.Value -is [string] -or
        $minimumHeight.Value -is [bool] -or $null -eq $minimumHeight.Value -or
        -not [double]::IsFinite([double]$minimumHeight.Value) -or
        [double]$minimumHeight.Value -lt 0.01 -or [double]$minimumHeight.Value -gt 1.0)
    {
        throw 'Volume version 10 requires a finite numeric minimumTypeThickness from 0.01 to 1.0.'
    }
}
elseif ($null -ne $minimumHeight)
{
    throw 'minimumTypeThickness requires volume version 10; no migration will be guessed.'
}
if ($volume.enabled -isnot [bool] -or -not $volume.enabled)
{
    throw 'Enable and validate the existing volume before applying this sample shape comparison.'
}
if ($volume.viewSteps -ne 48 -or $volume.shadowSteps -ne 6 -or $volume.ambientOcclusionStrength -ne 0.0)
{
    throw 'This comparison requires the existing 48 view / 6 shadow samples and zero ambient occlusion. Quality settings will not be overwritten.'
}

# Change only shape controls. Extinction, lighting, wind, seed, quality, layered clouds and other systems stay authored.
$shape = [ordered]@{
    baseHeightMeters = 1500.0
    baseHeightVariationMeters = 800.0
    thicknessMeters = 6000.0
    patternSizeMeters = 2500.0
    coverage = 0.5
    regionalCoverageStrength = 0.7
    independentWeather = $true
    cloudType = 0.55
    typeVariation = 1.0
    detailFrequency = 2
    detailAmount = 0.6
    erosionAmount = 0.25
    heightProfile = @(0.0, 0.08, 0.4, 1.0)
}
foreach ($name in $shape.Keys)
{
    if ($null -eq $volume.PSObject.Properties[$name])
    {
        throw "Missing cloud shape field: $name"
    }
    $volume.$name = $shape[$name]
}

$temporaryPath = Join-Path (Split-Path -Parent $settingsPath) ('Rendering.cloud-variety-' + [Guid]::NewGuid().ToString('N') + '.tmp')
try
{
    [IO.File]::WriteAllText($temporaryPath, ($settings | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
    if ((Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash -ne $originalHash)
    {
        throw 'Rendering settings changed during preparation; refusing to replace concurrent edits.'
    }
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
Write-Output "Applied illustrative tall/shallow cloud shape comparison; backup: $backupPath"
Write-Output 'Quality remains 48 view / 6 shadow samples, ambient occlusion zero. Extinction is unchanged. Visual variety and frame-time acceptance are pending.'
