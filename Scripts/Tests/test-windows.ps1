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
Assert-True (-not [string]::IsNullOrWhiteSpace($project.PROJECT_IDENTIFIER)) "Project manifest"
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

$packageStage = Join-Path ([IO.Path]::GetTempPath()) ("template-package-test-" + [guid]::NewGuid().ToString("N"))
try {
    foreach ($path in @("bin\Client.exe", "lib\Core.lib", "include\Core\Core.h", "include\Core\Log.h", "third-party\spdlog\spdlog.h", "third-party\licenses\spdlog-LICENSE.txt", "third-party\licenses\fmt-LICENSE.rst", "third-party\licenses\doctest-LICENSE.txt", "README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json")) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    Assert-WindowsPackageStage $packageStage Client Core Core
    Remove-Item (Join-Path $packageStage "third-party\licenses\spdlog-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Core Core } "Missing package license validation"
}
finally {
    Remove-Item $packageStage -Recurse -Force -ErrorAction SilentlyContinue
}

$parentFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-script-test-" + [guid]::NewGuid().ToString("N"))
$fixture = Join-Path $parentFixture "Template"
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $coreDirectory = $project.CORE_DIRECTORY
    $clientDirectory = $project.CLIENT_DIRECTORY
    $testsDirectory = $project.TESTS_DIRECTORY
    $projectNamespace = $project.PROJECT_NAMESPACE
    foreach ($directory in @("Scripts\Windows", "Config", "$coreDirectory\Include\$projectNamespace", "$coreDirectory\Source", "$clientDirectory\Source", "$testsDirectory\Source", "Vendor", "Build\Bin")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "rename.ps1"), (Join-Path $Windows "clean.ps1"), (Join-Path $Windows "doctor.ps1") (Join-Path $fixture "Scripts\Windows")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Project.conf") (Join-Path $fixture "Config\Project.conf")
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
    Set-Content (Join-Path $fixture "README.md") "$($project.PROJECT_IDENTIFIER) $($project.REPOSITORY_SLUG)"
    Set-Content (Join-Path $fixture "Vendor\keep.txt") 'vendor'
    Set-Content (Join-Path $fixture "Build\Bin\remove.txt") 'build'

    & git -C $parentFixture init --quiet
    & git -C $parentFixture config user.email "scripts@example.invalid"
    & git -C $parentFixture config user.name "Script Tests"
    & git -C $parentFixture add Template
    & git -C $parentFixture commit --quiet -m fixture
    Assert-Equal (Get-GitWorktreeRoot $fixture).Path (Resolve-Path $parentFixture).Path "Parent Git worktree detection"
    Add-Content (Join-Path $fixture "README.md") "dirty"

    $unicodeDisplayName = "Script Fixtur$([char]0x00E9)"
    & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName $unicodeDisplayName -Repository example/script-fixture
    Assert-True (-not (Test-Path (Join-Path $fixture ".git"))) "Nested Git repository prevention"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Include\ScriptFixture\Core.h")) "Rename structure"
    $renamed = Get-Content (Join-Path $fixture "Config\Project.conf") -Raw -Encoding UTF8
    Assert-True ($renamed.Contains("CORE_TARGET=ScriptFixtureCore")) "Rename manifest"
    Assert-True ($renamed.Contains("PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE")) "Rename macro manifest"
    Assert-True ($renamed.Contains("PROJECT_DISPLAY_NAME=$unicodeDisplayName")) "UTF-8 display name preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("dirty")) "Pre-existing edit preservation"
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
Write-Host "Windows script regression tests passed."
