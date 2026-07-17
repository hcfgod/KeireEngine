$ErrorActionPreference = "Stop"
$Windows = Resolve-Path (Join-Path $PSScriptRoot "..\Windows")
. (Join-Path $Windows "common.ps1")

function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -ne $Expected) { throw "$Message. Expected '$Expected', got '$Actual'." }
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "$Message failed." }
}

$project = Get-ProjectConfig
$generateScript = Get-Content (Join-Path $Windows "generate.ps1") -Raw
Assert-True ($generateScript.Contains('--file=premake5.lua')) "Unicode-safe relative Premake script path"
$menuScript = Get-Content (Join-Path $Windows "..\project.ps1") -Raw
Assert-True ($menuScript.Contains('$script:Target = $Project.CLIENT_TARGET')) "Post-rename client target refresh"
Assert-True (-not [string]::IsNullOrWhiteSpace($project.PROJECT_IDENTIFIER)) "Project manifest"
Assert-True ($project.PROJECT_VERSION -match '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') "Semantic project version"
Assert-True (Test-SemanticVersion "1.2.3-alpha.1+build.5") "Complete Semantic Version"
Assert-True (-not (Test-SemanticVersion "01.2.3")) "Semantic Version major leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3-01")) "Semantic Version prerelease leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3+")) "Empty Semantic Version build rejection"
Assert-Equal $project.PROJECT_MACRO_PREFIX (ConvertTo-MacroPrefix $project.PROJECT_IDENTIFIER) "Project macro prefix"
Assert-Equal (ConvertTo-MacroPrefix "HTTPServer2Client") "HTTP_SERVER2_CLIENT" "Macro prefix derivation"
$securityWorkflow = Get-Content (Join-Path (Get-RepositoryRoot) ".github\workflows\security.yml") -Raw
Assert-True ($securityWorkflow -match "(?m)^  security-status:\s*$") "Security activation sentinel"
Assert-True ($securityWorkflow -match "(?m)^    if: always\(\)\s*$") "Security sentinel always runs"
Assert-True ($securityWorkflow.Contains("ENABLE_ADVANCED_SECURITY")) "Advanced security opt-in variable"
Assert-True (-not $securityWorkflow.Contains("continue-on-error")) "Strict advanced security checks"
$emptyRepository = Join-Path ([IO.Path]::GetTempPath()) ("template-empty-git-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $emptyRepository | Out-Null
try {
    Assert-True (-not (Test-GitRepository $emptyRepository)) "Non-repository detection"
    & git -C $emptyRepository init --quiet
    Assert-Equal (Get-GitHeadCommit $emptyRepository) "uncommitted" "Empty Git commit fallback"
    Assert-True (Test-GitRepository $emptyRepository) "Empty Git repository detection"
}
finally {
    Remove-Item $emptyRepository -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-Equal (Normalize-Architecture "amd64") "x86_64" "x64 normalization"
Assert-Equal (Normalize-Architecture "aarch64") "ARM64" "ARM normalization"
Assert-Equal (Resolve-WindowsToolset "vs2022" "default") "msc" "VS default toolset"
Assert-Equal (Resolve-WindowsToolset "ninja" "default") "msc" "Ninja default toolset"
Assert-Equal (Resolve-WindowsToolset "gmake" "default") "gcc" "GNU Make default toolset"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "DebugUBSan" "x86_64" "msc" } "MSVC UBSan validation"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "Coverage" "x86_64" "clang" } "Coverage generator validation"
$lock = Get-DependencyLock
Assert-Equal $lock.SPDLOG_COMMIT "79524ddd08a4ec981b7fea76afd08ee05f83755d" "spdlog lock"
Assert-Equal $lock.DOCTEST_COMMIT "2d0a9359a60c51affe2a9bebb1be1dca47868151" "doctest lock"
Assert-Equal $lock.SDL_COMMIT "8e37db5e797b6167f3a00d697d816a684bd259c7" "SDL lock"
Assert-Equal $lock.JSON_COMMIT "55f93686c01528224f448c19128836e7df245f72" "JSON lock"
Assert-Equal $lock.IMGUI_COMMIT "b61e56346a92cfcaf1f43a545ca37b0b32239654" "Dear ImGui lock"
$vendorScript = Get-Content (Join-Path $Windows "vendor.ps1") -Raw
$vendorUpdateScript = Get-Content (Join-Path $Windows "vendor-update.ps1") -Raw
Assert-True ($vendorScript.Contains('Vendor/imgui') -and $vendorScript.Contains('$Lock.IMGUI_COMMIT')) "Dear ImGui vendor mapping"
Assert-True ($vendorUpdateScript.Contains('"imgui"')) "Dear ImGui vendor update support"
$dependencyScript = Get-Content (Join-Path $Windows "dependencies.ps1") -Raw
Assert-True ($dependencyScript.Contains('$Lock.SDL_COMMIT') -and $dependencyScript.Contains('$compiler') -and $dependencyScript.Contains('keire-dependency.stamp')) "Dependency cache identity inputs"
Assert-True ($dependencyScript.Contains('"Debug", "Release"') -and $dependencyScript.Contains('SDL_DUMMYVIDEO=ON') -and $dependencyScript.Contains('SDL_OFFSCREEN=ON')) "SDL variants and headless drivers"
Assert-True ($dependencyScript.Contains('SDL_GPU=ON') -and $dependencyScript.Contains('SDL_RENDER=OFF')) "SDL GPU renderer policy"
$premakePolicy = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Common.lua") -Raw
Assert-True ($premakePolicy.Contains('SDL3DebugLibrary') -and $premakePolicy.Contains('SDL3ReleaseLibrary')) "Premake SDL variant selection"
Assert-True ($premakePolicy.Contains('imgui_impl_sdl3.cpp') -and $premakePolicy.Contains('imgui_impl_sdlgpu3.cpp') -and $premakePolicy.Contains('imgui_stdlib.cpp') -and $premakePolicy.Contains('warnings "Off"')) "Premake Dear ImGui source policy"
$corePremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\premake5.lua") -Raw
$clientPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\premake5.lua") -Raw
$testsPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireTests\premake5.lua") -Raw
Assert-True ($corePremake.Contains('AddDearImGuiSources()') -and -not $clientPremake.Contains('AddDearImGuiSources()') -and -not $testsPremake.Contains('AddDearImGuiSources()')) "KeireCore-only Dear ImGui ownership"
$clientSources = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireClient") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True (-not ($clientSources -match '#include\s*[<\"]imgui|ImGui::|ImGui[A-Z]')) "KeireClient Dear ImGui isolation"
$publicHeaders = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireCore\Include") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True (-not ($publicHeaders -match 'SDL3/|nlohmann/json|imgui')) "Public dependency isolation"
$packageScript = Get-Content (Join-Path $Windows "package.ps1") -Raw
Assert-True ($packageScript.Contains('dear-imgui-LICENSE.txt') -and $packageScript.Contains('$Lock.IMGUI_COMMIT')) "Dear ImGui package metadata"

$packageStage = Join-Path ([IO.Path]::GetTempPath()) ("template-package-test-" + [guid]::NewGuid().ToString("N"))
try {
    foreach ($path in @("bin\Client.exe", "lib\Core.lib", "Config\Client.json", "include\Core\Core.h", "include\Core\Log.h", "include\Core\Api.h", "include\Core\Application.h", "include\Core\Assert.h", "include\Core\BuildInfo.h", "include\Core\EntryPoint.h", "include\Core\Event.h", "include\Core\Layer.h", "include\Core\Ref.h", "include\Core\Time.h", "include\Core\Window.h", "include\Core\WindowConfig.h", "examples\consumer\Main.cpp", "examples\consumer\Client.json", "examples\consumer\CMakeLists.txt", "examples\consumer\README.md", "examples\managed-consumer\ClientApplication.cpp", "examples\managed-consumer\CMakeLists.txt", "examples\managed-consumer\README.md", "lib\cmake\CrossPlatformCoreClientTemplate\CrossPlatformCoreClientTemplateConfig.cmake", "third-party\spdlog\spdlog.h", "third-party\SDL3\include\SDL3\SDL.h", "third-party\SDL3\lib\SDL3-static.lib", "third-party\SDL3\cmake\SDL3Config.cmake", "third-party\SDL3\licenses\SDL3\LICENSE.txt", "third-party\licenses\spdlog-LICENSE.txt", "third-party\licenses\fmt-LICENSE.rst", "third-party\licenses\doctest-LICENSE.txt", "third-party\licenses\nlohmann-json-LICENSE.MIT.txt", "third-party\licenses\dear-imgui-LICENSE.txt", "README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json")) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    $uiHeader = Join-Path $packageStage "include\Core\Ui.h"
    New-Item -ItemType Directory -Force (Split-Path $uiHeader) | Out-Null
    New-Item -ItemType File -Force $uiHeader | Out-Null
    Assert-WindowsPackageStage $packageStage Client Core Core
    Remove-Item (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Core Core } "Missing Dear ImGui package license validation"
    New-Item -ItemType File (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt") | Out-Null
    Remove-Item (Join-Path $packageStage "third-party\licenses\spdlog-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Core Core } "Missing package license validation"
}
finally {
    Remove-Item $packageStage -Recurse -Force -ErrorAction SilentlyContinue
}

$identityFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-identity-test-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $identityFixture "Scripts\Windows"), (Join-Path $identityFixture "Config") | Out-Null
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "build-info.ps1") (Join-Path $identityFixture "Scripts\Windows")
    $identityConfig = @(
        "PROJECT_IDENTIFIER=IdentityFixture", 'PROJECT_DISPLAY_NAME=Quoted "Kéire" \\ Client',
        "PROJECT_VERSION=1.2.3-alpha.1+build.5", "PROJECT_NAMESPACE=IdentityFixture", "PROJECT_MACRO_PREFIX=IDENTITY_FIXTURE",
        "CORE_TARGET=IdentityFixtureCore", "CORE_DIRECTORY=IdentityFixtureCore", "CLIENT_TARGET=IdentityFixtureClient", "CLIENT_DIRECTORY=IdentityFixtureClient",
        "TESTS_TARGET=IdentityFixtureTests", "TESTS_DIRECTORY=IdentityFixtureTests", "ARTIFACT_PREFIX=identityfixture", "REPOSITORY_SLUG=example/identity-fixture"
    )
    [IO.File]::WriteAllLines((Join-Path $identityFixture "Config\Project.conf"), $identityConfig, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $identityFixture ".gitignore"), "/Build/`n.ninja_lock`n", [Text.UTF8Encoding]::new($false))
    & git -C $identityFixture init --quiet
    & git -C $identityFixture config user.email "scripts@example.invalid"
    & git -C $identityFixture config user.name "Script Tests"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m first
    $firstCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $identityHeader = Join-Path $identityFixture "Build\Generated\IdentityFixture\BuildInfo.generated.h"
    $firstIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_VERSION "1.2.3-alpha.1+build.5"')) "Semantic Version identity generation"
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_NAME "Quoted \"Kéire\" \\\\ Client"')) "C string identity escaping"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_COMMIT `"$firstCommit`"")) "Clean Git identity"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Clean Git dirty state"
    New-Item -ItemType File -Force (Join-Path $identityFixture ".ninja_lock") | Out-Null
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Ignored Ninja lock dirty state"
    $firstWriteTime = [IO.File]::GetLastWriteTimeUtc($identityHeader)
    Start-Sleep -Milliseconds 1100
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-Equal ([IO.File]::GetLastWriteTimeUtc($identityHeader)) $firstWriteTime "Unchanged identity header timestamp"
    Set-Content -LiteralPath (Join-Path $identityFixture "untracked.txt") -Value untracked
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY true")) "Untracked Git dirty state"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m second
    $secondCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $secondIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($secondCommit -ne $firstCommit -and $secondIdentity.Contains($secondCommit)) "Identity refresh after commit"
    Assert-True ($secondIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Committed Git clean state"
}
finally {
    Remove-Item -LiteralPath $identityFixture -Recurse -Force -ErrorAction SilentlyContinue
}

$parentFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-script-test-" + [guid]::NewGuid().ToString("N"))
$fixture = Join-Path $parentFixture "Template"
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $coreDirectory = $project.CORE_DIRECTORY
    $clientDirectory = $project.CLIENT_DIRECTORY
    $testsDirectory = $project.TESTS_DIRECTORY
    $projectNamespace = $project.PROJECT_NAMESPACE
    foreach ($directory in @("Scripts\Windows", "Config", "Examples\Consumer", "Examples\ManagedConsumer", "$coreDirectory\Include\$projectNamespace", "$coreDirectory\Source", "$clientDirectory\Source", "$testsDirectory\Source", "Vendor", "Build\Bin")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "rename.ps1"), (Join-Path $Windows "clean.ps1"), (Join-Path $Windows "doctor.ps1") (Join-Path $fixture "Scripts\Windows")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Project.conf") (Join-Path $fixture "Config\Project.conf")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Client.json") (Join-Path $fixture "Config\Client.json")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\PackageConfig.cmake.in") (Join-Path $fixture "Config\PackageConfig.cmake.in")
    Copy-Item (Join-Path (Get-RepositoryRoot) "premake5.lua") (Join-Path $fixture "premake5.lua")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\Consumer\CMakeLists.txt"), (Join-Path (Get-RepositoryRoot) "Examples\Consumer\Main.cpp") (Join-Path $fixture "Examples\Consumer")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\CMakeLists.txt"), (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\ClientApplication.cpp") (Join-Path $fixture "Examples\ManagedConsumer")
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Core.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
namespace $projectNamespace { const char* GetName(); }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Log.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
namespace $projectNamespace { class Log; }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Source\Library.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$clientDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$testsDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "README.md") "$($project.PROJECT_IDENTIFIER) $($project.REPOSITORY_SLUG) Scripts/Tests Core.log Client.log"
    Set-Content (Join-Path $fixture "Vendor\keep.txt") 'vendor'
    Set-Content (Join-Path $fixture "Build\Bin\remove.txt") 'build'

    & git -C $parentFixture init --quiet
    & git -C $parentFixture config user.email "scripts@example.invalid"
    & git -C $parentFixture config user.name "Script Tests"
    & git -C $parentFixture add Template
    & git -C $parentFixture commit --quiet -m fixture
    Assert-Equal (Get-GitWorktreeRoot $fixture).Path (Resolve-Path $parentFixture).Path "Parent Git worktree detection"
    Add-Content (Join-Path $fixture "README.md") "dirty"

    Assert-Throws { & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName "Bad`nName" -Repository example/script-fixture } "Rename newline rejection"
    $unicodeDisplayName = 'Script "Fixturé" \\ Name'
    & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName $unicodeDisplayName -Repository example/script-fixture
    Assert-True (-not (Test-Path (Join-Path $fixture ".git"))) "Nested Git repository prevention"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Include\ScriptFixture\Core.h")) "Rename structure"
    $renamed = Get-Content (Join-Path $fixture "Config\Project.conf") -Raw -Encoding UTF8
    Assert-True ($renamed.Contains("CORE_TARGET=ScriptFixtureCore")) "Rename manifest"
    Assert-True ($renamed.Contains("PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE")) "Rename macro manifest"
    Assert-True ($renamed.Contains("PROJECT_VERSION=$($project.PROJECT_VERSION)")) "Rename version preservation"
    $renamedPremake = Get-Content (Join-Path $fixture "premake5.lua") -Raw
    Assert-True ($renamedPremake.Contains("valid Semantic Version 2.0.0")) "Premake Semantic Version validation"
    $renamedConsumer = Get-Content (Join-Path $fixture "Examples\Consumer\CMakeLists.txt") -Raw
    Assert-True ($renamedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed CMake package identity"
    Assert-True ($renamedConsumer.Contains("ScriptFixture::Core")) "Renamed CMake imported target"
    $renamedManagedConsumer = Get-Content (Join-Path $fixture "Examples\ManagedConsumer\CMakeLists.txt") -Raw
    Assert-True ($renamedManagedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed managed CMake package identity"
    Assert-True ($renamedManagedConsumer.Contains("ScriptFixture::Core")) "Renamed managed CMake imported target"
    Assert-True ((Get-Content (Join-Path $fixture "Config\PackageConfig.cmake.in") -Raw).Contains("@PROJECT_NAMESPACE@::Core")) "Generic package template preservation"
    Assert-True ($renamed.Contains("PROJECT_DISPLAY_NAME=$unicodeDisplayName")) "UTF-8 display name preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("dirty")) "Pre-existing edit preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("Scripts/Tests Core.log Client.log")) "Stable generic path preservation"
    Assert-True (Test-Path (Join-Path $fixture "Config\Client.json")) "Stable client configuration path preservation"
    $renamedHeaders = (Get-ChildItem (Join-Path $fixture "ScriptFixtureCore\Include") -File -Recurse | Get-Content) -join "`n"
    Assert-True (-not $renamedHeaders.Contains($project.PROJECT_MACRO_PREFIX)) "Old include guard removal"
    Assert-True ($renamedHeaders.Contains("SCRIPT_FIXTURE_CORE_CORE_H")) "Renamed include guard"
    & (Join-Path $fixture "Scripts\Windows\doctor.ps1") -Generator ninja -Architecture x86_64 -Toolset clang

    & (Join-Path $fixture "Scripts\Windows\clean.ps1") -Scope full
    Assert-True (-not (Test-Path (Join-Path $fixture "Build\Bin"))) "Full clean build removal"
    Assert-True (Test-Path (Join-Path $fixture "Vendor\keep.txt")) "Full clean vendor preservation"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Source\Library.cpp")) "Full clean source preservation"
}
finally {
    if ($parentFixture.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path $parentFixture)) {
        Remove-Item -LiteralPath $parentFixture -Recurse -Force
    }
}

$repositoryFiles = Get-ChildItem (Get-RepositoryRoot) -File -Recurse | Where-Object {
    $_.FullName -notmatch '[\\/](\.git|\.vs|Vendor|Tools|Build|Logs|Artifacts)[\\/]' -and $_.FullName -notmatch '[\\/]Scripts[\\/]Tests[\\/]'
}
$deprecatedNames = foreach ($prefix in @("CORE", "CLIENT")) {
    foreach ($suffix in @("API", "ASSERT", "ASSERTIONS_ENABLED", "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL")) { "${prefix}_${suffix}" }
}
foreach ($name in $deprecatedNames) {
    Assert-Equal (@($repositoryFiles | Select-String -Pattern "\b$([regex]::Escape($name))\b").Count) 0 "Deprecated public macro check for $name"
}
foreach ($stale in @('#include "KeireCore/', 'Scripts/KeireTests', 'Scripts\KeireTests', 'Scripts/Windows/Tests', 'Scripts/Unix/Tests', 'KeireCore.log', 'KeireClient.log')) {
    Assert-Equal (@($repositoryFiles | Select-String -SimpleMatch $stale).Count) 0 "Stale repository identity check for $stale"
}
Write-Host "Windows script regression tests passed."
