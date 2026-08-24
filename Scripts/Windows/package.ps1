[CmdletBinding()]
param([ValidateSet("Release", "Dist")][string]$Configuration = "Release", [string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default", [switch]$CI, [switch]$Update, [switch]$Generate, [switch]$AllowDirty, [switch]$StageOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig; $Lock = Get-DependencyLock
$worktreePolicy = Get-WindowsPackageWorktreePolicy -Root $Root -AllowDirty:$AllowDirty -CI:$CI
$dirty = $worktreePolicy.Dirty
$developmentArtifact = $worktreePolicy.DevelopmentArtifact
$CMake = Get-CMakeExecutable
if (-not $CMake) { throw "CMake 3.20 or newer is required for SDK package validation." }
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset; $outputArchitecture = Get-ArchitectureOutputName $Architecture
$imguiLibraryName = "$($Project.PROJECT_NAMESPACE)ImGui"
$zstdLibraryName = "$($Project.PROJECT_NAMESPACE)Zstd"
$assetToolName = "$($Project.PROJECT_NAMESPACE)AssetTool"
$assetWorkerName = "$($Project.PROJECT_NAMESPACE)AssetWorker"
$runtimeName = "$($Project.PROJECT_NAMESPACE)Runtime"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build-info.ps1") } "Build metadata generation"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "test.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate } "Package test suite"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -SmokeWindow } "Package editor smoke test"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $assetToolName -CI:$CI } "AssetTool build"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $assetWorkerName -CI:$CI } "Asset worker build"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $runtimeName -CI:$CI } "Runtime build"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $Project.HUB_TARGET -CI:$CI } "Project Hub build"
Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture -Toolset $Toolset } "Shader compiler build"
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$name = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-$Configuration"; $stage = Join-Path $Root "Artifacts\$name"
$archive = Join-Path $Root "Artifacts\$name.zip"; $symbols = Join-Path $Root "Artifacts\$name-symbols.zip"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
if (-not $StageOnly) {
    Remove-Item $archive, "$archive.sha256", $symbols, "$symbols.sha256" -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force "$stage\bin", "$stage\lib", "$stage\include", "$stage\Config", "$stage\samples", "$stage\content", "$stage\Docs\Diagnostics", "$stage\third-party\licenses", "$stage\third-party\SDL3", "$stage\examples\consumer", "$stage\examples\managed-consumer", "$stage\examples\source-module", "$stage\lib\cmake\$($Project.PROJECT_IDENTIFIER)" | Out-Null
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.HUB_TARGET)\$($Project.HUB_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$assetToolName\$assetToolName.exe" "$stage\bin\"
$assetWorkerDirectory = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$assetWorkerName"
Copy-Item (Join-Path $assetWorkerDirectory "$assetWorkerName.exe") "$stage\bin\"
Assert-WindowsFfmpegRuntimeClosure -Directory $assetWorkerDirectory -Context "Asset worker runtime"
foreach ($runtime in (Get-WindowsFfmpegRuntimeContract).Files) {
    Copy-Item -LiteralPath (Join-Path $assetWorkerDirectory $runtime.FileName) -Destination "$stage\bin\"
}
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$runtimeName\$runtimeName.exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$runtimeName\Managed" "$stage\bin\" -Recurse
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$runtimeName\nethost.dll" "$stage\bin\"
Copy-Item "$Root\Build\Tools\ShaderCompiler\KeireShaderCompiler.exe" "$stage\bin\"
Get-ChildItem "$Root\Build\Tools\ShaderCompiler" -Filter *.dll -File | Copy-Item -Destination "$stage\bin\"
Copy-Item "$Root\Build\Dependencies\ffmpeg\Release\install\share\licenses\ffmpeg" "$stage\third-party\licenses\" -Recurse
$coreArchiveTargets = @($Project.CORE_TARGET) + @(
    @("Assets", "Build", "World", "Rendering", "Scenes", "Scripting", "Ui", "Vfx") |
        ForEach-Object { "$($Project.CORE_TARGET)$_" }
)
$coreArchiveInputs = $coreArchiveTargets | ForEach-Object {
    Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$_\$_.lib"
}
& (Join-Path $PSScriptRoot "merge-static-libraries.ps1") `
    -Output (Join-Path $stage "lib\$($Project.CORE_TARGET).lib") -InputLibraries $coreArchiveInputs
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\DearImGui\$imguiLibraryName.lib" "$stage\lib\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\Zstd\$zstdLibraryName.lib" "$stage\lib\"
$dependencyInstall = Join-Path $Root "Build\Dependencies\windows-$outputArchitecture-$Toolset\Release\install"
Copy-Item (Join-Path $dependencyInstall "lib\assimp.lib"), (Join-Path $dependencyInstall "lib\zlibstatic.lib"), `
    (Join-Path $dependencyInstall "lib\Jolt.lib"), (Join-Path $dependencyInstall "lib\Recast.lib"), `
    (Join-Path $dependencyInstall "lib\Detour.lib"), (Join-Path $dependencyInstall "lib\DetourCrowd.lib"), `
    (Join-Path $dependencyInstall "lib\DetourTileCache.lib"), (Join-Path $dependencyInstall "lib\miniaudio.lib") "$stage\lib\"
$coralConfiguration = if ($Configuration -eq "Dist") { "Release" } else { $Configuration }
Copy-Item "$Root\Build\Dependencies\coral-patched\Build\$coralConfiguration\Coral.Native.lib" "$stage\lib\"
Copy-Item "$Root\Build\Dependencies\coral-nethost\nethost.lib" "$stage\lib\"
Copy-Item "$Root\Config\Client.json" "$stage\Config\Client.json"
Copy-WindowsTrackedTree $Root "Samples/KeireSandbox" "$stage\samples\KeireSandbox"
Copy-Item "$Root\$($Project.CORE_DIRECTORY)\Include\$($Project.PROJECT_NAMESPACE)" "$stage\include\" -Recurse
Copy-Item "$Root\Vendor\spdlog\LICENSE" "$stage\third-party\licenses\spdlog-LICENSE.txt"
Copy-Item "$Root\Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst" "$stage\third-party\licenses\fmt-LICENSE.rst"
Copy-Item "$Root\Vendor\doctest\LICENSE.txt" "$stage\third-party\licenses\doctest-LICENSE.txt"
Copy-Item "$Root\Vendor\json\LICENSE.MIT" "$stage\third-party\licenses\nlohmann-json-LICENSE.MIT.txt"
Copy-Item "$Root\Vendor\imgui\LICENSE.txt" "$stage\third-party\licenses\dear-imgui-LICENSE.txt"
Copy-Item "$Root\Vendor\zstd\LICENSE" "$stage\third-party\licenses\zstandard-LICENSE.txt"
Copy-Item "$Root\Vendor\entt\LICENSE" "$stage\third-party\licenses\entt-LICENSE.txt"
Copy-Item "$Root\Vendor\glm\copying.txt" "$stage\third-party\licenses\glm-COPYING.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\LICENSE.txt" "$stage\third-party\licenses\SDL-shadercross-LICENSE.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\external\DirectXShaderCompiler\LICENSE.TXT" "$stage\third-party\licenses\DirectXShaderCompiler-LICENSE.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\external\DirectXShaderCompiler\ThirdPartyNotices.txt" "$stage\third-party\licenses\DirectXShaderCompiler-ThirdPartyNotices.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\external\SPIRV-Cross\LICENSE" "$stage\third-party\licenses\SPIRV-Cross-LICENSE.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\external\SPIRV-Headers\LICENSE" "$stage\third-party\licenses\SPIRV-Headers-LICENSE.txt"
Copy-Item "$Root\Vendor\SDL_shadercross\external\SPIRV-Tools\LICENSE" "$stage\third-party\licenses\SPIRV-Tools-LICENSE.txt"
Copy-Item "$Root\Vendor\assimp\LICENSE" "$stage\third-party\licenses\assimp-LICENSE.txt"
Copy-Item "$Root\Vendor\assimp\contrib\zlib\LICENSE" "$stage\third-party\licenses\assimp-zlib-LICENSE.txt"
Copy-Item "$Root\Vendor\stb\LICENSE" "$stage\third-party\licenses\stb-LICENSE.txt"
Copy-Item "$dependencyInstall\share\licenses\keire\Jolt-LICENSE.txt", `
    "$dependencyInstall\share\licenses\keire\Recast-LICENSE.txt", `
    "$dependencyInstall\share\licenses\keire\miniaudio-LICENSE.txt" "$stage\third-party\licenses\"
Copy-Item "$Root\Build\Dependencies\coral-patched\LICENSE" "$stage\third-party\licenses\Coral-LICENSE.txt"
Copy-Item "$Root\Build\Dependencies\dotnet-sdk\LICENSE.txt" "$stage\third-party\licenses\dotnet-LICENSE.txt"
Copy-Item "$Root\Build\Dependencies\dotnet-sdk\ThirdPartyNotices.txt" `
    "$stage\third-party\licenses\dotnet-ThirdPartyNotices.txt"
$sdlInstall = Join-Path $Root "Build\Dependencies\windows-$outputArchitecture-$Toolset\Release\install"
if (-not (Test-Path (Join-Path $sdlInstall "lib\SDL3-static.lib"))) { throw "Packaged SDL Release dependency is missing." }
New-Item -ItemType Directory -Force "$stage\third-party\SDL3\include", "$stage\third-party\SDL3\lib", `
    "$stage\third-party\SDL3\cmake", "$stage\third-party\SDL3\licenses" | Out-Null
Copy-Item "$sdlInstall\include\SDL3" "$stage\third-party\SDL3\include\" -Recurse
Copy-Item "$sdlInstall\lib\SDL3-static.lib" "$stage\third-party\SDL3\lib\"
Copy-Item "$sdlInstall\cmake\*" "$stage\third-party\SDL3\cmake\" -Recurse
Copy-Item "$sdlInstall\licenses\SDL3" "$stage\third-party\SDL3\licenses\" -Recurse
Copy-Item "$Root\README.md", "$Root\LICENSE.txt", "$Root\THIRD_PARTY_NOTICES.md" $stage
Copy-Item "$Root\Docs\Diagnostics\*" "$stage\Docs\Diagnostics\" -Recurse
Copy-Item "$Root\Docs\PlayerBuilds.md" "$stage\Docs\"
Copy-Item "$Root\Config\SourceModules.premake.lua" "$stage\Config\"
Copy-Item "$Root\Examples\Consumer\*" "$stage\examples\consumer\" -Recurse
Copy-Item "$Root\Examples\ManagedConsumer\*" "$stage\examples\managed-consumer\" -Recurse
Copy-Item "$Root\Examples\SourceModule\*" "$stage\examples\source-module\" -Recurse
$packageConfig = [IO.File]::ReadAllText((Join-Path $Root "Config\PackageConfig.cmake.in"))
$packageConfig = $packageConfig.Replace("@CORE_TARGET@", $Project.CORE_TARGET).Replace("@PROJECT_NAMESPACE@", $Project.PROJECT_NAMESPACE).Replace("@PACKAGE_CONFIGURATION@", $Configuration)
[IO.File]::WriteAllText((Join-Path $stage "lib\cmake\$($Project.PROJECT_IDENTIFIER)\$($Project.PROJECT_IDENTIFIER)Config.cmake"), $packageConfig, [Text.UTF8Encoding]::new($false))
$compiler = if ($Toolset -eq "msc") {
    $clVersion = (Get-Item (Get-Command cl).Source).VersionInfo.FileVersion
    if ($clVersion -notmatch '^(\d+)\.(\d+)') { throw "Unable to determine the MSVC compiler version." }
    "MSVC $($Matches[1])$($Matches[2])"
}
elseif ($Toolset -eq "clang") { "Clang $((& clang -dumpversion) -join '')" }
else { "GCC $((& g++ -dumpfullversion -dumpversion) -join '')" }
$commit = Get-GitHeadCommit $Root "unknown"
$dotnetRuntimeVersion = (Get-ChildItem "$Root\Build\Dependencies\dotnet-sdk\shared\Microsoft.NETCore.App" -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$manifest = [ordered]@{ project=$Project.PROJECT_IDENTIFIER; version=$Project.PROJECT_VERSION; commit=$commit; dirty=$dirty; developmentArtifact=$developmentArtifact; platform="Windows"; architecture=$outputArchitecture; configuration=$Configuration; generator=$Generator; toolset=$Toolset; compiler=$compiler; spdlog=$Lock.SPDLOG_COMMIT; doctest=$Lock.DOCTEST_COMMIT; sdl=$Lock.SDL_COMMIT; json=$Lock.JSON_COMMIT; imgui=$Lock.IMGUI_COMMIT; zstd=$Lock.ZSTD_COMMIT; entt=$Lock.ENTT_COMMIT; glm=$Lock.GLM_COMMIT; sdlShadercross=$Lock.SDL_SHADERCROSS_COMMIT; dxc=$Lock.SDL_SHADERCROSS_DXC_COMMIT; spirvCross=$Lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT; spirvHeaders=$Lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT; spirvTools=$Lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT; assimp=$Lock.ASSIMP_COMMIT; stb=$Lock.STB_COMMIT; jolt=$Lock.JOLT_COMMIT; recast=$Lock.RECAST_COMMIT; miniaudio=$Lock.MINIAUDIO_COMMIT; coral=$Lock.CORAL_COMMIT; dotnetRuntime=$dotnetRuntimeVersion }
$manifest | ConvertTo-Json | Set-Content "$stage\build-manifest.json" -Encoding UTF8
Assert-WindowsPackageStage $stage $Project.CLIENT_TARGET $Project.HUB_TARGET $Project.CORE_TARGET $Project.PROJECT_NAMESPACE
$parsedManifest = Get-Content "$stage\build-manifest.json" -Raw | ConvertFrom-Json
if ($parsedManifest.commit -ne $commit -or $parsedManifest.commit -ne (Get-GitHeadCommit $Root "unknown")) {
    throw "Package manifest commit does not match the packaging worktree HEAD."
}
if ([bool]$parsedManifest.dirty -ne $dirty -or [bool]$parsedManifest.developmentArtifact -ne $developmentArtifact) {
    throw "Package manifest cleanliness flags do not match the package policy."
}
if (-not $AllowDirty -and ($parsedManifest.dirty -or $parsedManifest.developmentArtifact)) {
    throw "A production package cannot be marked dirty or as a development artifact."
}
if ($parsedManifest.imgui -ne $Lock.IMGUI_COMMIT) { throw "Packaged Dear ImGui identity does not match the dependency lock." }
if ($parsedManifest.zstd -ne $Lock.ZSTD_COMMIT) { throw "Packaged Zstandard identity does not match the dependency lock." }
if ($parsedManifest.entt -ne $Lock.ENTT_COMMIT) { throw "Packaged EnTT identity does not match the dependency lock." }
if ($parsedManifest.glm -ne $Lock.GLM_COMMIT) { throw "Packaged GLM identity does not match the dependency lock." }
if ($parsedManifest.assimp -ne $Lock.ASSIMP_COMMIT -or $parsedManifest.stb -ne $Lock.STB_COMMIT) {
    throw "Packaged asset importer identities do not match the dependency lock."
}
if ($parsedManifest.jolt -ne $Lock.JOLT_COMMIT -or $parsedManifest.recast -ne $Lock.RECAST_COMMIT -or
    $parsedManifest.miniaudio -ne $Lock.MINIAUDIO_COMMIT) {
    throw "Packaged gameplay middleware identities do not match the dependency lock."
}
if ($parsedManifest.coral -ne $Lock.CORAL_COMMIT -or
    -not ([string]$parsedManifest.dotnetRuntime).StartsWith("10.", [StringComparison]::Ordinal)) {
    throw "Packaged managed-runtime identities do not match the dependency lock."
}
if ($parsedManifest.sdlShadercross -ne $Lock.SDL_SHADERCROSS_COMMIT -or
    $parsedManifest.dxc -ne $Lock.SDL_SHADERCROSS_DXC_COMMIT -or
    $parsedManifest.spirvCross -ne $Lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT -or
    $parsedManifest.spirvHeaders -ne $Lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT -or
    $parsedManifest.spirvTools -ne $Lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT) {
    throw "Packaged shader compiler identities do not match the dependency lock."
}
$shaderHelpBase = Join-Path $env:TEMP ("keire-shader-help-" + [guid]::NewGuid().ToString("N"))
try {
    $shaderHelpProcess = Start-Process -FilePath (Join-Path $stage "bin\KeireShaderCompiler.exe") -ArgumentList "--help" `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$shaderHelpBase.out" -RedirectStandardError "$shaderHelpBase.err"
    $shaderCompilerHelp = ([IO.File]::ReadAllText("$shaderHelpBase.out") + [IO.File]::ReadAllText("$shaderHelpBase.err"))
    if ($shaderHelpProcess.ExitCode -ne 0 -or -not $shaderCompilerHelp.Contains("shadercross")) {
        throw "Packaged shader compiler validation failed."
    }
}
finally { Remove-Item "$shaderHelpBase.out", "$shaderHelpBase.err" -Force -ErrorAction SilentlyContinue }
$assetToolHelp = (& (Join-Path $stage "bin\$assetToolName.exe") --help) -join "`n"
if ($LASTEXITCODE -ne 0 -or -not $assetToolHelp.Contains("KeireAssetTool cook")) { throw "Packaged asset tool validation failed." }
$assetWorkerHelp = (& (Join-Path $stage "bin\$assetWorkerName.exe") --help) -join "`n"
if ($LASTEXITCODE -ne 0 -or -not $assetWorkerHelp.Contains("KeireAssetWorker")) { throw "Packaged asset worker validation failed." }
$sampleProject = Join-Path $stage "samples\KeireSandbox"
$previousShaderCompiler = $env:KEIRE_SHADER_COMPILER
$previousDotnetRoot = $env:DOTNET_ROOT
$previousPath = $env:PATH
$env:KEIRE_SHADER_COMPILER = Join-Path $stage "bin\KeireShaderCompiler.exe"
$env:DOTNET_ROOT = Join-Path $Root "Build\Dependencies\dotnet-sdk"
$env:PATH = "$env:DOTNET_ROOT;$env:PATH"
try { $assetImportOutput = (& (Join-Path $stage "bin\$assetToolName.exe") cook --project $sampleProject --output (Join-Path $stage "content\KeireSandbox") --profile Dist --target windows) -join "`n" }
finally {
    $env:KEIRE_SHADER_COMPILER = $previousShaderCompiler
    $env:DOTNET_ROOT = $previousDotnetRoot
    $env:PATH = $previousPath
}
if ($LASTEXITCODE -ne 0 -or -not $assetImportOutput.Contains("Cooked")) { throw "Packaged sample project asset validation failed." }
Remove-Item (Join-Path $sampleProject "Library") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $sampleProject "Logs"), (Join-Path $sampleProject "Build"), (Join-Path $sampleProject "Temp") -Recurse -Force -ErrorAction SilentlyContinue
$runtimeContent = Join-Path $stage "content\KeireSandbox"
if (-not (Test-Path (Join-Path $runtimeContent "catalog.json")) -or
    -not (Test-Path (Join-Path $runtimeContent "runtime-manifest.json"))) {
    throw "Packaged cooked runtime content is incomplete."
}
& (Join-Path $stage "bin\$runtimeName.exe") --content $runtimeContent --frames 12
if ($LASTEXITCODE -ne 0) { throw "Packaged runtime smoke failed with exit code $LASTEXITCODE." }
$versionResult = Invoke-WindowsExecutableCapture `
    (Join-Path $stage "bin\$($Project.CLIENT_TARGET).exe") @("--version")
if ($versionResult.ExitCode -ne 0) {
    throw "Packaged client version query failed with exit code $($versionResult.ExitCode)."
}
$versionOutput = $versionResult.StandardOutput
$commitPrefix = $commit.Substring(0, [Math]::Min(12, $commit.Length))
$expectedIdentity = if ($dirty) { "$commitPrefix-dirty" } else { $commitPrefix }
if (-not $versionOutput.Contains($expectedIdentity) -or (-not $dirty -and $versionOutput.Contains("$commitPrefix-dirty"))) {
    throw "Packaged binary identity does not match build-manifest.json."
}
Assert-WindowsPackageGeneratedDataFree $stage
if ($StageOnly) {
    Write-Host "==> Package stage created: $stage"
    return
}
Compress-WindowsArchive "$stage\*" $archive
Assert-WindowsPackageArchiveGeneratedDataFree $archive
(Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" | Set-Content "$archive.sha256" -Encoding ASCII
$symbolStage = Join-Path $Root "Artifacts\$name-symbols"
Remove-Item $symbolStage -Recurse -Force -ErrorAction SilentlyContinue
if ($Configuration -eq "Release") {
    New-Item -ItemType Directory -Force "$symbolStage\KeireClient", "$symbolStage\KeireHub", "$symbolStage\KeireCore", "$symbolStage\DearImGui", "$symbolStage\Zstd" | Out-Null
    $clientPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).pdb"
    $hubPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.HUB_TARGET)\$($Project.HUB_TARGET).pdb"
    $imguiPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\DearImGui\$imguiLibraryName.pdb"
    $zstdPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\Zstd\$zstdLibraryName.pdb"
    if (Test-Path $clientPdb) { Copy-Item $clientPdb "$symbolStage\KeireClient\" }
    if (Test-Path $hubPdb) { Copy-Item $hubPdb "$symbolStage\KeireHub\" }
    foreach ($coreArchiveTarget in $coreArchiveTargets) {
        $corePdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$coreArchiveTarget\$coreArchiveTarget.pdb"
        if (Test-Path $corePdb) { Copy-Item $corePdb "$symbolStage\KeireCore\" }
    }
    if (Test-Path $imguiPdb) { Copy-Item $imguiPdb "$symbolStage\DearImGui\" }
    if (Test-Path $zstdPdb) { Copy-Item $zstdPdb "$symbolStage\Zstd\" }
}
if ((Test-Path $symbolStage) -and (Get-ChildItem $symbolStage -File -Recurse | Select-Object -First 1)) {
    Compress-WindowsArchive "$symbolStage\*" $symbols
    (Get-FileHash $symbols -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name-symbols.zip" | Set-Content "$symbols.sha256" -Encoding ASCII
}
$validationRoot = Join-Path $env:LOCALAPPDATA ("CodexSdkValidation\" + [guid]::NewGuid().ToString("N"))
$previousValidationPath = $env:PATH
try {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
    $sdkRoot = Join-Path $validationRoot "sdk"
    New-Item -ItemType Directory -Force $sdkRoot | Out-Null
    Expand-Archive $archive $sdkRoot -Force
    Assert-WindowsPackageGeneratedDataFree $sdkRoot
    $env:PATH = "$(Join-Path $sdkRoot 'bin');$previousValidationPath"
    $consumerSource = Join-Path $sdkRoot "examples\consumer\Source\Main.cpp"
    $consumerExe = Join-Path $validationRoot "consumer.exe"
    $consumerObject = Join-Path $validationRoot "consumer.obj"
    Copy-Item (Join-Path $sdkRoot "bin\nethost.dll") $validationRoot
    $gameplayLibraries = @(
        (Join-Path $sdkRoot "lib\Jolt.lib"),
        (Join-Path $sdkRoot "lib\Recast.lib"),
        (Join-Path $sdkRoot "lib\Detour.lib"),
        (Join-Path $sdkRoot "lib\DetourCrowd.lib"),
        (Join-Path $sdkRoot "lib\DetourTileCache.lib"),
        (Join-Path $sdkRoot "lib\miniaudio.lib"),
        (Join-Path $sdkRoot "lib\Coral.Native.lib"),
        (Join-Path $sdkRoot "lib\nethost.lib")
    )
    $sdlMsvcLibraries = @(
        "kernel32.lib", "user32.lib", "gdi32.lib", "winmm.lib", "imm32.lib", "setupapi.lib", "version.lib",
        "ole32.lib", "oleaut32.lib", "shell32.lib", "advapi32.lib", "uuid.lib", "hid.lib", "mincore.lib",
        "dinput8.lib"
    )
    $sdlGnuLibraries = @(
        "-lkernel32", "-luser32", "-lgdi32", "-lwinmm", "-limm32", "-lsetupapi", "-lversion", "-lole32",
        "-loleaut32", "-lshell32", "-ladvapi32", "-luuid", "-lhid", "-lmincore", "-ldinput8"
    )
    if ($Toolset -eq "msc") {
        $consumerLinkOptions = if ($Configuration -eq "Dist") { @("/link", "/LTCG") } else { @() }
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /DKEIRE_STATIC "/I$(Join-Path $sdkRoot 'include')" $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") (Join-Path $sdkRoot "lib\assimp.lib") (Join-Path $sdkRoot "lib\zlibstatic.lib") `
            @gameplayLibraries `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            @sdlMsvcLibraries `
            "/Fo:$consumerObject" "/Fe:$consumerExe" @consumerLinkOptions
    }
    else {
        $compilerCommand = if ($Toolset -eq "clang") { "clang++" } else { "g++" }
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$(Join-Path $sdkRoot 'include')" $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") (Join-Path $sdkRoot "lib\assimp.lib") (Join-Path $sdkRoot "lib\zlibstatic.lib") `
            @gameplayLibraries `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            @sdlGnuLibraries -o $consumerExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer compilation failed with exit code $LASTEXITCODE." }
    Push-Location $validationRoot
    try { & $consumerExe (Join-Path $sdkRoot "examples\consumer\Client.json"); if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer failed with exit code $LASTEXITCODE." } }
    finally { Pop-Location }

    $managedSource = Join-Path $sdkRoot "examples\managed-consumer\Source\ClientApplication.cpp"
    $managedExe = Join-Path $validationRoot "managed-consumer.exe"
    $managedObject = Join-Path $validationRoot "managed-consumer.obj"
    if ($Toolset -eq "msc") {
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /DKEIRE_STATIC "/I$(Join-Path $sdkRoot 'include')" $managedSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") (Join-Path $sdkRoot "lib\assimp.lib") (Join-Path $sdkRoot "lib\zlibstatic.lib") `
            @gameplayLibraries `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            @sdlMsvcLibraries `
            "/Fo:$managedObject" "/Fe:$managedExe" @consumerLinkOptions
    }
    else {
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$(Join-Path $sdkRoot 'include')" $managedSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") (Join-Path $sdkRoot "lib\assimp.lib") (Join-Path $sdkRoot "lib\zlibstatic.lib") `
            @gameplayLibraries `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            @sdlGnuLibraries -o $managedExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK consumer compilation failed with exit code $LASTEXITCODE." }
    $managedHelp = (& $managedExe --help) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not $managedHelp.Contains("--managed-smoke")) { throw "Managed SDK consumer help validation failed." }
    & $managedExe --managed-smoke
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK consumer failed with exit code $LASTEXITCODE." }

    $managedApiProject = Join-Path $sdkRoot "examples\managed-consumer\ManagedApiConsumer.csproj"
    $managedApiOutput = Join-Path $validationRoot "managed-api-bin"
    $managedApiIntermediate = Join-Path $validationRoot "managed-api-obj"
    & (Join-Path $Root "Build\Dependencies\dotnet-sdk\dotnet.exe") build $managedApiProject --configuration Release `
        --nologo "-p:KeireManagedAssembly=$(Join-Path $sdkRoot 'bin\Managed\Keire.Managed.dll')" `
        "-p:BaseOutputPath=$managedApiOutput\" "-p:BaseIntermediateOutputPath=$managedApiIntermediate\"
    if ($LASTEXITCODE -ne 0) { throw "Managed API SDK consumer compilation failed with exit code $LASTEXITCODE." }

    $cmakeBuild = Join-Path $validationRoot "cmake-build"
    & $CMake -S (Join-Path $sdkRoot "examples\consumer") -B $cmakeBuild "-DCMAKE_PREFIX_PATH=$sdkRoot"
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake configuration failed with exit code $LASTEXITCODE." }
    & $CMake --build $cmakeBuild --config Release
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake build failed with exit code $LASTEXITCODE." }
    $cmakeConsumer = Get-ChildItem $cmakeBuild -Filter "SdkConsumer.exe" -Recurse | Select-Object -First 1
    if (-not $cmakeConsumer) { throw "SDK CMake consumer executable was not produced." }
    & $cmakeConsumer.FullName (Join-Path $sdkRoot "examples\consumer\Client.json")
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake consumer failed with exit code $LASTEXITCODE." }

    $managedCmakeBuild = Join-Path $validationRoot "managed-cmake-build"
    & $CMake -S (Join-Path $sdkRoot "examples\managed-consumer") -B $managedCmakeBuild "-DCMAKE_PREFIX_PATH=$sdkRoot"
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK CMake configuration failed with exit code $LASTEXITCODE." }
    & $CMake --build $managedCmakeBuild --config Release
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK CMake build failed with exit code $LASTEXITCODE." }
    $managedCmakeConsumer = Get-ChildItem $managedCmakeBuild -Filter "ManagedSdkConsumer.exe" -Recurse | Select-Object -First 1
    if (-not $managedCmakeConsumer) { throw "Managed SDK CMake consumer executable was not produced." }
    & $managedCmakeConsumer.FullName --managed-smoke
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK CMake consumer failed with exit code $LASTEXITCODE." }

    $moduleCmakeBuild = Join-Path $validationRoot "module-cmake-build"
    & $CMake -S (Join-Path $sdkRoot "examples\source-module") -B $moduleCmakeBuild "-DCMAKE_PREFIX_PATH=$sdkRoot"
    if ($LASTEXITCODE -ne 0) { throw "Source-module SDK CMake configuration failed with exit code $LASTEXITCODE." }
    & $CMake --build $moduleCmakeBuild --config Release
    if ($LASTEXITCODE -ne 0) { throw "Source-module SDK CMake build failed with exit code $LASTEXITCODE." }
    $moduleCmakeConsumer = Get-ChildItem $moduleCmakeBuild -Filter "SourceModuleConsumer.exe" -Recurse | Select-Object -First 1
    if (-not $moduleCmakeConsumer) { throw "Source-module SDK CMake consumer executable was not produced." }
    & $moduleCmakeConsumer.FullName --module-smoke
    if ($LASTEXITCODE -ne 0) { throw "Source-module SDK CMake consumer failed with exit code $LASTEXITCODE." }
}
finally {
    $env:PATH = $previousValidationPath
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "==> Package created: $archive"
