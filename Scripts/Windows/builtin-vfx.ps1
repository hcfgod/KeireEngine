$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Compiler = Join-Path $Root "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe"
$Source = Join-Path $Root "KeireCore/Shaders/BuiltinVfx.hlsl"
$GeneratedDirectory = Join-Path $Root "Build/Generated/Keire"
$Header = Join-Path $GeneratedDirectory "BuiltinVfxShaders.h"

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "KeireShaderCompiler is required before generating the built-in VFX shaders."
}

$Temporary = Join-Path ([IO.Path]::GetTempPath()) ("KeireBuiltinVfx-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $Temporary | Out-Null
try {
    $temporarySource = Join-Path $Temporary "BuiltinVfx.hlsl"
    Copy-Item -LiteralPath $Source -Destination $temporarySource
    $stages = @(
        @{ Stage = "compute"; Entry = "CSInitialize"; Name = "Initialize" },
        @{ Stage = "compute"; Entry = "CSReset"; Name = "Reset" },
        @{ Stage = "compute"; Entry = "CSSimulate"; Name = "Simulate" },
        @{ Stage = "compute"; Entry = "CSSpawn"; Name = "Spawn" },
        @{ Stage = "compute"; Entry = "CSFinalize"; Name = "Finalize" },
        @{ Stage = "vertex"; Entry = "VSMain"; Name = "Vertex" },
        @{ Stage = "fragment"; Entry = "PSMain"; Name = "Fragment" }
    )
    $variants = @(
        @{ Destination = "DXIL"; Name = "Dxil"; Extension = "dxil" },
        @{ Destination = "SPIRV"; Name = "Spirv"; Extension = "spv" },
        @{ Destination = "MSL"; Name = "Msl"; Extension = "msl" }
    )
    foreach ($stage in $stages) {
        foreach ($variant in $variants) {
            $output = Join-Path $Temporary ("$($stage.Name)-$($variant.Name).$($variant.Extension)")
            & $Compiler $temporarySource --source HLSL --dest $variant.Destination --stage $stage.Stage `
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
        [IO.File]::WriteAllText($Header, $content, [Text.Encoding]::ASCII)
    }
}
finally {
    Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
}
