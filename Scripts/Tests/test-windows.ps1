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
Assert-Equal $project.PROJECT_IDENTIFIER "CrossPlatformCoreClientTemplate" "Project manifest"
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

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("template-script-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    foreach ($directory in @("Scripts\Windows", "Config", "Core\Include\Core", "Core\Source", "Client\Source", "Tests\Source", "Vendor", "Build\Bin")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "rename.ps1"), (Join-Path $Windows "clean.ps1"), (Join-Path $Windows "doctor.ps1") (Join-Path $fixture "Scripts\Windows")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Project.conf") (Join-Path $fixture "Config\Project.conf")
    Set-Content (Join-Path $fixture "Core\Include\Core\Core.h") 'namespace Core { const char* GetName(); }'
    Set-Content (Join-Path $fixture "Core\Source\Core.cpp") '#include "Core/Core.h"'
    Set-Content (Join-Path $fixture "Client\Source\Main.cpp") '#include "Core/Core.h"'
    Set-Content (Join-Path $fixture "Tests\Source\Main.cpp") '#include "Core/Core.h"'
    Set-Content (Join-Path $fixture "README.md") 'CrossPlatformCoreClientTemplate hcfgod/C-Cross-Platform-Core-Client-Template'
    Set-Content (Join-Path $fixture "Vendor\keep.txt") 'vendor'
    Set-Content (Join-Path $fixture "Build\Bin\remove.txt") 'build'

    & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName "Script Fixture" -Repository example/script-fixture
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Include\ScriptFixture\Core.h")) "Rename structure"
    $renamed = Get-Content (Join-Path $fixture "Config\Project.conf") -Raw
    Assert-True ($renamed.Contains("CORE_TARGET=ScriptFixtureCore")) "Rename manifest"
    & (Join-Path $fixture "Scripts\Windows\doctor.ps1") -Generator ninja -Architecture x86_64 -Toolset clang

    & (Join-Path $fixture "Scripts\Windows\clean.ps1") -Scope full
    Assert-True (-not (Test-Path (Join-Path $fixture "Build\Bin"))) "Full clean build removal"
    Assert-True (Test-Path (Join-Path $fixture "Vendor\keep.txt")) "Full clean vendor preservation"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Source\Core.cpp")) "Full clean source preservation"
}
finally {
    if ($fixture.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path $fixture)) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
Write-Host "Windows script regression tests passed."
