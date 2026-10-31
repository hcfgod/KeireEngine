[CmdletBinding()]
param(
    [string]$AssetTool = (Join-Path $PSScriptRoot '../../Build/Distributions/keire-editor-windows-x86_64-DevDist/bin/KeireAssetTool.exe')
)

$ErrorActionPreference = 'Stop'
$toolPath = (Resolve-Path -LiteralPath $AssetTool).Path
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $temporaryRoot ('KeireHudImport-' + [Guid]::NewGuid().ToString('N'))
try
{
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'Assets/PlanetaryExplorer'),
        (Join-Path $fixtureRoot 'ProjectSettings') -Force | Out-Null
    @{
        schemaVersion = 4
        id = [Guid]::NewGuid().ToString('D')
        name = 'Flight HUD import fixture'
        createdWithEngineVersion = '0.3.0'
        minimumEngineVersion = '0.3.0'
        createdAt = '2026-01-01T00:00:00Z'
        lastSavedWithEngineVersion = '0.3.0'
        requiredModules = @()
    } | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $fixtureRoot 'ProjectSettings/Project.keireproject') -Encoding UTF8
    & (Join-Path $PSScriptRoot 'Update-FlightHud.ps1') -Project $fixtureRoot
    $output = & $toolPath import --project $fixtureRoot --worker-timeout-seconds 30 2>&1
    $result = $LASTEXITCODE
    $output | Write-Output
    if ($result -ne 0)
    {
        throw "Flight HUD production import failed with exit code $result."
    }
    if (($output -join "`n") -notmatch 'Imported 2 assets')
    {
        throw 'Expected a fresh production import of both the visual tree and private stylesheet.'
    }
    $stylePath = Join-Path $fixtureRoot 'Assets/PlanetaryExplorer/FlightHud.keirestyle'
    $styleSource = [IO.File]::ReadAllText($stylePath)
    [IO.File]::WriteAllText($stylePath, $styleSource.Replace('@keire-style 2;', '@keire-style 1;'))
    $invalidOutput = & $toolPath import --project $fixtureRoot --worker-timeout-seconds 30 2>&1
    $invalidResult = $LASTEXITCODE
    if ($invalidResult -eq 0 -or ($invalidOutput -join "`n") -notmatch 'requires schema v2')
    {
        throw 'The schema-v1 regression fixture must reject the advanced text-wrapping properties.'
    }
    Write-Output 'PASS actual flight HUD visual-tree and stylesheet parsers/importers.'
}
finally
{
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixtureRoot) -notlike 'KeireHudImport-*')
    {
        throw 'Refusing to remove a fixture outside the designated temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedFixtureRoot)
    {
        Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
    }
}
