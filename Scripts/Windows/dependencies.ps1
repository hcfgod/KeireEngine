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
$WorkspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $Root -CommandName "dependencies"
try {

function Get-LockedDependencySource {
    param([string]$Name, [string]$Url, [string]$Commit)

    $sourceBase = Join-Path $env:LOCALAPPDATA "KeireDependencySources"
    $sourceContainerRoot = Split-Path $sourceBase -Parent
    $source = Join-Path $sourceBase "$Name-$Commit"
    if (Test-Path -LiteralPath $source) {
        Assert-KeireLockedGitSource -Path $source -ExpectedCommit $Commit -Name $Name
        return $source
    }

    New-Item -ItemType Directory -Force $sourceBase | Out-Null
    $cacheLock = Enter-KeireWorkspaceLock -RepositoryRoot $sourceBase `
        -CommandName "dependency-source-$Name-$Commit" -LockRelativePath ".locks\$Name-$Commit.lock"
    $temporary = Join-Path $sourceBase "$Name-$Commit.tmp-$PID"
    try {
        # A different worktree can publish the immutable checkout while this process waits for the shared cache lock.
        if (Test-Path -LiteralPath $source) {
            Assert-KeireLockedGitSource -Path $source -ExpectedCommit $Commit -Name $Name
            return $source
        }
        if (Get-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue) {
            Remove-KeireGeneratedDirectory -RepositoryRoot $sourceContainerRoot -AllowedRoot $sourceBase `
                -Path $temporary -Description "temporary dependency source"
        }
        & git clone --quiet --filter=blob:none --no-checkout $Url $temporary
        if ($LASTEXITCODE -ne 0) { throw "Could not clone $Name." }
        & git -C $temporary fetch --quiet --depth 1 origin $Commit
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch locked $Name commit $Commit." }
        & git -C $temporary checkout --quiet --detach $Commit
        if ($LASTEXITCODE -ne 0) { throw "Could not check out locked $Name commit $Commit." }
        Assert-KeireLockedGitSource -Path $temporary -ExpectedCommit $Commit -Name $Name
        Move-Item -LiteralPath $temporary -Destination $source
    }
    catch {
        $failure = $_
        if (Get-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue) {
            try {
                Remove-KeireGeneratedDirectory -RepositoryRoot $sourceContainerRoot -AllowedRoot $sourceBase `
                    -Path $temporary -Description "temporary dependency source"
            }
            catch {
                Write-Warning "Could not safely clean temporary dependency source '$temporary': $($_.Exception.Message)"
            }
        }
        throw $failure
    }
    finally {
        Exit-KeireWorkspaceLock -Lock $cacheLock
    }
    Assert-KeireLockedGitSource -Path $source -ExpectedCommit $Commit -Name $Name
    return $source
}

$joltSource = Get-LockedDependencySource "jolt" $Lock.JOLT_URL $Lock.JOLT_COMMIT
$recastSource = Get-LockedDependencySource "recast" $Lock.RECAST_URL $Lock.RECAST_COMMIT
$miniaudioSource = Get-LockedDependencySource "miniaudio" $Lock.MINIAUDIO_URL $Lock.MINIAUDIO_COMMIT
$sodiumSource = Get-LockedDependencySource "libsodium" $Lock.LIBSODIUM_URL $Lock.LIBSODIUM_COMMIT
$assimpSource = Join-Path $Root "Vendor\assimp"
# Locked Git checkouts above are immutable and safe to share. Assimp is a submodule inside this checkout, so its ASCII
# junction must also include the workspace identity or a linked worktree can collide with the primary checkout.
$assimpSourceLink = Get-KeireWorkspaceJunctionPath -BasePath (Join-Path $env:LOCALAPPDATA "KeireDependencySources") `
    -Prefix "assimp-$($Lock.ASSIMP_COMMIT)" -RepositoryRoot $Root
Initialize-KeireWorkspaceJunction -Path $assimpSourceLink -Target $assimpSource | Out-Null

$compiler = if ($Toolset -eq "clang") { (& clang++ --version | Select-Object -First 1) }
elseif ($Toolset -eq "gcc") { (& g++ --version | Select-Object -First 1) }
else { "MSVC $env:VCToolsVersion WindowsSDK $env:WindowsSDKVersion" }
$sourceLayoutIdentity = "workspace-assimp-v2:$assimpSourceLink"
$options = @(
    "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF", "-DSDL_AUDIO=OFF", "-DSDL_CAMERA=OFF", "-DSDL_JOYSTICK=ON",
    "-DSDL_HAPTIC=ON", "-DSDL_HIDAPI=ON", "-DSDL_HIDAPI_JOYSTICK=ON", "-DSDL_HIDAPI_LIBUSB=OFF",
    "-DSDL_VIRTUAL_JOYSTICK=ON", "-DSDL_SENSOR=OFF", "-DSDL_RENDER=OFF", "-DSDL_GPU=ON",
    "-DSDL_DUMMYVIDEO=ON", "-DSDL_OFFSCREEN=ON", "-DSDL_INSTALL=ON", "-DSDL_INSTALL_DOCS=OFF",
    "-DSDL_INSTALL_CMAKEDIR_ROOT=cmake", "-DSDL_DEPS_SHARED=ON",
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_INSTALL_LIBDIR=lib",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>DLL",
    "-DBUILD_SHARED_LIBS=OFF", "-DASSIMP_BUILD_TESTS=OFF", "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF",
    "-DASSIMP_BUILD_SAMPLES=OFF", "-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF",
    "-DASSIMP_BUILD_OBJ_IMPORTER=ON", "-DASSIMP_BUILD_FBX_IMPORTER=ON", "-DASSIMP_BUILD_GLTF_IMPORTER=ON",
    "-DASSIMP_NO_EXPORT=ON", "-DASSIMP_BUILD_ZLIB=ON",
    "-DASSIMP_BUILD_DRACO=OFF", "-DASSIMP_WARNINGS_AS_ERRORS=OFF", "-DASSIMP_INSTALL=ON",
    "-DASSIMP_INJECT_DEBUG_POSTFIX=OFF", "-DASSIMP_IGNORE_GIT_HASH=ON", "-DLIBRARY_SUFFIX=",
    "-DJPH_BUILD_SHARED_LIBS=OFF", "-DCPP_RTTI_ENABLED=ON", "-DENABLE_INSTALL=ON", "-DOVERRIDE_CXX_FLAGS=OFF",
    "-DINTERPROCEDURAL_OPTIMIZATION=OFF", "-DENABLE_ALL_WARNINGS=OFF",
    "-DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF", "-DUSE_SSE4_1=OFF", "-DUSE_SSE4_2=OFF",
    "-DUSE_AVX=OFF", "-DUSE_AVX2=OFF", "-DUSE_AVX512=OFF", "-DUSE_LZCNT=OFF",
    "-DUSE_TZCNT=OFF", "-DUSE_F16C=OFF", "-DUSE_FMADD=OFF",
    "-DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF", "-DPROFILER_IN_DEBUG_AND_RELEASE=OFF",
    "-DENABLE_OBJECT_STREAM=OFF", "-DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF", "-DJPH_USE_DX12=OFF",
    "-DJPH_USE_VK=OFF", "-DJPH_USE_MTL=OFF", "-DJPH_USE_CPU_COMPUTE=OFF",
    "-DRECASTNAVIGATION_DEMO=OFF", "-DRECASTNAVIGATION_TESTS=OFF", "-DRECASTNAVIGATION_EXAMPLES=OFF",
    "-DRECASTNAVIGATION_DT_POLYREF64=ON", "-DMINIAUDIO_BUILD_EXAMPLES=OFF", "-DMINIAUDIO_BUILD_TESTS=OFF",
    "-DMINIAUDIO_BUILD_TOOLS=OFF", "-DMINIAUDIO_NO_EXTRA_NODES=ON", "-DMINIAUDIO_NO_LIBVORBIS=ON",
    "-DMINIAUDIO_NO_LIBOPUS=ON", "-DMINIAUDIO_INSTALL=ON"
)

function Assert-SdlInputBackends {
    param([string]$Build, [string]$Configuration)

    $configurationName = $Configuration.ToLowerInvariant()
    $config = Join-Path $Build "SDL\include-config-$configurationName\build_config\SDL_build_config.h"
    if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
        throw "SDL $Configuration input-backend configuration is missing: $config"
    }
    $source = Get-Content -LiteralPath $config -Raw
    foreach ($macro in @("SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_VIRTUAL", "SDL_JOYSTICK_RAWINPUT", "SDL_HAPTIC_DINPUT")) {
        if (-not $source.Contains("#define $macro 1")) {
            throw "SDL $Configuration input-backend configuration is missing $macro."
        }
    }
    foreach ($macro in @("SDL_JOYSTICK_DISABLED", "SDL_HAPTIC_DISABLED", "SDL_HIDAPI_DISABLED", "SDL_LIBUSB_DYNAMIC")) {
        if ($source.Contains("#define $macro ")) {
            throw "SDL $Configuration input-backend configuration unexpectedly defines $macro."
        }
    }
}

$key = @($Lock.SDL_COMMIT, $Lock.ASSIMP_COMMIT, $Lock.JOLT_COMMIT, $Lock.RECAST_COMMIT,
    $Lock.MINIAUDIO_COMMIT, $Lock.LIBSODIUM_COMMIT, $sourceLayoutIdentity, $Architecture, $Toolset, $compiler, $bridgeHash,
    ($options -join ";")) -join "|"
$base = Join-Path $Root "Build\Dependencies\windows-$OutputArchitecture-$Toolset"
$zlibDebugName = if ($Toolset -eq "msc") { "zlibstaticd.lib" } else { "zlibstatic.lib" }

$sodiumBuild = Join-Path $base "libsodium"
$sodiumOutput = Join-Path $sodiumBuild "out\libsodium.dll"
$sodiumStamp = Join-Path $sodiumBuild "keire-libsodium.stamp"
$sodiumVisualStudio = if ($Generator -in @("vs2019", "vs2022", "vs2026")) { $Generator } else { "vs2022" }
$sodiumVisualStudioMajor = Get-VisualStudioMajorVersion $sodiumVisualStudio
$sodiumEnvironment = Get-VSBuildEnvironment $sodiumVisualStudioMajor
$sodiumToolsetVersion = Get-ChildItem (Join-Path $sodiumEnvironment.InstallationPath "VC\Tools\MSVC") -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
$sodiumKey = "$($Lock.LIBSODIUM_COMMIT)|$Architecture|$sodiumToolsetVersion"
$sodiumCurrent = -not $Force -and (Test-Path -LiteralPath $sodiumOutput -PathType Leaf) -and
    (Test-Path -LiteralPath $sodiumStamp -PathType Leaf) -and
    ((Get-Content -LiteralPath $sodiumStamp -Raw).Trim() -eq $sodiumKey)
if (-not $sodiumCurrent) {
    if (Test-Path -LiteralPath $sodiumBuild) {
        $resolvedSodiumBuild = [IO.Path]::GetFullPath($sodiumBuild)
        $resolvedBase = [IO.Path]::GetFullPath($base) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedSodiumBuild.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace the libsodium build outside $base."
        }
        Remove-Item -LiteralPath $resolvedSodiumBuild -Recurse -Force
    }
    $sodiumOutputDirectory = Split-Path $sodiumOutput
    $sodiumIntermediate = Join-Path $sodiumBuild "obj"
    New-Item -ItemType Directory -Force -Path $sodiumOutputDirectory, $sodiumIntermediate | Out-Null
    $sodiumProject = Join-Path $sodiumSource "builds\msvc\$sodiumVisualStudio\libsodium\libsodium.vcxproj"
    $sodiumPlatform = Get-MSBuildPlatform $Architecture
    Write-Host "==> Building pinned libsodium 1.0.22 runtime"
    & $sodiumEnvironment.MSBuild $sodiumProject "/m" "/t:Build" "/p:Configuration=ReleaseDLL" `
        "/p:Platform=$sodiumPlatform" "/p:OutDir=$sodiumOutputDirectory\" `
        "/p:IntDir=$sodiumIntermediate\" "/p:VCTargetsPath=$($sodiumEnvironment.VCTargetsPath)"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sodiumOutput -PathType Leaf)) {
        throw "Pinned libsodium runtime build failed."
    }
    [IO.File]::WriteAllText($sodiumStamp, "$sodiumKey`n", [Text.UTF8Encoding]::new($false))
}

function Install-SodiumRuntime {
    param([string]$Install)

    $runtimeDirectory = Join-Path $Install "bin"
    $licenseDirectory = Join-Path $Install "share\licenses\libsodium"
    New-Item -ItemType Directory -Force -Path $runtimeDirectory, $licenseDirectory | Out-Null
    Copy-Item -LiteralPath $sodiumOutput -Destination (Join-Path $runtimeDirectory "libsodium.dll") -Force
    Copy-Item -LiteralPath (Join-Path $sodiumSource "LICENSE") `
        -Destination (Join-Path $licenseDirectory "LICENSE") -Force
}

foreach ($configuration in @("Debug", "Release")) {
    $build = Join-Path $base $configuration
    $install = Join-Path $build "install"
    $library = Join-Path $install "lib\SDL3-static.lib"
    $assimpLibrary = Join-Path $install "lib\assimp.lib"
    $joltLibrary = Join-Path $install "lib\Jolt.lib"
    $recastSuffix = if ($configuration -eq "Debug") { "-d" } else { "" }
    $recastLibrary = Join-Path $install "lib\Recast$recastSuffix.lib"
    $detourLibrary = Join-Path $install "lib\Detour$recastSuffix.lib"
    $detourCrowdLibrary = Join-Path $install "lib\DetourCrowd$recastSuffix.lib"
    $detourTileCacheLibrary = Join-Path $install "lib\DetourTileCache$recastSuffix.lib"
    $miniaudioLibrary = Join-Path $install "lib\miniaudio.lib"
    $zlibName = if ($configuration -eq "Debug") { "lib\$zlibDebugName" } else { "lib\zlibstatic.lib" }
    $zlibLibrary = Join-Path $install $zlibName
    $sodiumRuntime = Join-Path $install "bin\libsodium.dll"
    $sodiumLicense = Join-Path $install "share\licenses\libsodium\LICENSE"
    $stamp = Join-Path $build "keire-dependency.stamp"
    $valid = -not $Force -and (Test-Path $library) -and (Test-Path $assimpLibrary) -and
        (Test-Path $joltLibrary) -and (Test-Path $recastLibrary) -and (Test-Path $detourLibrary) -and
        (Test-Path $detourCrowdLibrary) -and (Test-Path $detourTileCacheLibrary) -and
        (Test-Path $miniaudioLibrary) -and (Test-Path $sodiumRuntime) -and (Test-Path $sodiumLicense) -and
        (Test-Path $zlibLibrary) -and (Test-Path $stamp) -and
        ((Get-Content $stamp -Raw).Trim() -eq "$key|$configuration")
    if ($valid) {
        Assert-SdlInputBackends $build $configuration
        Write-Host "==> Native $configuration dependency cache is current"
        continue
    }

    if (Test-Path -LiteralPath $build) {
        $resolvedBuild = [IO.Path]::GetFullPath($build)
        $resolvedBase = [IO.Path]::GetFullPath($base) + [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedBuild.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace dependency cache outside $base."
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    Write-Host "==> Configuring native dependencies ($configuration)"
    & cmake -S (Join-Path $Root "Scripts\Dependencies") -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DKEIRE_SDL_SOURCE=$(Join-Path $Root 'Vendor\SDL')" `
        "-DKEIRE_ASSIMP_SOURCE=$assimpSourceLink" "-DKEIRE_JOLT_SOURCE=$joltSource" `
        "-DKEIRE_RECAST_SOURCE=$recastSource" "-DKEIRE_MINIAUDIO_SOURCE=$miniaudioSource" `
        "-DCMAKE_BUILD_TYPE=$configuration" `
        "-DCMAKE_INSTALL_PREFIX=$install" @options
    if ($LASTEXITCODE -ne 0) { throw "Native $configuration dependency configuration failed." }
    & cmake --build $build --target install --parallel
    if ($LASTEXITCODE -ne 0) { throw "Native $configuration dependency build failed." }
    Assert-SdlInputBackends $build $configuration
    Install-SodiumRuntime $install
    if (-not (Test-Path -LiteralPath $library) -or -not (Test-Path -LiteralPath $assimpLibrary) -or
        -not (Test-Path -LiteralPath $joltLibrary) -or -not (Test-Path -LiteralPath $recastLibrary) -or
        -not (Test-Path -LiteralPath $detourLibrary) -or -not (Test-Path -LiteralPath $detourCrowdLibrary) -or
        -not (Test-Path -LiteralPath $detourTileCacheLibrary) -or -not (Test-Path -LiteralPath $miniaudioLibrary) -or
        -not (Test-Path -LiteralPath $zlibLibrary) -or -not (Test-Path -LiteralPath $sodiumRuntime) -or
        -not (Test-Path -LiteralPath $sodiumLicense) -or
        -not (Test-Path -LiteralPath (Join-Path $install "include\assimp\Importer.hpp")) -or
        -not (Test-Path -LiteralPath (Join-Path $install "include\SDL3\SDL.h")) -or
        -not (Test-Path -LiteralPath (Join-Path $install "cmake\SDL3Config.cmake"))) {
        throw "Native $configuration dependency install is incomplete."
    }
    [IO.File]::WriteAllText($stamp, "$key|$configuration`n", [Text.UTF8Encoding]::new($false))
}

$forceFfmpegSourceBuild = $Force
foreach ($configuration in @("Debug", "Release")) {
    & (Join-Path $PSScriptRoot "ffmpeg.ps1") -Configuration $configuration -Architecture $Architecture `
        -Toolset $Toolset -Force:$forceFfmpegSourceBuild
    # Debug and Release intentionally publish the same optimized FFmpeg binary. Once the first configuration has
    # honored a forced rebuild, allow the second root to reuse that freshly validated result.
    $forceFfmpegSourceBuild = $false
}

& (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture -Toolset $Toolset -Force:$Force

$coralDebug = & (Join-Path $PSScriptRoot "coral.ps1") -Configuration Debug -Architecture $Architecture `
    -Build -Force:$Force
$coralRelease = & (Join-Path $PSScriptRoot "coral.ps1") -Configuration Release -Architecture $Architecture `
    -Build -Force:$Force
if ($coralDebug.Source -ne $coralRelease.Source -or $coralDebug.BuildVariant -ne $coralRelease.BuildVariant -or
    $coralDebug.NetHostLibrary -ne $coralRelease.NetHostLibrary -or
    $coralDebug.NetHostRuntime -ne $coralRelease.NetHostRuntime -or $coralDebug.DotnetRoot -ne $coralRelease.DotnetRoot) {
    throw "Coral Debug and Release metadata must resolve to one checkout-isolated build variant."
}

function Set-DependencyJunction {
    param([string]$Path, [string]$Target)
    if ([string]::IsNullOrWhiteSpace($Target) -or -not (Test-Path -LiteralPath $Target -PathType Container)) {
        throw "Dependency junction target is not an existing directory: $Target"
    }
    $Item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($Item) {
        $LinkTarget = [string]($Item.Target | Select-Object -First 1)
        if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
            -not [string]::IsNullOrWhiteSpace($LinkTarget) -and
            [IO.Path]::GetFullPath($LinkTarget) -eq [IO.Path]::GetFullPath($Target)) {
            return
        }
        $AllowedRoot = [IO.Path]::GetFullPath((Join-Path $Root "Build\Dependencies")) +
            [IO.Path]::DirectorySeparatorChar
        if (-not ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [IO.Path]::GetFullPath($Path).StartsWith($AllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Dependency junction points somewhere unexpected: $Path"
        }
        # Windows PowerShell 5 can throw an internal NullReferenceException while removing a directory junction with
        # Remove-Item. The checks above prove this exact path is a reparse point inside Build\Dependencies; a
        # non-recursive Directory.Delete removes the junction itself without traversing or deleting its target.
        if ($Item -is [IO.DirectoryInfo]) {
            [IO.Directory]::Delete($Item.FullName, $false)
        }
        else {
            # Git and WSL can leave a dangling Linux symlink represented as a reparse-point FileInfo.
            [IO.File]::Delete($Item.FullName)
        }
    }
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null
}

$coralLink = Join-Path $Root "Build\Dependencies\coral-patched"
$dotnetLink = Join-Path $Root "Build\Dependencies\coral-nethost"
$dotnetSdkLink = Join-Path $Root "Build\Dependencies\dotnet-sdk"
Set-DependencyJunction $coralLink $coralDebug.Source
Set-DependencyJunction $dotnetLink (Split-Path $coralDebug.NetHostLibrary)
Set-DependencyJunction $dotnetSdkLink $env:DOTNET_ROOT

$managedOutput = Join-Path $Root "Build\Managed"
$managedIntermediate = Join-Path $managedOutput "obj\"
New-Item -ItemType Directory -Force -Path $managedOutput | Out-Null
& (Join-Path $env:DOTNET_ROOT "dotnet.exe") build (Join-Path $Root "KeireManaged\Keire.Managed.csproj") `
    --configuration Release --output $managedOutput --nologo `
    "/p:BaseIntermediateOutputPath=$managedIntermediate"
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $managedOutput "Keire.Managed.dll"))) {
    throw "Keire.Managed API build failed."
}

$generated = Join-Path $Root "Build\Generated"
New-Item -ItemType Directory -Force -Path $generated | Out-Null
$debugInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Debug/install"
$releaseInstall = "../Build/Dependencies/windows-$OutputArchitecture-$Toolset/Release/install"
$manifest = @"
DependencyManifest = {
    MacOSDeploymentTarget = "$($Lock.MACOS_DEPLOYMENT_TARGET)",
    SDLCommit = "$($Lock.SDL_COMMIT)",
    JSONCommit = "$($Lock.JSON_COMMIT)",
    AssimpCommit = "$($Lock.ASSIMP_COMMIT)",
    JoltCommit = "$($Lock.JOLT_COMMIT)",
    RecastCommit = "$($Lock.RECAST_COMMIT)",
    MiniaudioCommit = "$($Lock.MINIAUDIO_COMMIT)",
    SodiumCommit = "$($Lock.LIBSODIUM_COMMIT)",
    CoralCommit = "$($coralDebug.Commit)",
    CoralPatchDigest = "$($coralDebug.PatchDigest)",
    CoralSource = "../Build/Dependencies/coral-patched",
    CoralInclude = "../Build/Dependencies/coral-patched/Coral.Native/Include",
    CoralDebugLibrary = "../Build/Dependencies/coral-patched/Build/Debug/Coral.Native.lib",
    CoralReleaseLibrary = "../Build/Dependencies/coral-patched/Build/Release/Coral.Native.lib",
    CoralManagedDebug = "../Build/Dependencies/coral-patched/Build/Debug",
    CoralManagedRelease = "../Build/Dependencies/coral-patched/Build/Release",
    CoralNetHostLibrary = "../Build/Dependencies/coral-nethost/nethost.lib",
    CoralNetHostRuntime = "../Build/Dependencies/coral-nethost/nethost.dll",
    SDL3Include = "$debugInstall/include",
    SDL3DebugLibrary = "$debugInstall/lib/SDL3-static.lib",
    SDL3ReleaseLibrary = "$releaseInstall/lib/SDL3-static.lib",
    AssimpInclude = "$debugInstall/include",
    AssimpDebugLibrary = "$debugInstall/lib/assimp.lib",
    AssimpReleaseLibrary = "$releaseInstall/lib/assimp.lib",
    AssimpZlibDebugLibrary = "$debugInstall/lib/$zlibDebugName",
    AssimpZlibReleaseLibrary = "$releaseInstall/lib/zlibstatic.lib",
    JoltInclude = "$debugInstall/include",
    JoltDebugLibrary = "$debugInstall/lib/Jolt.lib",
    JoltReleaseLibrary = "$releaseInstall/lib/Jolt.lib",
    RecastInclude = "$debugInstall/include/recastnavigation",
    RecastDebugLibraries = { "$debugInstall/lib/Recast-d.lib", "$debugInstall/lib/Detour-d.lib", "$debugInstall/lib/DetourCrowd-d.lib", "$debugInstall/lib/DetourTileCache-d.lib" },
    RecastReleaseLibraries = { "$releaseInstall/lib/Recast.lib", "$releaseInstall/lib/Detour.lib", "$releaseInstall/lib/DetourCrowd.lib", "$releaseInstall/lib/DetourTileCache.lib" },
    MiniaudioInclude = "$debugInstall/include/miniaudio",
    MiniaudioDebugLibrary = "$debugInstall/lib/miniaudio.lib",
    MiniaudioReleaseLibrary = "$releaseInstall/lib/miniaudio.lib",
    SodiumDebugRuntime = "$debugInstall/bin/libsodium.dll",
    SodiumReleaseRuntime = "$releaseInstall/bin/libsodium.dll",
    SodiumLicense = "$releaseInstall/share/licenses/libsodium/LICENSE",
    SDL3PlatformLinks = { "kernel32", "user32", "gdi32", "winmm", "imm32", "setupapi", "version", "ole32", "oleaut32", "shell32", "advapi32", "uuid", "hid", "mincore", "dinput8" }
}
"@
[IO.File]::WriteAllText((Join-Path $generated "Dependencies.lua"), $manifest, [Text.UTF8Encoding]::new($false))
Write-Host "==> Dependency manifest generated"
}
finally {
    Exit-KeireWorkspaceLock -Lock $WorkspaceLock
}
