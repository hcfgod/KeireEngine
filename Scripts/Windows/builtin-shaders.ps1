$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Compiler = Join-Path $Root "Build\Tools\ShaderCompiler\KeireShaderCompiler.exe"
$Sources = @(
    @{ Prefix = "BuiltinUnlit"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinUnlit.hlsl") },
    @{ Prefix = "BuiltinSky"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinSky.hlsl") },
    @{ Prefix = "BuiltinGrid"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinGrid.hlsl") },
    @{ Prefix = "BuiltinShadow"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinShadow.hlsl") },
    @{ Prefix = "BuiltinToneMap"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinToneMap.hlsl") },
    @{ Prefix = "BuiltinRuntimeUi"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinRuntimeUi.hlsl") }
)
$Generated = Join-Path $Root "Build\Generated\Keire\BuiltinUnlitShaders.h"
$Stamp = Join-Path $Root "Build\Generated\Keire\BuiltinRenderingShaders.stamp"
$CacheHelper = Join-Path $PSScriptRoot "generated-content-cache.ps1"
. $CacheHelper

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "KeireShaderCompiler is required before generating built-in rendering shaders."
}

$FingerprintInputs = @($PSCommandPath, $CacheHelper, $Compiler) + @($Sources | ForEach-Object { $_.Path })
$Fingerprint = Get-GeneratedContentFingerprint -Schema "builtin-rendering-v1" -Inputs $FingerprintInputs
if (Test-GeneratedContentCurrent -Output $Generated -Stamp $Stamp -Fingerprint $Fingerprint) {
    return
}

$CacheLock = Enter-GeneratedContentLock -Name "builtin-rendering" -RepositoryRoot $Root
$Temporary = $null
try {
    if (Test-GeneratedContentCurrent -Output $Generated -Stamp $Stamp -Fingerprint $Fingerprint) {
        return
    }

    $Temporary = Join-Path ([IO.Path]::GetTempPath()) ("KeireBuiltinRendering-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $Temporary | Out-Null
    $Variants = @(
        @{ Name = "VertexDxil"; Stage = "vertex"; Entry = "VSMain"; Destination = "DXIL"; File = "vertex.dxil" },
        @{ Name = "FragmentDxil"; Stage = "fragment"; Entry = "PSMain"; Destination = "DXIL"; File = "fragment.dxil" },
        @{ Name = "VertexSpirV"; Stage = "vertex"; Entry = "VSMain"; Destination = "SPIRV"; File = "vertex.spv" },
        @{ Name = "FragmentSpirV"; Stage = "fragment"; Entry = "PSMain"; Destination = "SPIRV"; File = "fragment.spv" },
        @{ Name = "VertexMsl"; Stage = "vertex"; Entry = "VSMain"; Destination = "MSL"; File = "vertex.metal" },
        @{ Name = "FragmentMsl"; Stage = "fragment"; Entry = "PSMain"; Destination = "MSL"; File = "fragment.metal" }
    )

    foreach ($Source in $Sources) {
        $StagedSource = Join-Path $Temporary "$($Source.Prefix).hlsl"
        Copy-Item -LiteralPath $Source.Path -Destination $StagedSource
        foreach ($Variant in $Variants) {
            $Output = Join-Path $Temporary "$($Source.Prefix)-$($Variant.File)"
            & $Compiler $StagedSource -s HLSL -d $Variant.Destination -t $Variant.Stage -e $Variant.Entry -o $Output
            if ($LASTEXITCODE -ne 0) {
                throw "$($Source.Prefix) $($Variant.Name) shader compilation failed."
            }
        }
    }

    $Builder = [Text.StringBuilder]::new()
    [void]$Builder.AppendLine("#pragma once")
    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine("namespace Keire::Detail")
    [void]$Builder.AppendLine("{")
    foreach ($Source in $Sources) {
        foreach ($Variant in $Variants) {
            $Bytes = [IO.File]::ReadAllBytes((Join-Path $Temporary "$($Source.Prefix)-$($Variant.File)"))
            [void]$Builder.AppendLine("    inline constexpr unsigned char $($Source.Prefix)$($Variant.Name)[] = {")
            for ($Index = 0; $Index -lt $Bytes.Length; $Index += 16) {
                $End = [Math]::Min($Index + 15, $Bytes.Length - 1)
                $Values = for ($Byte = $Index; $Byte -le $End; ++$Byte) { "0x{0:x2}" -f $Bytes[$Byte] }
                [void]$Builder.Append("        ")
                [void]$Builder.Append(($Values -join ", "))
                [void]$Builder.AppendLine(",")
            }
            [void]$Builder.AppendLine("    };")
            [void]$Builder.AppendLine("    inline constexpr unsigned int $($Source.Prefix)$($Variant.Name)Size = $($Bytes.Length);")
        }
    }
    [void]$Builder.AppendLine("} // namespace Keire::Detail")

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Generated) | Out-Null
    $Content = $Builder.ToString()
    $Existing = if (Test-Path -LiteralPath $Generated) { [IO.File]::ReadAllText($Generated) } else { $null }
    if ($Existing -cne $Content) {
        $TemporaryHeader = Join-Path $Temporary "BuiltinUnlitShaders.h"
        [IO.File]::WriteAllText($TemporaryHeader, $Content, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $TemporaryHeader -Destination $Generated -Force
    }
    Write-GeneratedContentStamp -Stamp $Stamp -Fingerprint $Fingerprint
}
finally {
    if ($Temporary) {
        Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
    }
    Exit-GeneratedContentLock -Mutex $CacheLock
}
