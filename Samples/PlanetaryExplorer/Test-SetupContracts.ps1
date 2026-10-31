[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireSetupValidation-' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $fixtureRoot 'Source'
$destination = Join-Path $fixtureRoot 'Destination'
$nasa = Join-Path $fixtureRoot 'Nasa'

function Write-FixtureJson
{
    param([string]$Path, $Value)

    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Assert-Contract
{
    param([bool]$Condition, [string]$Message)

    if (-not $Condition)
    {
        throw $Message
    }
}

try
{
    foreach ($folder in @('Assets', 'Assets/SpaceFlight', 'Assets/FirstPersonController', 'Assets/Input',
        'Assets/Keire', 'Assets/Shaders', 'Assets/UI', 'ProjectSettings'))
    {
        New-Item -ItemType Directory -Path (Join-Path $source $folder) -Force | Out-Null
    }
    New-Item -ItemType Directory -Path $nasa -Force | Out-Null
    foreach ($name in @('Earth-BlueMarble-January-5400.jpg', 'Mars.jpg', 'SOURCES.md'))
    {
        # These fixtures test source copying only; no importer reads the image placeholders.
        Set-Content -LiteralPath (Join-Path $nasa $name) -Value 'isolated source fixture'
    }
    foreach ($name in @('BuildProfiles', 'BuildScenes', 'EnvironmentLighting', 'EnvironmentTime',
        'Planets', 'Rendering', 'Scripting'))
    {
        Write-FixtureJson (Join-Path $source "ProjectSettings/$name.keiresettings") @{}
    }
    Write-FixtureJson (Join-Path $source 'ProjectSettings/SharedShaders.lock') @{}
    Write-FixtureJson (Join-Path $source 'ProjectSettings/Project.keireproject') @{
        id = '11111111-2222-4333-8444-555555555555'; name = 'Source'; startupScene = ''
    }
    Write-FixtureJson (Join-Path $source 'ProjectSettings/Player.keiresettings') @{
        applicationIdentifier = 'source'; productName = 'Source'; windowTitle = 'Source'
    }
    Write-FixtureJson (Join-Path $source 'ProjectSettings/Authoring.keiresettings') @{
        externalEditorId = 'custom'; externalEditorExecutable = 'X:/SourceOnly/Editor.exe'
    }
    $scene = @{
        name = 'SourceScene'
        entities = @(@{
            name = 'Flight Camera'
            components = @(
                @{ type = 'eb170000-2026-4000-8000-000000000001'; data = @{} },
                @{ type = '4b454952-4543-414d-4552-410000000001'; data = @{ farPlane = 1000.0 } },
                @{ type = '4b454952-4555-4944-4f43-554d454e5401'; data = @{ visualTree = '' } }
            )
        })
    }
    Write-FixtureJson (Join-Path $source 'Assets/GravityAcceptanceQA.keirescene') $scene
    Write-FixtureJson (Join-Path $source 'Assets/GravityAcceptanceQA.keirescene.keiremeta') @{
        id = '22222222-2222-4333-8444-555555555555'; dependencies = @()
    }
    $ui = '<ui><VisualElement class="menu-card"><Label class="title"/><Label name="status"/><Button name="launch"/><Button name="return"/></VisualElement></ui>'
    Set-Content -LiteralPath (Join-Path $source 'Assets/UI/SandboxMenu.keireui') -Value $ui
    Write-FixtureJson (Join-Path $source 'Assets/UI/SandboxMenu.keireui.keiremeta') @{
        id = '33333333-2222-4333-8444-555555555555'; dependencies = @()
    }

    & (Join-Path $PSScriptRoot 'Bootstrap.ps1') -SourceProject $source -Destination $destination -NasaSources $nasa
    $authoring = Get-Content -LiteralPath (Join-Path $destination 'ProjectSettings/Authoring.keiresettings') -Raw |
        ConvertFrom-Json
    Assert-Contract ($authoring.externalEditorId -eq 'system') 'Bootstrap must select a valid system editor.'
    Assert-Contract ($authoring.externalEditorExecutable -eq '') 'Bootstrap retained a machine-specific executable.'
    $originalAuthoring = Get-Content -LiteralPath (Join-Path $source 'ProjectSettings/Authoring.keiresettings') -Raw |
        ConvertFrom-Json
    Assert-Contract ($originalAuthoring.externalEditorId -eq 'custom') 'Bootstrap modified source settings.'

    # Surface authoring is outside this contract: supply its scene output to the actual installer.
    foreach ($suffix in @('', '.keiremeta'))
    {
        Copy-Item -LiteralPath (Join-Path $destination "Assets/PlanetaryExplorerBaseline.keirescene$suffix") `
            -Destination (Join-Path $destination "Assets/PlanetaryExplorer.keirescene$suffix")
    }
    & (Join-Path $PSScriptRoot 'Install-ManualFlight.ps1') -Project $destination
    $installedScene = Get-Content -LiteralPath (Join-Path $destination 'Assets/PlanetaryExplorer.keirescene') -Raw |
        ConvertFrom-Json
    $camera = $installedScene.entities[0].components |
        Where-Object { $_.type -eq '4b454952-4543-414d-4552-410000000001' }
    Assert-Contract ($camera.data.farPlane -gt 0 -and $camera.data.farPlane -le 10000000.0) `
        'Manual flight camera exceeds the supported far-plane range.'
    $hudPath = Join-Path $destination 'Assets/PlanetaryExplorer/FlightHud.keireui'
    [xml]$hud = Get-Content -LiteralPath $hudPath -Raw
    $hudMetadata = Get-Content -LiteralPath "$hudPath.keiremeta" -Raw | ConvertFrom-Json
    $styleMetadata = Get-Content -LiteralPath (Join-Path $destination 'Assets/PlanetaryExplorer/FlightHud.keirestyle.keiremeta') -Raw |
        ConvertFrom-Json
    Assert-Contract ($hud.ui.style.src -eq $styleMetadata.id) 'HUD must reference its private stylesheet.'
    Assert-Contract ($hudMetadata.dependencies -contains $styleMetadata.id) 'HUD metadata omitted its stylesheet dependency.'
    foreach ($name in @('status', 'launch', 'view', 'pause', 'return'))
    {
        Assert-Contract ($null -ne $hud.SelectSingleNode("//*[@name='$name']")) "HUD is missing controller target $name."
    }
    $baselineUiPath = Join-Path $destination 'Assets/UI/SandboxMenu.keireui'
    $installedScenePath = Join-Path $destination 'Assets/PlanetaryExplorer.keirescene'
    $baselineHash = (Get-FileHash -LiteralPath $baselineUiPath -Algorithm SHA256).Hash
    $sceneHash = (Get-FileHash -LiteralPath $installedScenePath -Algorithm SHA256).Hash
    & (Join-Path $PSScriptRoot 'Update-FlightHud.ps1') -Project $destination
    Assert-Contract ((Get-FileHash -LiteralPath $baselineUiPath -Algorithm SHA256).Hash -eq $baselineHash) `
        'HUD refresh changed the baseline UI.'
    Assert-Contract ((Get-FileHash -LiteralPath $installedScenePath -Algorithm SHA256).Hash -eq $sceneHash) `
        'HUD refresh changed the authored scene.'
    Write-Output 'PASS bootstrap settings, source preservation, camera range, HUD dependencies and isolated refresh.'
}
finally
{
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'KeireSetupValidation-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot)
    {
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
