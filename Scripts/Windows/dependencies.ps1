[CmdletBinding()]
param(
    [string]$Generator = "vs2022",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$OutputArchitecture = Get-ArchitectureOutputName $Architecture
$Ninja = Get-NinjaExecutable
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$Bridge = Join-Path $Root "Scripts\Dependencies\CMakeLists.txt"
$bridgeHash = (Get-FileHash -Algorithm SHA256 $Bridge).Hash.ToLowerInvariant()

$compiler = if ($Toolset -eq "clang") { (& clang++ --version | Select-Object -First 1) }
elseif ($Toolset -eq "gcc") { (& g++ --version | Select-Object -First 1) }
else { "MSVC $env:VCToolsVersion WindowsSDK $env:WindowsSDKVersion" }
$options = @(
    "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF", "-DSDL_AUDIO=OFF", "-DSDL_CAMERA=OFF", "-DSDL_JOYSTICK=OFF",
    "-DSDL_HAPTIC=OFF", "-DSDL_SENSOR=OFF", "-DSDL_RENDER=OFF", "-DSDL_GPU=ON",
    "-DSDL_DUMMYVIDEO=ON", "-DSDL_OFFSCREEN=ON", "-DSDL_INSTALL=ON", "-DSDL_INSTALL_DOCS=OFF",
    "-DSDL_DEPS_SHARED=ON", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_INSTALL_LIBDIR=lib",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>DLL"
)
$key = @($Lock.SDL_COMMIT, $Architecture, $Toolset, $compiler, $bridgeHash, ($options -join ";")) -join "|"
$base = Join-Path $Root "Build\Dependencies\windows-$OutputArchitecture-$Toolset"

foreach ($configuration in @("Debug", "Release")) {
    $build = Join-Path $base $configuration
    $install = Join-Path $build "install"
    $library = Join-Path $install "lib\SDL3-static.lib"
    $stamp = Join-Path $build "keire-dependency.stamp"
    $valid = -not $Force -and (Test-Path $library) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq "$key|$configuration")
    if ($valid) { Write-Host "==> SDL $configuration dependency cache is current"; continue }

    if (Test-Path -LiteralPath $build) {
        $resolvedBuild = [IO.Path]::GetFullPath($build)
        $resolvedBase = [IO.Path]::GetFullPath($base) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedBuild.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace dependency cache outside $base."
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    Write-Host "==> Configuring SDL 3.4.10 ($configuration)"
    & cmake -S (Join-Path $Root "Scripts\Dependencies") -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DKEIRE_SDL_SOURCE=$(Join-Path $Root 'Vendor\SDL')" "-DCMAKE_BUILD_TYPE=$configuration" `
        "-DCMAKE_INSTALL_PREFIX=$install" @options
    if ($LASTEXITCODE -ne 0) { throw "SDL $configuration configuration failed." }
    & cmake --build $build --target install --parallel
    if ($LASTEXITCODE -ne 0) { throw "SDL $configuration build failed." }
    if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath (Join-Path $install "include\SDL3\SDL.h")) -or
        -not (Test-Path -LiteralPath (Join-Path $install "cmake\SDL3Config.cmake"))) {
        throw "SDL $configuration install did not produce its archive, headers, and official CMake configuration."
    }
    [IO.File]::WriteAllText($stamp, "$key|$configuration`n", [Text.UTF8Encoding]::new($false))
}

$generated = Join-Path $Root "Build\Generated"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$debugInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Debug/install"
$releaseInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Release/install"
$manifest = @"
DependencyManifest = {
    SDLCommit = "$($Lock.SDL_COMMIT)",
    JSONCommit = "$($Lock.JSON_COMMIT)",
    SDL3Include = "$debugInstall/include",
    SDL3DebugLibrary = "$debugInstall/lib/SDL3-static.lib",
    SDL3ReleaseLibrary = "$releaseInstall/lib/SDL3-static.lib",
    SDL3PlatformLinks = { "kernel32", "user32", "gdi32", "winmm", "imm32", "setupapi", "version", "ole32", "oleaut32", "shell32", "advapi32", "uuid" }
}
"@
[IO.File]::WriteAllText((Join-Path $generated "Dependencies.lua"), $manifest, [Text.UTF8Encoding]::new($false))
Write-Host "==> Dependency manifest generated"
