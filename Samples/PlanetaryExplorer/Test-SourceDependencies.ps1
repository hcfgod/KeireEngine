[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$validator = Join-Path $PSScriptRoot 'Validate-SourceDependencies.ps1'
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireSourceValidation-' + [Guid]::NewGuid().ToString('N'))
$sceneId = '11111111-2222-4333-8444-555555555555'
$materialId = 'aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee'
$typeId = '12345678-1234-4234-8234-123456789abc'
$passed = 0

function Write-FixtureAsset
{
    param([string]$Root, [string]$Name, [string]$Identifier, [string]$Type = $typeId)

    $sourcePath = Join-Path $Root "Assets/$Name"
    [IO.File]::WriteAllText($sourcePath, '{}')
    $metadata = @{ id = $Identifier; type = $Type; dependencies = @() }
    $metadata | ConvertTo-Json | Set-Content -LiteralPath "$sourcePath.keiremeta"
}

function Test-Fixture
{
    param([string]$Name, [scriptblock]$Arrange, [string]$ExpectedError = '')

    $caseRoot = Join-Path $fixtureRoot $Name
    $source = Join-Path $caseRoot 'Source'
    $project = Join-Path $caseRoot 'Project'
    New-Item -ItemType Directory -Path (Join-Path $source 'Assets'),
        (Join-Path $project 'Assets'), (Join-Path $project 'ProjectSettings') -Force | Out-Null
    Write-FixtureAsset $source 'Scene.keirescene' $sceneId
    Write-FixtureAsset $source 'Material.keirematerial' $materialId
    Write-FixtureAsset $project 'Scene.keirescene' $sceneId
    @{ startupScene = $sceneId } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $project 'ProjectSettings/Project.keireproject')
    & $Arrange $project

    $failure = ''
    try
    {
        & $validator -Project $project -SourceProject $source | Out-Null
    }
    catch
    {
        $failure = $_.Exception.Message
    }

    if ($ExpectedError)
    {
        if ($failure -notlike "*$ExpectedError*")
        {
            throw "$Name expected '$ExpectedError', received '$failure'."
        }
    }
    elseif ($failure)
    {
        throw "$Name failed: $failure"
    }

    $script:passed++
    Write-Output "PASS $Name"
}

try
{
    Test-Fixture 'Valid' { param($project) }
    Test-Fixture 'CompactStartupAndAsset' {
        param($project)
        Write-FixtureAsset $project 'Scene.keirescene' ([Guid]::Parse($sceneId).ToString('N').ToUpperInvariant())
    }
    Test-Fixture 'MalformedAssetId' {
        param($project)
        Write-FixtureAsset $project 'Scene.keirescene' 'f8e65aac-fcf0-4c36-acfa-a5e91a5b9e2'
    } 'Malformed asset identifier'
    Test-Fixture 'MalformedType' {
        param($project)
        Write-FixtureAsset $project 'Scene.keirescene' $sceneId 'invalid'
    } 'Malformed asset identifier'
    Test-Fixture 'CanonicalCompactDuplicate' {
        param($project)
        Write-FixtureAsset $project 'Duplicate.keirescene' ([Guid]::Parse($sceneId).ToString('N'))
    } 'Duplicate asset identifier'
    Test-Fixture 'MissingStartup' {
        param($project)
        @{ startupScene = '99999999-2222-4333-8444-555555555555' } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $project 'ProjectSettings/Project.keireproject')
    } 'Startup scene is absent'
    Test-Fixture 'MissingCompactReference' {
        param($project)
        @{ material = [Guid]::Parse($materialId).ToString('N') } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $project 'Assets/Scene.keirescene')
    } 'Missing known source asset'
    Test-Fixture 'PresentReference' {
        param($project)
        Write-FixtureAsset $project 'Material.keirematerial' $materialId
        @{ material = $materialId } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $project 'Assets/Scene.keirescene')
    }
    Write-Output "$passed source dependency regression cases passed."
}
finally
{
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'KeireSourceValidation-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot)
    {
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
