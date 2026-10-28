$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Compiler = Join-Path $Root "Build\Tools\ShaderCompiler\KeireShaderCompiler.exe"
$Sources = @(
    @{ Prefix = "BuiltinUnlit"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinUnlit.hlsl") },
    @{ Prefix = "BuiltinSky"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinSky.hlsl") },
    @{ Prefix = "BuiltinCloudVolume"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinCloudVolume.hlsl") },
    @{ Prefix = "BuiltinCloudTrace"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinCloudTrace.hlsl") },
    @{ Prefix = "BuiltinCloudResolve"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinCloudResolve.hlsl") },
    @{ Prefix = "BuiltinCloudComposite"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinCloudComposite.hlsl") },
    @{ Prefix = "BuiltinCloudLightCache"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinCloudLightCache.hlsl"); Compute = $true },
    @{ Prefix = "BuiltinGrid"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinGrid.hlsl") },
    @{ Prefix = "BuiltinDepthVelocity"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinDepthVelocity.hlsl") },
    @{ Prefix = "BuiltinShadow"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinShadow.hlsl") },
    @{ Prefix = "BuiltinToneMap"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinToneMap.hlsl") },
    @{ Prefix = "BuiltinBloom"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinBloom.hlsl") },
    @{ Prefix = "BuiltinRuntimeUi"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinRuntimeUi.hlsl") },
    @{ Prefix = "BuiltinDeferredGBuffer"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinDeferredGBuffer.hlsl") },
    @{ Prefix = "BuiltinDeferredLighting"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinDeferredLighting.hlsl") },
    @{ Prefix = "BuiltinIrradyn"; Path = (Join-Path $Root "KeireCore\Shaders\BuiltinIrradyn.hlsl") }
)
$Generated = Join-Path $Root "Build\Generated\Keire\BuiltinUnlitShaders.h"
$GeneratedSource = Join-Path $Root "Build\Generated\Keire\CloudVolumeSource.h"
$Includes = @((Join-Path $Root "KeireCore\Shaders\BuiltinLighting.hlsli"),
             (Join-Path $Root "KeireCore\Shaders\GridAppearance.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\AtmosphereMedium.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\AtmosphereGround.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudLayered.hlsl"),
              (Join-Path $Root "KeireCore\Shaders\CloudVolume.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudLightCacheSampling.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudTemporalRejection.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudRadianceEncoding.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\DisplayTransfer.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudVolumeGeometry.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\FogDensity.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\FogExclusion.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\FogTerrainShadow.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\FogRayIntervals.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudVolumeLighting.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudVolumeDensity.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudWeather.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\SkyColorGradient.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\CloudVolumeView.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\VolumeSurfaceComposition.hlsli"),
              (Join-Path $Root "KeireCore\Shaders\VolumeTransport.hlsli"))
$Stamp = Join-Path $Root "Build\Generated\Keire\BuiltinRenderingShaders.stamp"
$CacheHelper = Join-Path $PSScriptRoot "generated-content-cache.ps1"
. $CacheHelper

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "KeireShaderCompiler is required before generating built-in rendering shaders."
}

$FingerprintInputs = @($PSCommandPath, $CacheHelper, $Compiler) + @($Sources | ForEach-Object { $_.Path }) + $Includes
$Fingerprint = Get-GeneratedContentFingerprint -Schema "builtin-rendering-v2" -Inputs $FingerprintInputs
if ((Test-Path -LiteralPath $GeneratedSource -PathType Leaf) -and
    (Test-GeneratedContentCurrent -Output $Generated -Stamp $Stamp -Fingerprint $Fingerprint)) {
    return
}

$CacheLock = Enter-GeneratedContentLock -Name "builtin-rendering" -RepositoryRoot $Root
$Temporary = $null
try {
    if ((Test-Path -LiteralPath $GeneratedSource -PathType Leaf) -and
        (Test-GeneratedContentCurrent -Output $Generated -Stamp $Stamp -Fingerprint $Fingerprint)) {
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

    $ComputeVariants = @(
        @{ Name = "ComputeDxil"; Stage = "compute"; Entry = "CSMain"; Destination = "DXIL"; File = "compute.dxil" },
        @{ Name = "ComputeSpirV"; Stage = "compute"; Entry = "CSMain"; Destination = "SPIRV"; File = "compute.spv" },
        @{ Name = "ComputeMsl"; Stage = "compute"; Entry = "CSMain"; Destination = "MSL"; File = "compute.metal" }
    )

    foreach ($Include in $Includes) {
        Copy-Item -LiteralPath $Include -Destination $Temporary
    }
    foreach ($Source in $Sources) {
        $StagedSource = Join-Path $Temporary "$($Source.Prefix).hlsl"
        Copy-Item -LiteralPath $Source.Path -Destination $StagedSource
        $SourceVariants = if ($Source.ContainsKey('Compute')) { $ComputeVariants } else { $Variants }
        foreach ($Variant in $SourceVariants) {
            $Output = Join-Path $Temporary "$($Source.Prefix)-$($Variant.File)"
            $CompilerArguments = @($StagedSource, '-s', 'HLSL', '-d', $Variant.Destination, '-t', $Variant.Stage,
                                   '-e', $Variant.Entry, '-I', $Temporary, '-o', $Output)
            & $Compiler @CompilerArguments
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
        $SourceVariants = if ($Source.ContainsKey('Compute')) { $ComputeVariants } else { $Variants }
        foreach ($Variant in $SourceVariants) {
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
    # Generated material shaders must also work without a source checkout in installed editors.
    $SourceBuilder = [Text.StringBuilder]::new()
    [void]$SourceBuilder.Append("#pragma once`n`nnamespace Keire::Detail`n{`n")
    [void]$SourceBuilder.Append('    inline constexpr char CloudVolumeShaderSource[] = "\n"').Append("`n")
    foreach ($SourceName in @('VolumeTransport', 'AtmosphereMedium', 'CloudVolumeGeometry', 'FogDensity', 'FogExclusion', 'FogRayIntervals', 'CloudVolumeLighting', 'CloudVolumeDensity', 'CloudWeather', 'CloudVolume',
                              'VolumeSurfaceComposition', 'CloudVolumeView')) {
        $SourcePath = Join-Path $Root "KeireCore\Shaders\$SourceName.hlsli"
        foreach ($SourceLine in [IO.File]::ReadAllLines($SourcePath)) {
            if ($SourceLine -match '^\s*#\s*(pragma\s+once|include)\b') {
                continue
            }
            if ($SourceLine.Contains(')KEIRE_CLOUD"')) {
                throw "Cloud source contains the reserved raw string delimiter."
            }
            # Adjacent per-line literals avoid MSVC's single-literal limit while preserving the shader bytes.
            if ([Text.Encoding]::UTF8.GetByteCount($SourceLine) -gt 8192) {
                throw "Cloud source line exceeds the portable embedded literal limit."
            }
            [void]$SourceBuilder.Append('R"KEIRE_CLOUD(').Append($SourceLine).Append("`n")
            [void]$SourceBuilder.Append(')KEIRE_CLOUD"').Append("`n")
        }
    }
    [void]$SourceBuilder.Append(';')
    [void]$SourceBuilder.Append("`n} // namespace Keire::Detail`n")
    $SourceContent = $SourceBuilder.ToString()
    $ExistingSource = if (Test-Path -LiteralPath $GeneratedSource) {
        [IO.File]::ReadAllText($GeneratedSource)
    } else { $null }
    if ($ExistingSource -cne $SourceContent) {
        $TemporarySource = Join-Path $Temporary "CloudVolumeSource.h"
        [IO.File]::WriteAllText($TemporarySource, $SourceContent, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $TemporarySource -Destination $GeneratedSource -Force
    }
    Write-GeneratedContentStamp -Stamp $Stamp -Fingerprint $Fingerprint
}
finally {
    if ($Temporary) {
        Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
    }
    Exit-GeneratedContentLock -Mutex $CacheLock
}
