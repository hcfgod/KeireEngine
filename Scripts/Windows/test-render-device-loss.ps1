[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "ninja",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "msc",
    [string]$CacheRoot = "",
    [ValidateRange(60, 3600)]
    [int]$EditorSmokeTimeoutSeconds = 600,
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$configuration = "Debug"
$expectedCommit = Get-GitHeadCommit $Root "unknown"
$cacheRoot = if ($CacheRoot) {
    [IO.Path]::GetFullPath($CacheRoot)
} else {
    Join-Path $Root "Build\Cache\DeviceLoss"
}
$temporaryRoot = Join-Path $cacheRoot "temp"
$validationRoot = Join-Path $Root "Build\Validation\DeviceLoss"
$packageRoot = Join-Path $validationRoot "Package"
$runtimeStage = Join-Path $packageRoot "bin"
$contentStage = Join-Path $packageRoot "content\KeireSandbox"
$runtimeReport = Join-Path $validationRoot "cooked-runtime-device-loss-$outputArchitecture.json"
$editorReport = Join-Path $validationRoot "editor-play-device-loss-$outputArchitecture.json"
$matrixReport = Join-Path $validationRoot "device-loss-e2e-$outputArchitecture.json"
$runtimeName = "$($Project.PROJECT_NAMESPACE)Runtime"
$assetToolName = "$($Project.PROJECT_NAMESPACE)AssetTool"
$runtimeSource = Join-Path $Root "Build\Bin\$configuration-windows-$outputArchitecture\$runtimeName"
$assetTool = Join-Path $Root "Build\Bin\$configuration-windows-$outputArchitecture\$assetToolName\$assetToolName.exe"
$shaderCompiler = Join-Path $Root "Build\Tools\ShaderCompiler\KeireShaderCompiler.exe"
$sampleProject = Join-Path $Root "Samples\KeireSandbox"
$buildScenes = Join-Path $sampleProject "ProjectSettings\BuildScenes.keiresettings"

function Read-FreshValidationReport {
    param([string]$Path, [DateTime]$StartedAt, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description did not publish its result."
    }
    if ((Get-Item -LiteralPath $Path).LastWriteTimeUtc -lt $StartedAt) {
        throw "$Description published a stale result."
    }
    try { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { throw "$Description published malformed JSON: $($_.Exception.Message)" }
}

function Assert-RecoveryReport {
    param($Report, [string]$Description, [string]$PhaseField)
    if ($Report.schemaVersion -ne 1 -or $Report.status -ne "passed" -or
        $Report.build.gitCommit -ne $expectedCommit -or $Report.build.configuration -ne $configuration -or
        -not $Report.deviceLoss.$PhaseField -or -not $Report.deviceLoss.recoverySucceeded -or
        $Report.deviceLoss.operation -ne "test frame injection" -or
        [string]::IsNullOrWhiteSpace([string]$Report.deviceLoss.backend) -or
        [string]::IsNullOrWhiteSpace([string]$Report.deviceLoss.adapter) -or
        $Report.deviceLoss.recoveryAttempt -ne 1 -or $Report.deviceLoss.newGeneration -le $Report.deviceLoss.oldGeneration -or
        $Report.deviceLoss.retryCount -ne 1 -or $Report.deviceLoss.lostGenerationGpuCleanupCalls -ne 0 -or
        $Report.deviceLoss.retainedVfxSnapshots -lt 1 -or -not $Report.deviceLoss.continuedAfterRecovery) {
        throw "$Description published an incomplete or mismatched recovery result."
    }
}

function Assert-ShutdownLossReport {
    param($Report, [string]$Description)
    $shutdown = $Report.deviceLoss.shutdown
    if (-not $Report.renderedWindowLoop -or $Report.renderMode -ne "rendered" -or
        -not $Report.nativeWindowCreated -or -not $Report.validationWindowHidden -or
        -not $shutdown.duringShutdown -or -not $shutdown.acceptedFrameBlockedBeforeClose -or
        $shutdown.operation -ne "test frame injection" -or $shutdown.recoverySucceeded -or
        $shutdown.recoveryAttempt -ne 0 -or $shutdown.newGeneration -ne 0 -or
        $shutdown.oldGeneration -ne $Report.deviceLoss.newGeneration -or
        $shutdown.recoveryAttemptCount -ne 1 -or -not $shutdown.rendererClosed -or
        $shutdown.outstandingFrames -ne 0 -or $shutdown.lostGenerationGpuCleanupCalls -ne 0 -or
        $shutdown.healthyCandidateCleanupCalls -ne 0) {
        throw "$Description did not prove device loss was contained after shutdown began."
    }
}

$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousNuget = $env:NUGET_PACKAGES
$previousDotnetHome = $env:DOTNET_CLI_HOME
$previousShaderCompiler = $env:KEIRE_SHADER_COMPILER
$previousDotnetRoot = $env:DOTNET_ROOT
$previousPath = $env:PATH
try {
    New-Item -ItemType Directory -Force $temporaryRoot, $validationRoot | Out-Null
    $env:TEMP = $temporaryRoot
    $env:TMP = $temporaryRoot
    $env:NUGET_PACKAGES = Join-Path $cacheRoot "nuget"
    $env:DOTNET_CLI_HOME = Join-Path $cacheRoot "dotnet-home"

    foreach ($target in @($assetToolName, $runtimeName)) {
        Invoke-CheckedWindowsCommand {
            & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $configuration `
                -Architecture $Architecture -Toolset $Toolset -Target $target -CI:$CI -Update:$Update `
                -Generate:$Generate
        } "$target device-loss validation build"
        $Update = $false
        $Generate = $false
    }
    Invoke-CheckedWindowsCommand {
        & (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture `
            -Toolset $Toolset
    } "Shader compiler device-loss validation build"

    foreach ($required in @($runtimeSource, $assetTool, $shaderCompiler, $buildScenes)) {
        if (-not (Test-Path -LiteralPath $required)) { throw "Device-loss validation input is missing: $required" }
    }
    Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $runtimeStage, $contentStage | Out-Null
    Copy-Item -Path (Join-Path $runtimeSource "*") -Destination $runtimeStage -Recurse -Force
    Copy-Item -LiteralPath $shaderCompiler -Destination $runtimeStage -Force
    Get-ChildItem -LiteralPath (Split-Path $shaderCompiler -Parent) -Filter *.dll -File |
        Copy-Item -Destination $runtimeStage -Force

    $env:KEIRE_SHADER_COMPILER = Join-Path $runtimeStage "KeireShaderCompiler.exe"
    $env:DOTNET_ROOT = Join-Path $Root "Build\Dependencies\dotnet-sdk"
    $env:PATH = "$env:DOTNET_ROOT;$env:PATH"
    $cookOutput = (& $assetTool cook --project $sampleProject --output $contentStage --profile Dist --target windows) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not $cookOutput.Contains("Cooked")) {
        throw "Device-loss validation could not cook the packaged sample."
    }
    foreach ($required in @((Join-Path $contentStage "catalog.json"),
                            (Join-Path $contentStage "runtime-manifest.json"))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Device-loss validation cooked package is incomplete: $required"
        }
    }

    Remove-Item -LiteralPath $runtimeReport -Force -ErrorAction SilentlyContinue
    $runtimeStartedAt = [DateTime]::UtcNow
    & (Join-Path $runtimeStage "$runtimeName.exe") --content $contentStage `
        --validate-additive-runtime $runtimeReport --validate-device-loss --hidden-validation-window
    if ($LASTEXITCODE -ne 0) { throw "Cooked runtime device-loss validation exited with $LASTEXITCODE." }
    $runtime = Read-FreshValidationReport $runtimeReport $runtimeStartedAt "Cooked runtime device-loss validation"
    Assert-RecoveryReport $runtime "Cooked runtime device-loss validation" "duringLoading"
    Assert-ShutdownLossReport $runtime "Cooked runtime shutdown device-loss validation"
    if ($runtime.twoSceneContributions -ne 2 -or $runtime.threeSceneContributions -ne 3 -or
        $runtime.twoSceneUiCommands -lt 2 -or $runtime.threeSceneUiCommands -lt 2 -or
        -not $runtime.noPresentationSession -or -not $runtime.unloadReloadOrder -or
        -not $runtime.inputHandledByActiveTopmostPresentation -or -not $runtime.failedLoadPreservedWorld) {
        throw "Cooked runtime device-loss validation skipped an additive-scene scenario."
    }

    Remove-Item -LiteralPath $editorReport -Force -ErrorAction SilentlyContinue
    $editorStartedAt = [DateTime]::UtcNow
    Invoke-CheckedWindowsCommand {
        & (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $configuration `
            -Architecture $Architecture -Toolset $Toolset -CI:$CI -SmokePlay -SmokePlayDeviceLoss `
            -SmokeOutput $editorReport -SmokeTimeoutSeconds $EditorSmokeTimeoutSeconds -ProjectPath $sampleProject
    } "Rendered Editor Play device-loss validation"
    $editor = Read-FreshValidationReport $editorReport $editorStartedAt "Rendered Editor Play device-loss validation"
    Assert-RecoveryReport $editor "Rendered Editor Play device-loss validation" "duringPlay"
    if (-not $editor.renderedWindowLoop -or $editor.twoSceneContributions -ne 2 -or
        $editor.observedRenderedFrames -lt 2 -or
        -not $editor.twoPresentationTrees -or -not $editor.activeSessionRendered -or
        -not $editor.topmostInputHandled -or -not $editor.nativeWindowInputQueued -or
        -not $editor.unloadReloadOrder) {
        throw "Rendered Editor Play device-loss validation skipped a window/input scenario."
    }

    [ordered]@{
        schemaVersion = 1
        status = "passed"
        build = [ordered]@{ gitCommit = $expectedCommit; configuration = $configuration }
        cookedRuntimeReport = $runtimeReport
        editorPlayReport = $editorReport
        cookedRuntimeShutdownCompleted = $true
        cookedRuntimeShutdownDeviceLoss = $true
        editorShutdownCompleted = $true
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $matrixReport -Encoding utf8NoBOM
    Write-Host "==> Real cooked-runtime and Editor Play device-loss validation passed: $matrixReport"
}
finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    $env:NUGET_PACKAGES = $previousNuget
    $env:DOTNET_CLI_HOME = $previousDotnetHome
    $env:KEIRE_SHADER_COMPILER = $previousShaderCompiler
    $env:DOTNET_ROOT = $previousDotnetRoot
    $env:PATH = $previousPath
}
