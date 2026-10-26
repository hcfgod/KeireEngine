[CmdletBinding()]
param(
    [string]$Generator = 'ninja',
    [string]$Architecture = '',
    [string]$Toolset = 'default',
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'ffmpeg-runtime-contract.ps1')
. (Join-Path $PSScriptRoot 'editor-development-stage.ps1')
if ($CI) { throw 'DevDist staging is local iteration, not a CI or release gate.' }
$root = Get-RepositoryRoot
$project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$paths = Get-EditorDevelopmentStagePaths -Root $root -Project $project -Architecture $Architecture
$workspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $root -CommandName 'stage-editor-devdist'
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    Invoke-CheckedWindowsCommand {
        & (Join-Path $PSScriptRoot 'build.ps1') -Generator $Generator -Configuration DevDist `
            -Architecture $Architecture -Toolset $Toolset -Target "$($project.PROJECT_NAMESPACE)EditorDev" `
            -Update:$Update -Generate:$Generate -ProfileBuild
    } 'DevDist editor and runtime-tool build'
    Invoke-CheckedWindowsCommand {
        & (Join-Path $PSScriptRoot 'shader-compiler.ps1') -Generator $Generator `
            -Architecture $Architecture -Toolset $Toolset
    } 'Cached shader compiler preparation'

    Assert-KeireContainedWindowsPath -Root $root -Path $paths.Stage
    New-Item -ItemType Directory -Force -Path $paths.Stage | Out-Null
    Start-EditorDevelopmentStageUpdate -Root $root -Stage $paths.Stage
    foreach ($target in @($project.CLIENT_TARGET, "$($project.PROJECT_NAMESPACE)AssetTool",
            "$($project.PROJECT_NAMESPACE)AssetWorker", "$($project.PROJECT_NAMESPACE)Runtime")) {
        $source = Join-Path $paths.Binaries $target
        Copy-EditorDevelopmentFile -Root $root -Source (Join-Path $source "$target.exe") `
            -Destination (Join-Path $paths.Stage "bin\$target.exe")
        foreach ($runtime in Get-ChildItem -LiteralPath $source -File -Filter '*.dll') {
            Copy-EditorDevelopmentFile -Root $root -Source $runtime.FullName `
                -Destination (Join-Path $paths.Stage "bin\$($runtime.Name)")
        }
    }
    # Runtime assemblies are small and content-driven. The full SDK remains in the repository's pinned cache.
    Copy-EditorDevelopmentTree -Root $root -Source (Join-Path $paths.Binaries "$($project.CLIENT_TARGET)\Managed") `
        -Destination (Join-Path $paths.Stage 'bin\Managed')
    foreach ($compilerFile in Get-ChildItem -LiteralPath (Join-Path $root 'Build\Tools\ShaderCompiler') -File) {
        if ($compilerFile.Extension -in @('.exe', '.dll')) {
            Copy-EditorDevelopmentFile -Root $root -Source $compilerFile.FullName `
                -Destination (Join-Path $paths.Stage "bin\$($compilerFile.Name)")
        }
    }
    foreach ($relative in @('Config\Client.json', 'Config\SourceModules.premake.lua', 'Config\Branding\Keire.png',
            'Config\Marketplace\trusted-marketplace-key.json', 'Config\Marketplace\trusted-marketplace-keys.json')) {
        Copy-EditorDevelopmentFile -Root $root -Source (Join-Path $root $relative) `
            -Destination (Join-Path $paths.Stage $relative)
    }
    Copy-EditorDevelopmentTree -Root $root -Source (Join-Path $root 'KeireHubContent\Fonts') `
        -Destination (Join-Path $paths.Stage 'content\Fonts')
    foreach ($relative in @('README.md', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md')) {
        Copy-EditorDevelopmentFile -Root $root -Source (Join-Path $root $relative) `
            -Destination (Join-Path $paths.Stage $relative)
    }
    Write-EditorDevelopmentStageIdentity -Root $root -Stage $paths.Stage -Project $project `
        -Architecture $Architecture -Toolset $Toolset
    $launcher = @"
@echo off
if exist "%~dp0development-stage.incomplete" (
    echo The DevDist update did not finish. Run Scripts\project.bat stage-editor again.
    exit /b 1
)
cd /d "%~dp0..\..\.."
start "" "%~dp0bin\$($project.CLIENT_TARGET).exe" %*
"@
    $launcherPath = Join-Path $paths.Stage 'Launch-KeireEditor.cmd'
    Assert-KeireContainedWindowsPath -Root $root -Path $launcherPath
    [IO.File]::WriteAllText($launcherPath, $launcher, [Text.ASCIIEncoding]::new())
    Complete-EditorDevelopmentStageUpdate -Root $root -Stage $paths.Stage
    Write-Host "==> DevDist Editor ready: $($paths.Stage)"
    Write-Host '==> Repository-local iteration only: no SDK archives, installers, or player Build Support regeneration.'
    Write-Host '==> Use stage-editor -Configuration Dist for shipping-optimization and packaged-player acceptance.'
}
finally {
    $timer.Stop()
    Write-Host ("==> DevDist stage elapsed: {0:n1} seconds" -f $timer.Elapsed.TotalSeconds)
    Exit-KeireWorkspaceLock -Lock $workspaceLock
}
