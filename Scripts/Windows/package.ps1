[CmdletBinding()]
param([ValidateSet("Release", "Dist")][string]$Configuration = "Release", [string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig; $Lock = Get-DependencyLock
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset; $outputArchitecture = Get-ArchitectureOutputName $Architecture
& (Join-Path $PSScriptRoot "test.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate
& (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$name = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-$Configuration"; $stage = Join-Path $Root "Artifacts\$name"
$archive = Join-Path $Root "Artifacts\$name.zip"; $symbols = Join-Path $Root "Artifacts\$name-symbols.zip"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archive, "$archive.sha256", $symbols, "$symbols.sha256" -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin", "$stage\lib", "$stage\include", "$stage\third-party\licenses" | Out-Null
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).lib" "$stage\lib\"
Copy-Item "$Root\$($Project.CORE_DIRECTORY)\Include\*" "$stage\include\" -Recurse
Copy-Item "$Root\Vendor\spdlog\include\spdlog" "$stage\third-party\spdlog\" -Recurse
Copy-Item "$Root\Vendor\spdlog\LICENSE" "$stage\third-party\licenses\spdlog-LICENSE.txt"
Copy-Item "$Root\Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst" "$stage\third-party\licenses\fmt-LICENSE.rst"
Copy-Item "$Root\Vendor\doctest\LICENSE.txt" "$stage\third-party\licenses\doctest-LICENSE.txt"
Copy-Item "$Root\README.md", "$Root\LICENSE.txt", "$Root\THIRD_PARTY_NOTICES.md" $stage
$compiler = if ($Toolset -eq "msc") {
    $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
    "MSVC $((Get-VSBuildEnvironment $majorVersion).InstallationVersion)"
}
elseif ($Toolset -eq "clang") { (& clang --version | Select-Object -First 1) -join "" }
else { (& g++ --version | Select-Object -First 1) -join "" }
$manifest = [ordered]@{ project=$Project.PROJECT_IDENTIFIER; commit=(Get-GitHeadCommit $Root); platform="windows"; architecture=$Architecture; configuration=$Configuration; generator=$Generator; toolset=$Toolset; compiler=$compiler; spdlog=$Lock.SPDLOG_COMMIT; doctest=$Lock.DOCTEST_COMMIT }
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
$validationRoot = Join-Path $Root "Artifacts\$name-validation"
try {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
    $sdkRoot = Join-Path $validationRoot "sdk"
    New-Item -ItemType Directory -Force $sdkRoot | Out-Null
    Expand-Archive $archive $sdkRoot -Force
    $consumerSource = Join-Path $validationRoot "consumer.cpp"
    $consumerExe = Join-Path $validationRoot "consumer.exe"
    $consumerObject = Join-Path $validationRoot "consumer.obj"
    @"
#include "$($Project.PROJECT_NAMESPACE)/Core.h"
#include <string>

int main()
{
    $($Project.PROJECT_NAMESPACE)::LogConfig config;
    config.EnableConsole = false;
    config.LogDirectory = "Logs";
    $($Project.PROJECT_NAMESPACE)::Log::Initialize(config);
    CORE_INFO("SDK consumer initialized");
    $($Project.PROJECT_NAMESPACE)::Log::Shutdown();
    return std::string($($Project.PROJECT_NAMESPACE)::GetName()).empty() ? 1 : 0;
}
"@ | Set-Content $consumerSource -Encoding UTF8

    if ($Toolset -eq "msc") {
        $consumerLinkOptions = if ($Configuration -eq "Dist") { @("/link", "/LTCG") } else { @() }
        & cl /nologo /std:c++20 /EHsc /MD /W4 /WX /utf-8 /permissive- /Zc:__cplusplus "/I$(Join-Path $sdkRoot 'include')" `
            "/external:I$(Join-Path $sdkRoot 'third-party')" /external:W0 $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") "/Fo:$consumerObject" "/Fe:$consumerExe" @consumerLinkOptions
    }
    else {
        $compilerCommand = if ($Toolset -eq "clang") { "clang++" } else { "g++" }
        & $compilerCommand -std=c++20 -Wall -Wextra -Werror "-I$(Join-Path $sdkRoot 'include')" `
            "-I$(Join-Path $sdkRoot 'third-party')" $consumerSource `
            (Join-Path $sdkRoot "lib\$($Project.CORE_TARGET).lib") -o $consumerExe
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer compilation failed with exit code $LASTEXITCODE." }
    Push-Location $validationRoot
    try { & $consumerExe; if ($LASTEXITCODE -ne 0) { throw "Extracted SDK consumer failed with exit code $LASTEXITCODE." } }
    finally { Pop-Location }
}
finally {
    Remove-Item $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "==> Package created: $archive"
