[CmdletBinding()]
param([ValidateSet("Release", "Dist")][string]$Configuration = "Release", [string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig; $Lock = Get-DependencyLock
$CMake = Get-CMakeExecutable
if (-not $CMake) { throw "CMake 3.20 or newer is required for SDK package validation." }
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset; $outputArchitecture = Get-ArchitectureOutputName $Architecture
$imguiLibraryName = "$($Project.PROJECT_NAMESPACE)ImGui"
$zstdLibraryName = "$($Project.PROJECT_NAMESPACE)Zstd"
$assetToolName = "$($Project.PROJECT_NAMESPACE)AssetTool"
& (Join-Path $PSScriptRoot "build-info.ps1")
& (Join-Path $PSScriptRoot "test.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate
& (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -SmokeWindow
& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $assetToolName -CI:$CI
& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -Target $Project.HUB_TARGET -CI:$CI
& (Join-Path $PSScriptRoot "shader-compiler.ps1") -Generator $Generator -Architecture $Architecture -Toolset $Toolset
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$name = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-$Configuration"; $stage = Join-Path $Root "Artifacts\$name"
$archive = Join-Path $Root "Artifacts\$name.zip"; $symbols = Join-Path $Root "Artifacts\$name-symbols.zip"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archive, "$archive.sha256", $symbols, "$symbols.sha256" -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin", "$stage\lib", "$stage\include", "$stage\Config", "$stage\samples", "$stage\third-party\licenses", "$stage\third-party\SDL3", "$stage\examples\consumer", "$stage\examples\managed-consumer", "$stage\lib\cmake\$($Project.PROJECT_IDENTIFIER)" | Out-Null
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.HUB_TARGET)\$($Project.HUB_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$assetToolName\$assetToolName.exe" "$stage\bin\"
Copy-Item "$Root\Build\Tools\ShaderCompiler\KeireShaderCompiler.exe" "$stage\bin\"
Get-ChildItem "$Root\Build\Tools\ShaderCompiler" -Filter *.dll -File | Copy-Item -Destination "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).lib" "$stage\lib\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\DearImGui\$imguiLibraryName.lib" "$stage\lib\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\Zstd\$zstdLibraryName.lib" "$stage\lib\"
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
$sdlInstall = Join-Path $Root "Build\Dependencies\windows-$outputArchitecture-$Toolset\Release\install"
if (-not (Test-Path (Join-Path $sdlInstall "lib\SDL3-static.lib"))) { throw "Packaged SDL Release dependency is missing." }
Copy-Item "$sdlInstall\*" "$stage\third-party\SDL3\" -Recurse
Copy-Item "$Root\README.md", "$Root\LICENSE.txt", "$Root\THIRD_PARTY_NOTICES.md" $stage
Copy-Item "$Root\Examples\Consumer\*" "$stage\examples\consumer\" -Recurse
Copy-Item "$Root\Examples\ManagedConsumer\*" "$stage\examples\managed-consumer\" -Recurse
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
$dirty = if (Test-GitRepository $Root) { [bool]((& git -C $Root status --porcelain --untracked-files=normal) -join "") } else { $false }
$commit = Get-GitHeadCommit $Root "unknown"
$manifest = [ordered]@{ project=$Project.PROJECT_IDENTIFIER; version=$Project.PROJECT_VERSION; commit=$commit; dirty=$dirty; platform="Windows"; architecture=$outputArchitecture; configuration=$Configuration; generator=$Generator; toolset=$Toolset; compiler=$compiler; spdlog=$Lock.SPDLOG_COMMIT; doctest=$Lock.DOCTEST_COMMIT; sdl=$Lock.SDL_COMMIT; json=$Lock.JSON_COMMIT; imgui=$Lock.IMGUI_COMMIT; zstd=$Lock.ZSTD_COMMIT; entt=$Lock.ENTT_COMMIT; glm=$Lock.GLM_COMMIT; sdlShadercross=$Lock.SDL_SHADERCROSS_COMMIT; dxc=$Lock.SDL_SHADERCROSS_DXC_COMMIT; spirvCross=$Lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT; spirvHeaders=$Lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT; spirvTools=$Lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT }
$manifest | ConvertTo-Json | Set-Content "$stage\build-manifest.json" -Encoding UTF8
Assert-WindowsPackageStage $stage $Project.CLIENT_TARGET $Project.HUB_TARGET $Project.CORE_TARGET $Project.PROJECT_NAMESPACE
$parsedManifest = Get-Content "$stage\build-manifest.json" -Raw | ConvertFrom-Json
if ($parsedManifest.imgui -ne $Lock.IMGUI_COMMIT) { throw "Packaged Dear ImGui identity does not match the dependency lock." }
if ($parsedManifest.zstd -ne $Lock.ZSTD_COMMIT) { throw "Packaged Zstandard identity does not match the dependency lock." }
if ($parsedManifest.entt -ne $Lock.ENTT_COMMIT) { throw "Packaged EnTT identity does not match the dependency lock." }
if ($parsedManifest.glm -ne $Lock.GLM_COMMIT) { throw "Packaged GLM identity does not match the dependency lock." }
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
$sampleProject = Join-Path $stage "samples\KeireSandbox"
$previousShaderCompiler = $env:KEIRE_SHADER_COMPILER
$env:KEIRE_SHADER_COMPILER = Join-Path $stage "bin\KeireShaderCompiler.exe"
try { $assetImportOutput = (& (Join-Path $stage "bin\$assetToolName.exe") import --project $sampleProject) -join "`n" }
finally { $env:KEIRE_SHADER_COMPILER = $previousShaderCompiler }
if ($LASTEXITCODE -ne 0 -or -not $assetImportOutput.Contains("Imported")) { throw "Packaged sample project asset validation failed." }
Remove-Item (Join-Path $sampleProject "Library") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $sampleProject "Logs"), (Join-Path $sampleProject "Build"), (Join-Path $sampleProject "Temp") -Recurse -Force -ErrorAction SilentlyContinue
$versionOutput = (& (Join-Path $stage "bin\$($Project.CLIENT_TARGET).exe") --version) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "Packaged client version query failed with exit code $LASTEXITCODE." }
$commitPrefix = $commit.Substring(0, [Math]::Min(12, $commit.Length))
$expectedIdentity = if ($dirty) { "$commitPrefix-dirty" } else { $commitPrefix }
if (-not $versionOutput.Contains($expectedIdentity) -or (-not $dirty -and $versionOutput.Contains("$commitPrefix-dirty"))) {
    throw "Packaged binary identity does not match build-manifest.json."
}
Assert-WindowsPackageGeneratedDataFree $stage
Compress-Archive "$stage\*" $archive
Assert-WindowsPackageArchiveGeneratedDataFree $archive
(Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" | Set-Content "$archive.sha256" -Encoding ASCII
$symbolStage = Join-Path $Root "Artifacts\$name-symbols"
Remove-Item $symbolStage -Recurse -Force -ErrorAction SilentlyContinue
if ($Configuration -eq "Release") {
    New-Item -ItemType Directory -Force "$symbolStage\KeireClient", "$symbolStage\KeireHub", "$symbolStage\KeireCore", "$symbolStage\DearImGui", "$symbolStage\Zstd" | Out-Null
    $clientPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).pdb"
    $hubPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.HUB_TARGET)\$($Project.HUB_TARGET).pdb"
    $corePdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).pdb"
    $imguiPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\DearImGui\$imguiLibraryName.pdb"
    $zstdPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\Zstd\$zstdLibraryName.pdb"
    if (Test-Path $clientPdb) { Copy-Item $clientPdb "$symbolStage\KeireClient\" }
    if (Test-Path $hubPdb) { Copy-Item $hubPdb "$symbolStage\KeireHub\" }
    if (Test-Path $corePdb) { Copy-Item $corePdb "$symbolStage\KeireCore\" }
    if (Test-Path $imguiPdb) { Copy-Item $imguiPdb "$symbolStage\DearImGui\" }
    if (Test-Path $zstdPdb) { Copy-Item $zstdPdb "$symbolStage\Zstd\" }
}
if ((Test-Path $symbolStage) -and (Get-ChildItem $symbolStage -File -Recurse | Select-Object -First 1)) {
    Compress-Archive "$symbolStage\*" $symbols -Force
    (Get-FileHash $symbols -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name-symbols.zip" | Set-Content "$symbols.sha256" -Encoding ASCII
}
$validationRoot = Join-Path $env:LOCALAPPDATA ("CodexSdkValidation\" + [guid]::NewGuid().ToString("N"))
try {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
    $sdkRoot = Join-Path $validationRoot "sdk"
    New-Item -ItemType Directory -Force $sdkRoot | Out-Null
    Expand-Archive $archive $sdkRoot -Force
    Assert-WindowsPackageGeneratedDataFree $sdkRoot
    $consumerSource = Join-Path $sdkRoot "examples\consumer\Main.cpp"
    $consumerExe = Join-Path $validationRoot "consumer.exe"
    $consumerObject = Join-Path $validationRoot "consumer.obj"
    if ($Toolset -eq "msc") {
        $consumerLinkOptions = if ($Configuration -eq "Dist") { @("/link", "/LTCG") } else { @() }
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /DKEIRE_STATIC "/I$(Join-Path $sdkRoot 'include')" `
            "/external:I$(Join-Path $sdkRoot 'third-party')" /external:W0 $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            user32.lib gdi32.lib winmm.lib imm32.lib setupapi.lib version.lib ole32.lib oleaut32.lib shell32.lib advapi32.lib `
            "/Fo:$consumerObject" "/Fe:$consumerExe" @consumerLinkOptions
    }
    else {
        $compilerCommand = if ($Toolset -eq "clang") { "clang++" } else { "g++" }
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$(Join-Path $sdkRoot 'include')" `
            "-I$(Join-Path $sdkRoot 'third-party')" $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            -luser32 -lgdi32 -lwinmm -limm32 -lsetupapi -lversion -lole32 -loleaut32 -lshell32 -ladvapi32 -o $consumerExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer compilation failed with exit code $LASTEXITCODE." }
    Push-Location $validationRoot
    try { & $consumerExe (Join-Path $sdkRoot "examples\consumer\Client.json"); if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer failed with exit code $LASTEXITCODE." } }
    finally { Pop-Location }

    $managedSource = Join-Path $sdkRoot "examples\managed-consumer\ClientApplication.cpp"
    $managedExe = Join-Path $validationRoot "managed-consumer.exe"
    $managedObject = Join-Path $validationRoot "managed-consumer.obj"
    if ($Toolset -eq "msc") {
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /DKEIRE_STATIC "/I$(Join-Path $sdkRoot 'include')" `
            "/external:I$(Join-Path $sdkRoot 'third-party')" /external:W0 $managedSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            user32.lib gdi32.lib winmm.lib imm32.lib setupapi.lib version.lib ole32.lib oleaut32.lib shell32.lib advapi32.lib `
            "/Fo:$managedObject" "/Fe:$managedExe" @consumerLinkOptions
    }
    else {
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror -DKEIRE_STATIC "-I$(Join-Path $sdkRoot 'include')" `
            "-I$(Join-Path $sdkRoot 'third-party')" $managedSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") (Join-Path $sdkRoot "lib\$imguiLibraryName.lib") (Join-Path $sdkRoot "lib\$zstdLibraryName.lib") `
            (Join-Path $sdkRoot "third-party\SDL3\lib\SDL3-static.lib") `
            -luser32 -lgdi32 -lwinmm -limm32 -lsetupapi -lversion -lole32 -loleaut32 -lshell32 -ladvapi32 -o $managedExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK consumer compilation failed with exit code $LASTEXITCODE." }
    $managedHelp = (& $managedExe --help) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not $managedHelp.Contains("--managed-smoke")) { throw "Managed SDK consumer help validation failed." }
    & $managedExe --managed-smoke
    if ($LASTEXITCODE -ne 0) { throw "Managed SDK consumer failed with exit code $LASTEXITCODE." }

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
}
finally {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "==> Package created: $archive"
