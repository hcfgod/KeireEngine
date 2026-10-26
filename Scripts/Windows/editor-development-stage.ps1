# Helpers for a repository-local iteration layout, never a distributable Editor package.
function Get-EditorDevelopmentStagePaths {
    param([string]$Root, $Project, [string]$Architecture)

    $outputArchitecture = Get-ArchitectureOutputName $Architecture
    $stageName = "$($Project.ARTIFACT_PREFIX)-editor-windows-$Architecture-DevDist"
    return [pscustomobject]@{
        Stage = Join-Path $Root "Build\Distributions\$stageName"
        Binaries = Join-Path $Root "Build\Bin\DevDist-windows-$outputArchitecture"
        Configuration = 'DevDist'
    }
}

function Copy-EditorDevelopmentFile {
    param([string]$Root, [string]$Source, [string]$Destination)

    Assert-KeireContainedWindowsPath -Root $Root -Path $Source
    Assert-KeireContainedWindowsPath -Root $Root -Path $Destination
    & (Join-Path $PSScriptRoot 'copy-file-if-changed.ps1') -Source $Source -Destination $Destination
}

function Copy-EditorDevelopmentTree {
    param([string]$Root, [string]$Source, [string]$Destination)

    Assert-KeireContainedWindowsPath -Root $Root -Path $Source
    Assert-KeireContainedWindowsPath -Root $Root -Path $Destination
    foreach ($file in Get-ChildItem -LiteralPath $Source -File -Recurse -Force -ErrorAction Stop) {
        $relative = Get-WindowsRelativePath $Source $file.FullName
        Copy-EditorDevelopmentFile -Root $Root -Source $file.FullName -Destination (Join-Path $Destination $relative)
    }
}

function Start-EditorDevelopmentStageUpdate {
    param([string]$Root, [string]$Stage)

    $marker = Join-Path $Stage 'development-stage.incomplete'
    Assert-KeireContainedWindowsPath -Root $Root -Path $marker
    [IO.File]::WriteAllText($marker, 'The development layout is being updated. Rerun stage-editor if interrupted.')
}

function Complete-EditorDevelopmentStageUpdate {
    param([string]$Root, [string]$Stage)

    $marker = Join-Path $Stage 'development-stage.incomplete'
    Assert-KeireContainedWindowsPath -Root $Root -Path $marker
    if (Test-Path -LiteralPath $marker -PathType Leaf) {
        Remove-Item -LiteralPath $marker -Force
    }
}

function Assert-EditorDevelopmentStageReady {
    param([string]$Root, [string]$Stage)

    $repair = 'Run Scripts\project.bat stage-editor -Configuration DevDist to finish updating the layout.'
    $marker = Join-Path $Stage 'development-stage.incomplete'
    $identity = Join-Path $Stage 'development-stage.json'
    Assert-KeireContainedWindowsPath -Root $Root -Path $marker
    Assert-KeireContainedWindowsPath -Root $Root -Path $identity
    if (Test-Path -LiteralPath $marker) {
        throw "The DevDist stage update did not finish. $repair"
    }
    try {
        $metadata = [IO.File]::ReadAllText($identity) | ConvertFrom-Json
        if ($metadata.schemaVersion -ne 1 -or $metadata.configuration -cne 'DevDist' -or
            $metadata.developmentArtifact -ne $true -or $metadata.portable -ne $false -or
            $metadata.stageStatus -cne 'ready' -or
            -not [string]::Equals([IO.Path]::GetFullPath([string]$metadata.workingDirectory),
                [IO.Path]::GetFullPath($Root), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Development stage identity is incomplete or belongs to another repository.'
        }
    }
    catch {
        throw "The DevDist stage is not ready: $($_.Exception.Message) $repair"
    }
}

function Write-EditorDevelopmentStageIdentity {
    param([string]$Root, [string]$Stage, $Project, [string]$Architecture, [string]$Toolset)

    Assert-KeireContainedWindowsPath -Root $Root -Path $Stage
    $manifest = [ordered]@{
        schemaVersion = 1
        project = $Project.PROJECT_IDENTIFIER
        version = $Project.PROJECT_VERSION
        commit = Get-GitHeadCommit $Root 'unknown'
        configuration = 'DevDist'
        stageStatus = 'ready'
        developmentArtifact = $true
        portable = $false
        workingDirectory = [IO.Path]::GetFullPath($Root)
        architecture = $Architecture
        toolset = $Toolset
        releaseValidation = 'not-run'
        buildSupport = 'not-generated; use the Dist stage for packaged-player acceptance'
    }
    $identity = Join-Path $Stage 'development-stage.json'
    Assert-KeireContainedWindowsPath -Root $Root -Path $identity
    $content = ($manifest | ConvertTo-Json) + "`n"
    if (-not (Test-Path -LiteralPath $identity -PathType Leaf) -or
        [IO.File]::ReadAllText($identity) -cne $content) {
        [IO.File]::WriteAllText($identity, $content, [Text.UTF8Encoding]::new($false))
    }
}
