param([switch]$Generated)

$ErrorActionPreference = 'Stop'
$windows = Join-Path $PSScriptRoot '..\Windows'
. (Join-Path $windows 'common.ps1')
. (Join-Path $windows 'ffmpeg-runtime-contract.ps1')
. (Join-Path $windows 'editor-development-stage.ps1')

function Assert-DevelopmentCondition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-editor-development-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $project = [pscustomobject]@{ ARTIFACT_PREFIX = 'fixture'; PROJECT_IDENTIFIER = 'Fixture'; PROJECT_VERSION = '1.2.3' }
    $paths = Get-EditorDevelopmentStagePaths -Root $fixture -Project $project -Architecture 'x86_64'
    Assert-DevelopmentCondition ($paths.Stage.EndsWith('fixture-editor-windows-x86_64-DevDist')) 'Stage must be isolated from Dist.'
    Assert-DevelopmentCondition ($paths.Binaries.EndsWith('DevDist-windows-x86_64')) 'Objects and binaries must not alias Dist.'
    New-Item -ItemType Directory -Force -Path $paths.Stage | Out-Null
    $source = Join-Path $fixture 'source.txt'
    $destination = Join-Path $paths.Stage 'bin\file.txt'
    [IO.File]::WriteAllText($source, 'first')
    Copy-EditorDevelopmentFile -Root $fixture -Source $source -Destination $destination
    Assert-DevelopmentCondition ([IO.File]::ReadAllText($destination) -ceq 'first') 'Initial staged copy failed.'
    $timestamp = (Get-Item -LiteralPath $destination).LastWriteTimeUtc
    Copy-EditorDevelopmentFile -Root $fixture -Source $source -Destination $destination
    Assert-DevelopmentCondition ((Get-Item -LiteralPath $destination).LastWriteTimeUtc -eq $timestamp) 'No-op stage rewrote an unchanged file.'
    [IO.File]::WriteAllText($source, 'other')
    Copy-EditorDevelopmentFile -Root $fixture -Source $source -Destination $destination
    Assert-DevelopmentCondition ([IO.File]::ReadAllText($destination) -ceq 'other') 'Same-size changed content was not staged.'
    $escaped = $false
    try {
        Copy-EditorDevelopmentFile -Root $fixture -Source $source -Destination (Join-Path $fixture '..\escaped.txt')
    }
    catch { $escaped = $_.Exception.Message.Contains('outside the repository root') }
    Assert-DevelopmentCondition $escaped 'Staging accepted an escaping destination.'
    Write-EditorDevelopmentStageIdentity -Root $fixture -Stage $paths.Stage -Project $project `
        -Architecture 'x86_64' -Toolset 'msc'
    $identity = Get-Content -LiteralPath (Join-Path $paths.Stage 'development-stage.json') -Raw | ConvertFrom-Json
    Assert-DevelopmentCondition ($identity.configuration -ceq 'DevDist' -and $identity.developmentArtifact -and
        -not $identity.portable -and $identity.releaseValidation -ceq 'not-run') 'Development provenance must be explicit.'
    Assert-DevelopmentCondition (-not (Test-Path -LiteralPath (Join-Path $paths.Stage 'editor-package.json'))) 'Fast staging must not fabricate a release package manifest.'
    Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage
    Start-EditorDevelopmentStageUpdate -Root $fixture -Stage $paths.Stage
    $incompleteRejected = $false
    try { Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage }
    catch { $incompleteRejected = $_.Exception.Message.Contains('update did not finish') }
    Assert-DevelopmentCondition $incompleteRejected 'An interrupted update must not remain launchable.'
    Complete-EditorDevelopmentStageUpdate -Root $fixture -Stage $paths.Stage
    Complete-EditorDevelopmentStageUpdate -Root $fixture -Stage $paths.Stage
    Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage
    $identityPath = Join-Path $paths.Stage 'development-stage.json'
    [IO.File]::WriteAllText($identityPath, '{')
    $corruptRejected = $false
    try { Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage }
    catch { $corruptRejected = $_.Exception.Message.Contains('stage is not ready') }
    Assert-DevelopmentCondition $corruptRejected 'Corrupt stage provenance must not be accepted.'
    Write-EditorDevelopmentStageIdentity -Root $fixture -Stage $paths.Stage -Project $project `
        -Architecture 'x86_64' -Toolset 'msc'
    Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage
    foreach ($invalidIdentity in @(
            @{ Field = 'configuration'; Value = 'Dist' },
            @{ Field = 'developmentArtifact'; Value = $false },
            @{ Field = 'workingDirectory'; Value = (Join-Path $fixture 'other-repository') })) {
        $invalid = Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json
        $invalid.PSObject.Properties[$invalidIdentity.Field].Value = $invalidIdentity.Value
        [IO.File]::WriteAllText($identityPath, ($invalid | ConvertTo-Json))
        $identityRejected = $false
        try { Assert-EditorDevelopmentStageReady -Root $fixture -Stage $paths.Stage }
        catch { $identityRejected = $_.Exception.Message.Contains('stage is not ready') }
        Assert-DevelopmentCondition $identityRejected "Invalid identity field was accepted: $($invalidIdentity.Field)."
        Write-EditorDevelopmentStageIdentity -Root $fixture -Stage $paths.Stage -Project $project `
            -Architecture 'x86_64' -Toolset 'msc'
    }

    $root = Get-RepositoryRoot
    $policy = Get-Content -LiteralPath (Join-Path $root 'Scripts\Premake\Common.lua') -Raw
    Assert-DevelopmentCondition ($policy -match '(?s)filter "configurations:DevDist".*?linktimeoptimization "Off".*?"KEIRE_DISTRIBUTION"') 'DevDist must retain distribution behavior without LTO.'
    Assert-DevelopmentCondition ($policy -match '(?s)filter "configurations:Dist".*?optimize "full".*?linktimeoptimization "On"') 'Shipping optimization must remain enabled.'
    $packageCommand = Get-Command (Join-Path $windows 'package.ps1')
    $packageConfigurations = @($packageCommand.Parameters['Configuration'].Attributes |
        Where-Object { $_ -is [Management.Automation.ValidateSetAttribute] } |
        ForEach-Object { $_.ValidValues })
    Assert-DevelopmentCondition ($packageConfigurations -contains 'Dist' -and
        $packageConfigurations -notcontains 'DevDist') 'DevDist must not become a distributable SDK package profile.'
    if ($Generated) {
        foreach ($target in @('KeireClient', 'KeireCore', 'KeireEditorTests')) {
            $ninja = Get-Content -LiteralPath (Join-Path $root "$target\$target.ninja")
            $compileFlags = @($ninja | Where-Object { $_ -match "^cxxflags_${target}_DevDist =" })
            Assert-DevelopmentCondition ($compileFlags.Count -eq 1 -and $compileFlags[0] -notmatch '/GL\b' -and
                $compileFlags[0].Contains('/O2') -and $compileFlags[0].Contains('KEIRE_DISTRIBUTION') -and
                $compileFlags[0].Contains('NDEBUG') -and $compileFlags[0] -notmatch 'KEIRE_ENABLE_TEST_HOOKS') "Incorrect generated DevDist flags for $target."
            if ($target -ne 'KeireCore') {
                $linkFlags = @($ninja | Where-Object { $_ -match "^ldflags_${target}_DevDist =" })
                Assert-DevelopmentCondition ($linkFlags.Count -eq 1 -and $linkFlags[0] -notmatch '/LTCG\b') "DevDist must not use LTCG for $target."
            }
            $distFlags = @($ninja | Where-Object { $_ -match "^cxxflags_${target}_Dist =" })
            Assert-DevelopmentCondition ($distFlags.Count -eq 1 -and $distFlags[0] -match '/GL\b') "Shipping LTO was lost for $target."
        }
    }
    $stageSource = Get-Content -LiteralPath (Join-Path $windows 'stage-editor.ps1') -Raw
    Assert-DevelopmentCondition ($stageSource.Contains('-Target "$($project.PROJECT_NAMESPACE)EditorDev"') -and
        -not $stageSource.Contains('package.ps1') -and -not $stageSource.Contains('player-support.ps1')) 'Iteration must not invoke SDK packaging or all player configurations.'
    $ciRejected = $false
    try { & (Join-Path $windows 'stage-editor.ps1') -CI }
    catch { $ciRejected = $_.Exception.Message -like '*DevDist staging is local iteration*' }
    Assert-DevelopmentCondition $ciRejected 'CI development staging should have been rejected.'
    Write-Host 'Editor development staging tests passed (copy/update/no-op, path safety, provenance/recovery, profile separation, CI rejection).'
}
finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedFixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedFixture -Leaf) -notlike 'keire-editor-development-*') {
        throw 'Refusing to remove an unexpected development-stage fixture.'
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
    $global:LASTEXITCODE = 0
}
