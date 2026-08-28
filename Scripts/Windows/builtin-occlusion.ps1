$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Compiler = Join-Path $Root "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe"
$GeneratedDirectory = Join-Path $Root "Build/Generated/Keire"
$Header = Join-Path $GeneratedDirectory "BuiltinOcclusionShaders.h"
$Stamp = Join-Path $GeneratedDirectory "BuiltinOcclusionShaders.stamp"
$CacheHelper = Join-Path $PSScriptRoot "generated-content-cache.ps1"
. $CacheHelper

$Sources = @(
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionDepth.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionPyramid.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionClassify.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionScanBlocks.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionScanBatches.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionScatter.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionDebugPyramid.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinOcclusionDebugBounds.hlsl"),
    (Join-Path $Root "KeireCore/Shaders/BuiltinForwardPlusVisibility.hlsl")
)

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "KeireShaderCompiler is required before generating the built-in occlusion shaders."
}

$FingerprintInputs = @($PSCommandPath, $CacheHelper, $Compiler) + $Sources
$Fingerprint = Get-GeneratedContentFingerprint -Schema "builtin-occlusion-v1" -Inputs $FingerprintInputs
if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
    return
}

$CacheLock = Enter-GeneratedContentLock -Name "builtin-occlusion" -RepositoryRoot $Root
$Temporary = $null
try {
    if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
        return
    }

    $Temporary = Join-Path ([IO.Path]::GetTempPath()) ("KeireBuiltinOcclusion-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $Temporary | Out-Null
    $temporarySources = @(
        (Join-Path $Temporary "Depth.hlsl"),
        (Join-Path $Temporary "Pyramid.hlsl"),
        (Join-Path $Temporary "Classify.hlsl"),
        (Join-Path $Temporary "ScanBlocks.hlsl"),
        (Join-Path $Temporary "ScanBatches.hlsl"),
        (Join-Path $Temporary "Scatter.hlsl"),
        (Join-Path $Temporary "DebugPyramid.hlsl"),
        (Join-Path $Temporary "DebugBounds.hlsl"),
        (Join-Path $Temporary "ForwardPlusVisibility.hlsl")
    )
    for ($sourceIndex = 0; $sourceIndex -lt $Sources.Count; ++$sourceIndex) {
        Copy-Item -LiteralPath $Sources[$sourceIndex] -Destination $temporarySources[$sourceIndex]
    }
    $stages = @(
        @{ Source = $temporarySources[0]; Stage = "vertex"; Entry = "VSDepth"; Name = "DepthVertex" },
        @{ Source = $temporarySources[0]; Stage = "fragment"; Entry = "PSDepth"; Name = "DepthFragment" },
        @{ Source = $temporarySources[1]; Stage = "compute"; Entry = "CSBuildBase"; Name = "PyramidBase" },
        @{ Source = $temporarySources[1]; Stage = "compute"; Entry = "CSReduce"; Name = "PyramidReduce" },
        @{ Source = $temporarySources[2]; Stage = "compute"; Entry = "CSClassify"; Name = "Classify" },
        @{ Source = $temporarySources[3]; Stage = "compute"; Entry = "CSScanBlocks"; Name = "ScanBlocks" },
        @{ Source = $temporarySources[4]; Stage = "compute"; Entry = "CSScanBatches"; Name = "ScanBatches" },
        @{ Source = $temporarySources[5]; Stage = "compute"; Entry = "CSScatter"; Name = "Scatter" },
        @{ Source = $temporarySources[6]; Stage = "vertex"; Entry = "VSDebugPyramid"; Name = "DebugPyramidVertex" },
        @{ Source = $temporarySources[6]; Stage = "fragment"; Entry = "PSDebugPyramid"; Name = "DebugPyramidFragment" },
        @{ Source = $temporarySources[7]; Stage = "vertex"; Entry = "VSDebugBounds"; Name = "DebugBoundsVertex" },
        @{ Source = $temporarySources[7]; Stage = "fragment"; Entry = "PSDebugBounds"; Name = "DebugBoundsFragment" },
        @{ Source = $temporarySources[8]; Stage = "compute"; Entry = "CSCompactForwardPlusTiles"; Name = "ForwardPlusVisibility" }
    )
    $variants = @(
        @{ Destination = "DXIL"; Name = "Dxil"; Extension = "dxil" },
        @{ Destination = "SPIRV"; Name = "Spirv"; Extension = "spv" },
        @{ Destination = "MSL"; Name = "Msl"; Extension = "msl" }
    )
    foreach ($stage in $stages) {
        foreach ($variant in $variants) {
            $output = Join-Path $Temporary ("$($stage.Name)-$($variant.Name).$($variant.Extension)")
            & $Compiler $stage.Source --source HLSL --dest $variant.Destination --stage $stage.Stage `
                --entrypoint $stage.Entry --output $output
            if ($LASTEXITCODE -ne 0) {
                throw "Built-in occlusion shader compilation failed for $($stage.Entry) / $($variant.Destination)."
            }
        }
    }

    $builder = [Text.StringBuilder]::new()
    [void]$builder.AppendLine("#pragma once")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("namespace Keire::Detail")
    [void]$builder.AppendLine("{")
    foreach ($stage in $stages) {
        foreach ($variant in $variants) {
            $bytes = [IO.File]::ReadAllBytes(
                (Join-Path $Temporary ("$($stage.Name)-$($variant.Name).$($variant.Extension)")))
            [void]$builder.AppendLine(
                "    inline constexpr unsigned char BuiltinOcclusion$($stage.Name)$($variant.Name)[] = {")
            for ($index = 0; $index -lt $bytes.Length; $index += 16) {
                $count = [Math]::Min(16, $bytes.Length - $index)
                $values = for ($offset = 0; $offset -lt $count; ++$offset) {
                    "0x{0:x2}" -f $bytes[$index + $offset]
                }
                [void]$builder.AppendLine("        " + ($values -join ", ") + ",")
            }
            [void]$builder.AppendLine("    };")
            [void]$builder.AppendLine()
        }
    }
    [void]$builder.AppendLine("} // namespace Keire::Detail")

    New-Item -ItemType Directory -Force -Path $GeneratedDirectory | Out-Null
    $content = $builder.ToString()
    $existing = if (Test-Path -LiteralPath $Header) { [IO.File]::ReadAllText($Header) } else { "" }
    if ($existing -ne $content) {
        $temporaryHeader = Join-Path $Temporary "BuiltinOcclusionShaders.h"
        [IO.File]::WriteAllText($temporaryHeader, $content, [Text.Encoding]::ASCII)
        Move-Item -LiteralPath $temporaryHeader -Destination $Header -Force
    }
    Write-GeneratedContentStamp -Stamp $Stamp -Fingerprint $Fingerprint
}
finally {
    if ($Temporary) {
        Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
    }
    Exit-GeneratedContentLock -Mutex $CacheLock
}
