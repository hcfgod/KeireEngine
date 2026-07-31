[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$TestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.TESTS_TARGET)\$($Project.TESTS_TARGET).exe"

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
    -Architecture $Architecture -Toolset $Toolset -Target $Project.TESTS_TARGET -CI:$CI -Update:$Update -Generate:$Generate
if (-not (Test-Path $TestsExe)) { throw "KeireTests executable was not found: $TestsExe" }

$originalPath = $env:PATH
$exitCode = 1
Push-Location $Root
try {
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -eq "msc")
    if ($Configuration -eq "DebugASan" -and $usesMSVC) {
        $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
        $runtimeDirectory = Get-MSVCASanRuntimeDirectory $majorVersion $Architecture
        Write-Host "==> Using MSVC AddressSanitizer runtime from $runtimeDirectory"
        $env:PATH = "$runtimeDirectory;$env:PATH"
    }
    Write-Host "==> Running KeireTests $Configuration for $Architecture"
    & $TestsExe
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0 -and $Configuration -in @("Debug", "DebugASan", "DebugUBSan", "DebugTSan")) {
        $probeOutput = Join-Path $env:TEMP ("core-assert-probe-" + [guid]::NewGuid().ToString("N") + ".txt")
        try {
            $probe = Start-Process -FilePath $TestsExe -ArgumentList "--core-assert-probe" -NoNewWindow -Wait `
                -PassThru -RedirectStandardError $probeOutput
            if ($probe.ExitCode -eq 0) { throw "Assertion probe unexpectedly succeeded." }
            $diagnostic = Get-Content $probeOutput -Raw
            if ($diagnostic -notmatch "Assertion failed: false" -or $diagnostic -notmatch "assertion probe") {
                throw "Assertion probe did not emit the required diagnostic."
            }
        }
        finally { Remove-Item $probeOutput -Force -ErrorAction SilentlyContinue }
    }
}
finally {
    $env:PATH = $originalPath
    Pop-Location
}

if ($exitCode -eq 0) {
    $editorTestsTarget = "$($Project.PROJECT_NAMESPACE)EditorTests"
    $editorTestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$editorTestsTarget\$editorTestsTarget.exe"
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
        -Architecture $Architecture -Toolset $Toolset -Target $editorTestsTarget -CI:$CI -Update:$Update -Generate:$Generate
    if (-not (Test-Path $editorTestsExe)) { throw "Editor tests executable was not found: $editorTestsExe" }
    $editorOriginalPath = $env:PATH
    try {
        if ($Configuration -eq "DebugASan" -and $usesMSVC) {
            $env:PATH = "$runtimeDirectory;$env:PATH"
        }
        Write-Host "==> Running editor document and controller tests"
        & $editorTestsExe
        if ($LASTEXITCODE -ne 0) { throw "Editor tests failed with exit code $LASTEXITCODE." }
    }
    finally {
        $env:PATH = $editorOriginalPath
    }
}

if ($exitCode -eq 0) {
    Write-Host "==> Building complete client compile gate"
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
        -Architecture $Architecture -Toolset $Toolset -Target $Project.CLIENT_TARGET -CI:$CI -Update:$Update `
        -Generate:$Generate
}

if ($exitCode -eq 0 -and $Configuration -in @("Debug", "Release")) {
    $renderTestsTarget = "$($Project.PROJECT_NAMESPACE)RenderTests"
    $renderTestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$renderTestsTarget\$renderTestsTarget.exe"
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
        -Architecture $Architecture -Toolset $Toolset -Target $renderTestsTarget -CI:$CI -Update:$Update -Generate:$Generate
    if (-not (Test-Path $renderTestsExe)) { throw "GPU render tests executable was not found: $renderTestsExe" }

    $previousVideoDriver = $env:SDL_VIDEODRIVER
    $previousGpuBackend = $env:KEIRE_GPU_TEST_BACKEND
    try {
        Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue
        foreach ($backend in @("direct3d12", "vulkan")) {
            $env:KEIRE_GPU_TEST_BACKEND = $backend
            & $renderTestsExe --probe
            $gpuProbeExitCode = $LASTEXITCODE
            if ($gpuProbeExitCode -eq 77) {
                Write-Host "==> GPU render tests skipped: $backend is unavailable"
                $required = $env:KEIRE_REQUIRE_GPU_TESTS
                if ($required -and ($required -in @("1", "all") -or $backend -in ($required -split ','))) {
                    throw "Required GPU test backend is unavailable: $backend"
                }
                continue
            }
            elseif ($gpuProbeExitCode -ne 0) {
                throw "GPU render test probe failed for $backend with exit code $gpuProbeExitCode."
            }

            Write-Host "==> Running GPU render tests with $backend"
            & $renderTestsExe
            $gpuExitCode = $LASTEXITCODE
            if ($gpuExitCode -ne 0) {
                throw "GPU render tests failed for $backend with exit code $gpuExitCode."
            }
        }
    }
    finally {
        $env:SDL_VIDEODRIVER = $previousVideoDriver
        $env:KEIRE_GPU_TEST_BACKEND = $previousGpuBackend
    }
}
exit $exitCode
