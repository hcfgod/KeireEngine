[CmdletBinding()]
param([ValidateSet("Release", "Dist")][string]$Configuration = "Release", [string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig; $Lock = Get-DependencyLock
$CMake = Get-CMakeExecutable
if (-not $CMake) { throw "CMake 3.20 or newer is required for SDK package validation." }
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset; $outputArchitecture = Get-ArchitectureOutputName $Architecture
& (Join-Path $PSScriptRoot "test.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate
& (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$name = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-$Configuration"; $stage = Join-Path $Root "Artifacts\$name"
$archive = Join-Path $Root "Artifacts\$name.zip"; $symbols = Join-Path $Root "Artifacts\$name-symbols.zip"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archive, "$archive.sha256", $symbols, "$symbols.sha256" -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin", "$stage\lib", "$stage\include", "$stage\third-party\licenses", "$stage\examples\consumer", "$stage\lib\cmake\$($Project.PROJECT_IDENTIFIER)" | Out-Null
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).lib" "$stage\lib\"
Copy-Item "$Root\$($Project.CORE_DIRECTORY)\Include\*" "$stage\include\" -Recurse
Copy-Item "$Root\Vendor\spdlog\include\spdlog" "$stage\third-party\spdlog\" -Recurse
Copy-Item "$Root\Vendor\spdlog\LICENSE" "$stage\third-party\licenses\spdlog-LICENSE.txt"
Copy-Item "$Root\Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst" "$stage\third-party\licenses\fmt-LICENSE.rst"
Copy-Item "$Root\Vendor\doctest\LICENSE.txt" "$stage\third-party\licenses\doctest-LICENSE.txt"
Copy-Item "$Root\README.md", "$Root\LICENSE.txt", "$Root\THIRD_PARTY_NOTICES.md" $stage
Copy-Item "$Root\Examples\Consumer\*" "$stage\examples\consumer\" -Recurse
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
$dirty = if (Test-GitRepository $Root) { [bool]((& git -C $Root status --porcelain --untracked-files=no) -join "") } else { $false }
$manifest = [ordered]@{ project=$Project.PROJECT_IDENTIFIER; version=$Project.PROJECT_VERSION; commit=(Get-GitHeadCommit $Root "unknown"); dirty=$dirty; platform="Windows"; architecture=$outputArchitecture; configuration=$Configuration; generator=$Generator; toolset=$Toolset; compiler=$compiler; spdlog=$Lock.SPDLOG_COMMIT; doctest=$Lock.DOCTEST_COMMIT }
$manifest | ConvertTo-Json | Set-Content "$stage\build-manifest.json" -Encoding UTF8
Assert-WindowsPackageStage $stage $Project.CLIENT_TARGET $Project.CORE_TARGET $Project.PROJECT_NAMESPACE
Get-Content "$stage\build-manifest.json" -Raw | ConvertFrom-Json | Out-Null
Compress-Archive "$stage\*" $archive
(Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" | Set-Content "$archive.sha256" -Encoding ASCII
$symbolStage = Join-Path $Root "Artifacts\$name-symbols"
Remove-Item $symbolStage -Recurse -Force -ErrorAction SilentlyContinue
if ($Configuration -eq "Release") {
    New-Item -ItemType Directory -Force "$symbolStage\Client", "$symbolStage\Core" | Out-Null
    $clientPdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).pdb"
    $corePdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).pdb"
    if (Test-Path $clientPdb) { Copy-Item $clientPdb "$symbolStage\Client\" }
    if (Test-Path $corePdb) { Copy-Item $corePdb "$symbolStage\Core\" }
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
    $consumerSource = Join-Path $sdkRoot "examples\consumer\Main.cpp"
    $consumerExe = Join-Path $validationRoot "consumer.exe"
    $consumerObject = Join-Path $validationRoot "consumer.obj"
    if ($Toolset -eq "msc") {
        $consumerLinkOptions = if ($Configuration -eq "Dist") { @("/link", "/LTCG") } else { @() }
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /DCORE_STATIC "/I$(Join-Path $sdkRoot 'include')" `
            "/external:I$(Join-Path $sdkRoot 'third-party')" /external:W0 $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") "/Fo:$consumerObject" "/Fe:$consumerExe" @consumerLinkOptions
    }
    else {
        $compilerCommand = if ($Toolset -eq "clang") { "clang++" } else { "g++" }
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror -DCORE_STATIC "-I$(Join-Path $sdkRoot 'include')" `
            "-I$(Join-Path $sdkRoot 'third-party')" $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") -o $consumerExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer compilation failed with exit code $LASTEXITCODE." }
    Push-Location $validationRoot
    try { & $consumerExe; if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer failed with exit code $LASTEXITCODE." } }
    finally { Pop-Location }
    $cmakeBuild = Join-Path $validationRoot "cmake-build"
    & $CMake -S (Join-Path $sdkRoot "examples\consumer") -B $cmakeBuild "-DCMAKE_PREFIX_PATH=$sdkRoot"
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake configuration failed with exit code $LASTEXITCODE." }
    & $CMake --build $cmakeBuild --config Release
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake build failed with exit code $LASTEXITCODE." }
    $cmakeConsumer = Get-ChildItem $cmakeBuild -Filter "SdkConsumer.exe" -Recurse | Select-Object -First 1
    if (-not $cmakeConsumer) { throw "SDK CMake consumer executable was not produced." }
    & $cmakeConsumer.FullName
    if ($LASTEXITCODE -ne 0) { throw "SDK CMake consumer failed with exit code $LASTEXITCODE." }
}
finally {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "==> Package created: $archive"
