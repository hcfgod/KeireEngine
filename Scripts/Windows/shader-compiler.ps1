[CmdletBinding()]
param(
    [string]$Generator = "ninja",
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

$sdlInstall = Join-Path $Root "Build\Dependencies\windows-$OutputArchitecture-$Toolset\Release\install"
if (-not (Test-Path (Join-Path $sdlInstall "cmake\SDL3Config.cmake"))) {
    throw "The Release SDL dependency must be built before KeireShaderCompiler."
}

$cacheRoot = Join-Path $Root "Build\Tools\ShaderCompiler\Cache\windows-$OutputArchitecture-$Toolset"
$installRoot = Join-Path $cacheRoot "install"
$publishedRoot = Join-Path $Root "Build\Tools\ShaderCompiler"
$publishedCompiler = Join-Path $publishedRoot "KeireShaderCompiler.exe"
$stamp = Join-Path $cacheRoot "keire-shader-compiler.stamp"
$configureStamp = Join-Path $cacheRoot "keire-shader-compiler.configure"
$asciiRoot = Join-Path ([IO.Path]::GetTempPath()) "KeireShaderCompilerWorkspace"
if (Test-Path -LiteralPath $asciiRoot) {
    $junction = Get-Item -LiteralPath $asciiRoot -Force
    $target = @($junction.Target)[0]
    if (-not $target -or [IO.Path]::GetFullPath($target) -ne [IO.Path]::GetFullPath($Root)) {
        throw "The shader compiler ASCII workspace exists but targets another location: $asciiRoot"
    }
}
else {
    New-Item -ItemType Junction -Path $asciiRoot -Target $Root | Out-Null
}
$cmakeCacheRoot = Join-Path $asciiRoot "Build\Tools\ShaderCompiler\Cache\windows-$OutputArchitecture-$Toolset"
$cmakeInstallRoot = Join-Path $cmakeCacheRoot "install"
$cmakeSdlInstall = Join-Path $asciiRoot "Build\Dependencies\windows-$OutputArchitecture-$Toolset\Release\install"
$hostToolset = "msc"
Enter-WindowsToolEnvironment "ninja" $hostToolset $Architecture | Out-Null
$compilerIdentity = "MSVC $env:VCToolsVersion WindowsSDK $env:WindowsSDKVersion"
$key = @($Lock.SDL_SHADERCROSS_COMMIT, $Lock.SDL_SHADERCROSS_DXC_COMMIT,
    $Lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT, $Lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT,
    $Lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT, $Lock.SDL_COMMIT, $Architecture, $Toolset,
    $compilerIdentity) -join "|"

$valid = -not $Force -and (Test-Path $publishedCompiler) -and (Test-Path $stamp) -and
    ((Get-Content $stamp -Raw).Trim() -eq $key)
if ($valid) {
    Write-Host "==> KeireShaderCompiler cache is current"
    return
}

if (Test-Path -LiteralPath $cacheRoot) {
    $resolved = [IO.Path]::GetFullPath($cacheRoot)
    $allowed = [IO.Path]::GetFullPath((Join-Path $Root "Build\Tools\ShaderCompiler\Cache")) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace shader compiler cache outside $allowed."
    }
    $configuredKey = if (Test-Path $configureStamp) { (Get-Content $configureStamp -Raw).Trim() } else { "" }
    $mustReplace = $Force -or -not (Test-Path (Join-Path $cacheRoot "CMakeCache.txt")) -or
        -not $configuredKey -or $configuredKey -ne $key
    if ($mustReplace) { Remove-Item -LiteralPath $cacheRoot -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null

$options = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$cmakeInstallRoot",
    "-DCMAKE_PREFIX_PATH=$cmakeSdlInstall",
    "-DCMAKE_C_COMPILER=cl.exe",
    "-DCMAKE_CXX_COMPILER=cl.exe",
    "-DSDLSHADERCROSS_VENDORED=ON",
    "-DSDLSHADERCROSS_DXC=ON",
    "-DSDLSHADERCROSS_SHARED=OFF",
    "-DSDLSHADERCROSS_STATIC=ON",
    "-DSDLSHADERCROSS_SPIRVCROSS_SHARED=OFF",
    "-DSDLSHADERCROSS_CLI=ON",
    "-DSDLSHADERCROSS_CLI_STATIC=ON",
    "-DSDLSHADERCROSS_TESTS=OFF",
    "-DSDLSHADERCROSS_INSTALL=ON",
    "-DSDLSHADERCROSS_INSTALL_RUNTIME=ON",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
)
Write-Host "==> Configuring the pinned host shader compiler"
& cmake -S (Join-Path $asciiRoot "Vendor\SDL_shadercross") -B $cmakeCacheRoot -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$Ninja" @options
if ($LASTEXITCODE -ne 0) { throw "KeireShaderCompiler configuration failed." }
[IO.File]::WriteAllText($configureStamp, "$key`n", [Text.UTF8Encoding]::new($false))
& cmake --build $cmakeCacheRoot --target install --parallel
if ($LASTEXITCODE -ne 0) { throw "KeireShaderCompiler build failed." }

$builtCompiler = Get-ChildItem $installRoot -Filter shadercross.exe -File -Recurse | Select-Object -First 1
if (-not $builtCompiler) { throw "SDL_shadercross did not install its command-line compiler." }
New-Item -ItemType Directory -Force -Path $publishedRoot | Out-Null
Copy-Item $builtCompiler.FullName $publishedCompiler -Force
foreach ($runtime in Get-ChildItem $installRoot -Filter *.dll -File -Recurse) {
    Copy-Item $runtime.FullName (Join-Path $publishedRoot $runtime.Name) -Force
}
[IO.File]::WriteAllText($stamp, "$key`n", [Text.UTF8Encoding]::new($false))
Write-Host "==> KeireShaderCompiler published to $publishedCompiler"
