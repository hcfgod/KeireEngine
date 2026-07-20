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
$assimpSource = Join-Path $Root "Vendor\assimp"
$assimpSourceLink = Join-Path $env:LOCALAPPDATA "KeireDependencySources\assimp-$($Lock.ASSIMP_COMMIT)"
if (Test-Path -LiteralPath $assimpSourceLink) {
    $link = Get-Item -LiteralPath $assimpSourceLink -Force
    $linkTarget = [string]($link.Target | Select-Object -First 1)
    if (-not ($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        [IO.Path]::GetFullPath($linkTarget) -ne [IO.Path]::GetFullPath($assimpSource)) {
        throw "Assimp dependency source cache points somewhere unexpected: $assimpSourceLink"
    }
}
else {
    New-Item -ItemType Directory -Force (Split-Path $assimpSourceLink) | Out-Null
    New-Item -ItemType Junction -Path $assimpSourceLink -Target $assimpSource | Out-Null
}

$compiler = if ($Toolset -eq "clang") { (& clang++ --version | Select-Object -First 1) }
elseif ($Toolset -eq "gcc") { (& g++ --version | Select-Object -First 1) }
else { "MSVC $env:VCToolsVersion WindowsSDK $env:WindowsSDKVersion" }
$options = @(
    "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF", "-DSDL_AUDIO=OFF", "-DSDL_CAMERA=OFF", "-DSDL_JOYSTICK=OFF",
    "-DSDL_HAPTIC=OFF", "-DSDL_SENSOR=OFF", "-DSDL_RENDER=OFF", "-DSDL_GPU=ON",
    "-DSDL_DUMMYVIDEO=ON", "-DSDL_OFFSCREEN=ON", "-DSDL_INSTALL=ON", "-DSDL_INSTALL_DOCS=OFF",
    "-DSDL_DEPS_SHARED=ON", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_INSTALL_LIBDIR=lib",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>DLL",
    "-DBUILD_SHARED_LIBS=OFF", "-DASSIMP_BUILD_TESTS=OFF", "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF",
    "-DASSIMP_BUILD_SAMPLES=OFF", "-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF",
    "-DASSIMP_BUILD_OBJ_IMPORTER=ON", "-DASSIMP_BUILD_FBX_IMPORTER=ON", "-DASSIMP_BUILD_GLTF_IMPORTER=ON",
    "-DASSIMP_NO_EXPORT=ON", "-DASSIMP_BUILD_ZLIB=ON",
    "-DASSIMP_BUILD_DRACO=OFF", "-DASSIMP_WARNINGS_AS_ERRORS=OFF", "-DASSIMP_INSTALL=ON",
    "-DASSIMP_INJECT_DEBUG_POSTFIX=OFF", "-DASSIMP_IGNORE_GIT_HASH=ON", "-DLIBRARY_SUFFIX="
)
$key = @($Lock.SDL_COMMIT, $Lock.ASSIMP_COMMIT, $Architecture, $Toolset, $compiler, $bridgeHash,
    ($options -join ";")) -join "|"
$base = Join-Path $Root "Build\Dependencies\windows-$OutputArchitecture-$Toolset"

foreach ($configuration in @("Debug", "Release")) {
    $build = Join-Path $base $configuration
    $install = Join-Path $build "install"
    $library = Join-Path $install "lib\SDL3-static.lib"
    $assimpLibrary = Join-Path $install "lib\assimp.lib"
    $zlibName = if ($configuration -eq "Debug") { "lib\zlibstaticd.lib" } else { "lib\zlibstatic.lib" }
    $zlibLibrary = Join-Path $install $zlibName
    $stamp = Join-Path $build "keire-dependency.stamp"
    $valid = -not $Force -and (Test-Path $library) -and (Test-Path $assimpLibrary) -and
        (Test-Path $zlibLibrary) -and (Test-Path $stamp) -and
        ((Get-Content $stamp -Raw).Trim() -eq "$key|$configuration")
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
    Write-Host "==> Configuring SDL and Assimp ($configuration)"
    & cmake -S (Join-Path $Root "Scripts\Dependencies") -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DKEIRE_SDL_SOURCE=$(Join-Path $Root 'Vendor\SDL')" `
        "-DKEIRE_ASSIMP_SOURCE=$assimpSourceLink" "-DCMAKE_BUILD_TYPE=$configuration" `
        "-DCMAKE_INSTALL_PREFIX=$install" @options
    if ($LASTEXITCODE -ne 0) { throw "SDL/Assimp $configuration configuration failed." }
    & cmake --build $build --target install --parallel
    if ($LASTEXITCODE -ne 0) { throw "SDL/Assimp $configuration build failed." }
    if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath $assimpLibrary) -or
        -not (Test-Path -LiteralPath $zlibLibrary) -or
        -not (Test-Path -LiteralPath (Join-Path $install "include\assimp\Importer.hpp")) -or
        -not (Test-Path -LiteralPath (Join-Path $install "include\SDL3\SDL.h")) -or
        -not (Test-Path -LiteralPath (Join-Path $install "cmake\SDL3Config.cmake"))) {
        throw "SDL/Assimp $configuration install is incomplete."
    }
    [IO.File]::WriteAllText($stamp, "$key|$configuration`n", [Text.UTF8Encoding]::new($false))
}

& (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture -Toolset $Toolset -Force:$Force

$generated = Join-Path $Root "Build\Generated"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$debugInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Debug/install"
$releaseInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Release/install"
$manifest = @"
DependencyManifest = {
    SDLCommit = "$($Lock.SDL_COMMIT)",
    JSONCommit = "$($Lock.JSON_COMMIT)",
    AssimpCommit = "$($Lock.ASSIMP_COMMIT)",
    SDL3Include = "$debugInstall/include",
    SDL3DebugLibrary = "$debugInstall/lib/SDL3-static.lib",
    SDL3ReleaseLibrary = "$releaseInstall/lib/SDL3-static.lib",
    AssimpInclude = "$debugInstall/include",
    AssimpDebugLibrary = "$debugInstall/lib/assimp.lib",
    AssimpReleaseLibrary = "$releaseInstall/lib/assimp.lib",
    AssimpZlibDebugLibrary = "$debugInstall/lib/zlibstaticd.lib",
    AssimpZlibReleaseLibrary = "$releaseInstall/lib/zlibstatic.lib",
    SDL3PlatformLinks = { "kernel32", "user32", "gdi32", "winmm", "imm32", "setupapi", "version", "ole32", "oleaut32", "shell32", "advapi32", "uuid" }
}
"@
[IO.File]::WriteAllText((Join-Path $generated "Dependencies.lua"), $manifest, [Text.UTF8Encoding]::new($false))
Write-Host "==> Dependency manifest generated"
