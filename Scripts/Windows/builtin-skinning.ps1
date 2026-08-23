$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Compiler = Join-Path $Root "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe"
$Source = Join-Path $Root "KeireCore/Shaders/BuiltinSkinning.hlsl"
$GeneratedDirectory = Join-Path $Root "Build/Generated/Keire"
$Header = Join-Path $GeneratedDirectory "BuiltinSkinningShaders.h"
$Stamp = Join-Path $GeneratedDirectory "BuiltinSkinningShaders.stamp"
$CacheHelper = Join-Path $PSScriptRoot "generated-content-cache.ps1"
. $CacheHelper

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "KeireShaderCompiler is required before generating the built-in skinning shader."
}

$Fingerprint = Get-GeneratedContentFingerprint -Schema "builtin-skinning-v1" `
    -Inputs @($PSCommandPath, $CacheHelper, $Compiler, $Source)
if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
    return
}

$CacheLock = Enter-GeneratedContentLock -Name "builtin-skinning" -RepositoryRoot $Root
$Temporary = $null
try {
    if (Test-GeneratedContentCurrent -Output $Header -Stamp $Stamp -Fingerprint $Fingerprint) {
        return
    }

    $Temporary = Join-Path ([IO.Path]::GetTempPath()) ("KeireBuiltinSkinning-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $Temporary | Out-Null
    $temporarySource = Join-Path $Temporary "BuiltinSkinning.hlsl"
    Copy-Item -LiteralPath $Source -Destination $temporarySource
    $variants = @(
        @{ Destination = "DXIL"; Name = "Dxil"; File = "skinning.dxil" },
        @{ Destination = "SPIRV"; Name = "Spirv"; File = "skinning.spv" },
        @{ Destination = "MSL"; Name = "Msl"; File = "skinning.msl" }
    )
    foreach ($variant in $variants) {
        $output = Join-Path $Temporary $variant.File
        & $Compiler $temporarySource --source HLSL --dest $variant.Destination --stage compute --entrypoint CSMain --output $output
        if ($LASTEXITCODE -ne 0) {
            throw "Built-in skinning shader compilation failed for $($variant.Destination)."
        }
    }

    $builder = [Text.StringBuilder]::new()
    [void]$builder.AppendLine("#pragma once")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("namespace Keire::Detail")
    [void]$builder.AppendLine("{")
    foreach ($variant in $variants) {
        $bytes = [IO.File]::ReadAllBytes((Join-Path $Temporary $variant.File))
        [void]$builder.AppendLine("    inline constexpr unsigned char BuiltinSkinningCompute$($variant.Name)[] = {")
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
    [void]$builder.AppendLine("} // namespace Keire::Detail")

    New-Item -ItemType Directory -Force -Path $GeneratedDirectory | Out-Null
    $content = $builder.ToString()
    $existing = if (Test-Path -LiteralPath $Header) { [IO.File]::ReadAllText($Header) } else { "" }
    if ($existing -ne $content) {
        $temporaryHeader = Join-Path $Temporary "BuiltinSkinningShaders.h"
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
