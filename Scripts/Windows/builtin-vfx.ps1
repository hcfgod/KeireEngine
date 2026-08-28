$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Compiler = Join-Path $Root "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe"
$Source = Join-Path $Root "KeireCore/Shaders/BuiltinVfx.hlsl"
$VisibilitySource = Join-Path $Root "KeireCore/Shaders/BuiltinVfxVisibility.hlsl"
$GeneratedDirectory = Join-Path $Root "Build/Generated/Keire"
$Header = Join-Path $GeneratedDirectory "BuiltinVfxShaders.h"
$Stamp = Join-Path $GeneratedDirectory "BuiltinVfxShaders.stamp"
$CacheHelper = Join-Path $PSScriptRoot "generated-content-cache.ps1"
. $CacheHelper

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "KeireShaderCompiler is required before generating the built-in VFX shaders."
}

$Fingerprint = Get-GeneratedContentFingerprint -Schema "builtin-vfx-v2" `
    -Inputs @($PSCommandPath, $CacheHelper, $Compiler, $Source, $VisibilitySource)
if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
    return
}

$CacheLock = Enter-GeneratedContentLock -Name "builtin-vfx" -RepositoryRoot $Root
$Temporary = $null
try {
    if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
        return
    }

    $Temporary = Join-Path ([IO.Path]::GetTempPath()) ("KeireBuiltinVfx-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $Temporary | Out-Null
    $temporarySource = Join-Path $Temporary "BuiltinVfx.hlsl"
    $temporaryVisibilitySource = Join-Path $Temporary "BuiltinVfxVisibility.hlsl"
    Copy-Item -LiteralPath $Source -Destination $temporarySource
    Copy-Item -LiteralPath $VisibilitySource -Destination $temporaryVisibilitySource
    $stages = @(
        @{ Stage = "compute"; Entry = "CSInitialize"; Name = "Initialize" },
        @{ Stage = "compute"; Entry = "CSReset"; Name = "Reset" },
        @{ Stage = "compute"; Entry = "CSKill"; Name = "Kill" },
        @{ Stage = "compute"; Entry = "CSTransform"; Name = "Transform" },
        @{ Stage = "compute"; Entry = "CSSimulate"; Name = "Simulate" },
        @{ Stage = "compute"; Entry = "CSSimulateOutput"; Name = "SimulateOutput" },
        @{ Stage = "compute"; Entry = "CSSpawn"; Name = "Spawn" },
        @{ Stage = "compute"; Entry = "CSSpawnInitialize"; Name = "SpawnInitialize" },
        @{ Stage = "compute"; Entry = "CSSpawnOutput"; Name = "SpawnOutput" },
        @{ Stage = "compute"; Entry = "CSMapStrips"; Name = "MapStrips" },
        @{ Stage = "compute"; Entry = "CSLinkStrips"; Name = "LinkStrips" },
        @{ Stage = "compute"; Entry = "CSFinalize"; Name = "Finalize" },
        @{ Stage = "compute"; Entry = "CSResetRender"; Name = "ResetRender" },
        @{ Stage = "compute"; Entry = "CSFilterRender"; Name = "FilterRender" },
        @{ Stage = "compute"; Entry = "CSBuildVisibilityCandidates"; Name = "BuildVisibilityCandidates" },
        @{ Stage = "compute"; Entry = "CSCompactVisibility"; Name = "CompactVisibility" },
        @{ Stage = "vertex"; Entry = "VSMain"; Name = "Vertex" },
        @{ Stage = "vertex"; Entry = "VSRibbon"; Name = "RibbonVertex" },
        @{ Stage = "fragment"; Entry = "PSMain"; Name = "Fragment" },
        @{ Stage = "vertex"; Entry = "VSMesh"; Name = "MeshVertex" },
        @{ Stage = "fragment"; Entry = "PSMesh"; Name = "MeshFragment" },
        @{ Stage = "vertex"; Entry = "VSCpu"; Name = "CpuVertex" },
        @{ Stage = "fragment"; Entry = "PSCpu"; Name = "CpuFragment" }
    )
    $variants = @(
        @{ Destination = "DXIL"; Name = "Dxil"; Extension = "dxil" },
        @{ Destination = "SPIRV"; Name = "Spirv"; Extension = "spv" },
        @{ Destination = "MSL"; Name = "Msl"; Extension = "msl" }
    )
    foreach ($stage in $stages) {
        foreach ($variant in $variants) {
            $output = Join-Path $Temporary ("$($stage.Name)-$($variant.Name).$($variant.Extension)")
            $stageSource = if ($stage.Name -in @("BuildVisibilityCandidates", "CompactVisibility")) {
                $temporaryVisibilitySource
            } else {
                $temporarySource
            }
            & $Compiler $stageSource --source HLSL --dest $variant.Destination --stage $stage.Stage `
                --entrypoint $stage.Entry --output $output
            if ($LASTEXITCODE -ne 0) {
                throw "Built-in VFX shader compilation failed for $($stage.Entry) / $($variant.Destination)."
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
                "    inline constexpr unsigned char BuiltinVfx$($stage.Name)$($variant.Name)[] = {")
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
        $temporaryHeader = Join-Path $Temporary "BuiltinVfxShaders.h"
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
