[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "ninja",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "msc",
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
$configuration = "Release"
$benchmarkRoot = Join-Path $Root "Build\Benchmarks"
$workspaceCacheRoot = if ([string]::IsNullOrWhiteSpace($env:KEIRE_WORKSPACE_CACHE_ROOT)) {
    [IO.Path]::GetTempPath()
}
else {
    [IO.Path]::GetFullPath($env:KEIRE_WORKSPACE_CACHE_ROOT)
}
$temporaryRoot = Join-Path $workspaceCacheRoot ("render-benchmark-" + (Get-KeireWorkspaceIdentity $Root))
$contentRoot = Join-Path $benchmarkRoot "Content\KeireSandbox"
$sampleRoot = Join-Path $benchmarkRoot "Sample\KeireSandbox"
$matrixPath = Join-Path $benchmarkRoot "render-matrix.json"
$runtime = Join-Path $Root "Build\Bin\$configuration-windows-$outputArchitecture\$($Project.PROJECT_NAMESPACE)Runtime\$($Project.PROJECT_NAMESPACE)Runtime.exe"
$assetTool = Join-Path $Root "Build\Bin\$configuration-windows-$outputArchitecture\$($Project.PROJECT_NAMESPACE)AssetTool\$($Project.PROJECT_NAMESPACE)AssetTool.exe"
$expectedCommit = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $expectedCommit) { throw "Render benchmark could not resolve the build commit." }
$expectedDirty = -not [string]::IsNullOrEmpty(((& git -C $Root status --porcelain --untracked-files=normal) -join "`n"))
if ($LASTEXITCODE -ne 0) { throw "Render benchmark could not resolve the build worktree identity." }
if ($env:SDL_VIDEODRIVER -eq "dummy") {
    throw "Render benchmark requires a real graphics-capable window; SDL_VIDEODRIVER=dummy is not supported."
}

function Assert-RequiredProperties {
    param($Value, [string[]]$Names, [string]$Description)
    if ($null -eq $Value) { throw "$Description is missing." }
    $available = @($Value.PSObject.Properties.Name)
    $missing = @($Names | Where-Object { $_ -notin $available })
    if ($missing.Count -ne 0) {
        throw "$Description is missing required fields: $($missing -join ', ')."
    }
}

function Assert-NonNegativeNumber {
    param($Value, [string]$Description)
    if ($null -eq $Value -or $Value -is [bool] -or $Value -is [string]) {
        throw "$Description is not a number."
    }
    $number = [double]$Value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0.0) {
        throw "$Description is not a finite non-negative number."
    }
    return $number
}

function Assert-MetricSummary {
    param($Value, [string]$Description)
    Assert-RequiredProperties $Value @("median", "p95", "p99") $Description
    $median = Assert-NonNegativeNumber $Value.median "$Description.median"
    $p95 = Assert-NonNegativeNumber $Value.p95 "$Description.p95"
    $p99 = Assert-NonNegativeNumber $Value.p99 "$Description.p99"
    if ($median -gt $p95 -or $p95 -gt $p99) {
        throw "$Description percentiles are not monotonic."
    }
}

New-Item -ItemType Directory -Force $benchmarkRoot, $temporaryRoot | Out-Null
Remove-Item -LiteralPath $matrixPath -Force -ErrorAction SilentlyContinue
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousShaderCompiler = $env:KEIRE_SHADER_COMPILER
$previousDotnetRoot = $env:DOTNET_ROOT
$previousPath = $env:PATH
$env:TEMP = $temporaryRoot
$env:TMP = $temporaryRoot
try {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $configuration `
        -Architecture $Architecture -Toolset $Toolset -Target "$($Project.PROJECT_NAMESPACE)AssetTool" `
        -CI:$CI -Update:$Update -Generate:$Generate
    if ($LASTEXITCODE -ne 0) { throw "Release AssetTool build failed with exit code $LASTEXITCODE." }
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $configuration `
        -Architecture $Architecture -Toolset $Toolset -Target "$($Project.PROJECT_NAMESPACE)Runtime" -CI:$CI
    if ($LASTEXITCODE -ne 0) { throw "Release runtime build failed with exit code $LASTEXITCODE." }
    & (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture `
        -Toolset $Toolset | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Shader compiler build failed with exit code $LASTEXITCODE." }
    if (-not (Test-Path -LiteralPath $runtime -PathType Leaf) -or
        -not (Test-Path -LiteralPath $assetTool -PathType Leaf)) {
        throw "Render benchmark executables were not produced."
    }

    Remove-Item -LiteralPath $contentRoot, $sampleRoot -Recurse -Force -ErrorAction SilentlyContinue
    Copy-WindowsTrackedTree -RepositoryRoot $Root -RelativeSource "Samples/KeireSandbox" -Destination $sampleRoot
    $buildScenes = Join-Path $Root "Samples\KeireSandbox\ProjectSettings\BuildScenes.keiresettings"
    if (-not (Test-Path -LiteralPath $buildScenes -PathType Leaf)) {
        throw "Render benchmark requires ProjectSettings/BuildScenes.keiresettings."
    }
    Copy-Item -LiteralPath $buildScenes -Destination (Join-Path $sampleRoot "ProjectSettings\BuildScenes.keiresettings") `
        -Force
    $env:KEIRE_SHADER_COMPILER = Join-Path $Root "Build\Tools\ShaderCompiler\KeireShaderCompiler.exe"
    $env:DOTNET_ROOT = Join-Path $Root "Build\Dependencies\dotnet-sdk"
    $env:PATH = "$env:DOTNET_ROOT;$env:PATH"
    $cookOutput = (& $assetTool cook --project $sampleRoot --output $contentRoot --profile Dist --target windows) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not $cookOutput.Contains("Cooked")) {
        throw "Render benchmark cooked workload failed."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $contentRoot "catalog.json") -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $contentRoot "runtime-manifest.json") -PathType Leaf)) {
        throw "Render benchmark cooked workload is incomplete."
    }

    $reports = @()
    foreach ($presentMode in @("vsync", "immediate")) {
        $output = Join-Path $benchmarkRoot "render-$presentMode.json"
        Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
        & $runtime --content $contentRoot --render-benchmark $output --present-mode $presentMode
        if ($LASTEXITCODE -ne 0) {
            throw "Release $presentMode render benchmark failed with exit code $LASTEXITCODE."
        }
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Release $presentMode render benchmark did not publish its result."
        }
        $report = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        Assert-RequiredProperties $report @("schemaVersion", "status", "workload", "presentMode", "build",
                                            "hardware", "pipeline", "summary", "timelines") `
            "Release $presentMode render benchmark report"
        Assert-RequiredProperties $report.build @("version", "gitCommit", "dirty", "configuration", "compiler",
                                                  "platform", "architecture") `
            "Release $presentMode build identity"
        Assert-RequiredProperties $report.hardware @("operatingSystemDescription", "operatingSystemVersion", "cpuModel",
                                                     "logicalProcessorCount", "physicalMemoryBytes", "rendererBackend",
                                                     "gpuAdapter", "driverName", "driverVersion", "driverInformation",
                                                     "deviceGeneration") `
            "Release $presentMode hardware identity"
        Assert-RequiredProperties $report.pipeline @("allowedFramesInFlight", "framesInFlightHighWaterMark",
                                                     "rendererQueueHighWaterMark") `
            "Release $presentMode pipeline identity"
        $metricNames = @("ownerUpdateMilliseconds", "captureMilliseconds", "admissionWaitMilliseconds",
                         "queueDelayMilliseconds", "renderCpuMilliseconds", "gpuRetirementMilliseconds",
                         "submitToPresentMilliseconds")
        Assert-RequiredProperties $report.summary $metricNames "Release $presentMode timing summary"
        [void](Assert-NonNegativeNumber $report.hardware.logicalProcessorCount `
            "Release $presentMode hardware.logicalProcessorCount")
        [void](Assert-NonNegativeNumber $report.hardware.physicalMemoryBytes `
            "Release $presentMode hardware.physicalMemoryBytes")
        [void](Assert-NonNegativeNumber $report.hardware.deviceGeneration `
            "Release $presentMode hardware.deviceGeneration")
        [void](Assert-NonNegativeNumber $report.pipeline.allowedFramesInFlight `
            "Release $presentMode pipeline.allowedFramesInFlight")
        [void](Assert-NonNegativeNumber $report.pipeline.framesInFlightHighWaterMark `
            "Release $presentMode pipeline.framesInFlightHighWaterMark")
        [void](Assert-NonNegativeNumber $report.pipeline.rendererQueueHighWaterMark `
            "Release $presentMode pipeline.rendererQueueHighWaterMark")
        if ($report.schemaVersion -ne 1 -or $report.status -ne "passed" -or $report.presentMode -ne $presentMode -or
            $report.workload.warmupFrames -ne 300 -or $report.workload.measuredFrames -ne 2000 -or
            $report.build.configuration -ne "Release" -or $report.build.gitCommit -ne $expectedCommit -or
            $report.build.dirty -isnot [bool] -or [bool]$report.build.dirty -ne $expectedDirty -or
            $report.build.version -ne $Project.PROJECT_VERSION -or
            [string]::IsNullOrWhiteSpace([string]$report.build.compiler) -or $report.build.platform -ne "windows" -or
            $report.build.architecture -ne $outputArchitecture -or $report.timelines.Count -ne 2000 -or
            [string]::IsNullOrWhiteSpace([string]$report.hardware.operatingSystemDescription) -or
            [string]::IsNullOrWhiteSpace([string]$report.hardware.operatingSystemVersion) -or
            [string]::IsNullOrWhiteSpace([string]$report.hardware.gpuAdapter) -or
            [string]::IsNullOrWhiteSpace([string]$report.hardware.rendererBackend) -or
            [string]::IsNullOrWhiteSpace([string]$report.hardware.cpuModel) -or
            [uint64]$report.hardware.logicalProcessorCount -eq 0 -or
            [uint64]$report.hardware.physicalMemoryBytes -eq 0 -or [uint64]$report.hardware.deviceGeneration -eq 0 -or
            [uint32]$report.pipeline.allowedFramesInFlight -lt 1 -or
            [uint32]$report.pipeline.allowedFramesInFlight -gt 3 -or
            [uint32]$report.pipeline.framesInFlightHighWaterMark -lt 1 -or
            [uint32]$report.pipeline.framesInFlightHighWaterMark -gt
                [uint32]$report.pipeline.allowedFramesInFlight -or
            [uint32]$report.pipeline.rendererQueueHighWaterMark -lt 1) {
            throw "Release $presentMode render benchmark published a missing, stale, or mismatched result."
        }
        foreach ($metricName in $metricNames) {
            Assert-MetricSummary $report.summary.$metricName "Release $presentMode summary.$metricName"
        }
        $timelineFields = @("frame", "ownerUpdateMilliseconds", "captureMilliseconds", "admissionWaitMilliseconds",
                            "queueDelayMilliseconds", "renderCpuMilliseconds", "gpuRetirementMilliseconds",
                            "submitToPresentMilliseconds", "outstandingAtAdmission")
        for ($index = 0; $index -lt $report.timelines.Count; ++$index) {
            $timeline = $report.timelines[$index]
            Assert-RequiredProperties $timeline $timelineFields "Release $presentMode timeline $index"
            $frame = Assert-NonNegativeNumber $timeline.frame "Release $presentMode timeline $index.frame"
            $outstanding = Assert-NonNegativeNumber $timeline.outstandingAtAdmission `
                "Release $presentMode timeline $index.outstandingAtAdmission"
            if ($frame -eq 0 -or [math]::Truncate($frame) -ne $frame -or
                [math]::Truncate($outstanding) -ne $outstanding -or
                $outstanding -gt [uint32]$report.pipeline.allowedFramesInFlight) {
                throw "Release $presentMode timeline $index published invalid frame-admission data."
            }
            foreach ($metricName in $metricNames) {
                [void](Assert-NonNegativeNumber $timeline.$metricName "Release $presentMode timeline $index.$metricName")
            }
            if ($index -gt 0 -and
                [uint64]$frame -ne [uint64]$report.timelines[$index - 1].frame + 1) {
                throw "Release $presentMode render benchmark published non-monotonic frame IDs."
            }
        }
        $reports += $report
    }
    if ($reports.Count -ne 2 -or $reports[0].presentMode -eq $reports[1].presentMode) {
        throw "Render benchmark skipped a required VSync mode."
    }
    $matrix = [ordered]@{
        schemaVersion = 1
        status = "passed"
        warmupFrames = 300
        measuredFrames = 2000
        buildCommit = $expectedCommit
        runs = $reports
    }
    $matrixTemporary = "$matrixPath.tmp-$([guid]::NewGuid().ToString('N'))"
    [IO.File]::WriteAllText($matrixTemporary, (($matrix | ConvertTo-Json -Depth 100) + "`n"),
                            [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $matrixTemporary -Destination $matrixPath -Force
    Write-Host "==> Render benchmark matrix: $matrixPath"
}
finally {
    $env:KEIRE_SHADER_COMPILER = $previousShaderCompiler
    $env:DOTNET_ROOT = $previousDotnetRoot
    $env:PATH = $previousPath
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
}
