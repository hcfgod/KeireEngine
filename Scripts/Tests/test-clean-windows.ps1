$ErrorActionPreference = "Stop"

function Assert-CleanState {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function New-CleanFixture {
    $fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-clean-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force (Join-Path $fixture "Scripts\Windows") | Out-Null
    Copy-Item (Join-Path $PSScriptRoot "..\Windows\clean.ps1") (Join-Path $fixture "Scripts\Windows\clean.ps1")
    @'
function Get-ProjectConfig {
    return [pscustomobject]@{
        CORE_DIRECTORY = "KeireCore"
        CLIENT_DIRECTORY = "KeireClient"
        HUB_DIRECTORY = "KeireHub"
        TESTS_DIRECTORY = "KeireTests"
    }
}
'@ | Set-Content (Join-Path $fixture "Scripts\Windows\common.ps1") -Encoding UTF8
    foreach ($directory in @("KeireCore", "KeireClient", "KeireHub", "KeireTests")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    return $fixture
}

$launcherScript = Get-Content (Join-Path $PSScriptRoot "..\project.ps1") -Raw
Assert-CleanState ($launcherScript.Contains('[string]$CleanScope = "full"')) `
    "The Windows launcher does not default clean to the full scope."
Assert-CleanState ($launcherScript.Contains('"clean.ps1") -Scope $CleanScope')) `
    "The Windows launcher does not forward the selected clean scope."

$fullFixture = New-CleanFixture
try {
    New-Item -ItemType Directory -Force (Join-Path $fullFixture "Build\Dependencies") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $fullFixture "Build\UnclassifiedOutput") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $fullFixture "Artifacts") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $fullFixture "Vendor") | Out-Null
    "cache" | Set-Content (Join-Path $fullFixture "Build\Dependencies\cache.bin") -Encoding ASCII
    "stale" | Set-Content (Join-Path $fullFixture "Build\UnclassifiedOutput\stale.txt") -Encoding ASCII
    "archive" | Set-Content (Join-Path $fullFixture "Artifacts\package.zip") -Encoding ASCII
    "generated" | Set-Content (Join-Path $fullFixture "Fixture.sln") -Encoding ASCII
    "generated" | Set-Content (Join-Path $fullFixture "KeireCore\Fixture.vcxproj") -Encoding ASCII
    "keep" | Set-Content (Join-Path $fullFixture "Vendor\sentinel.txt") -Encoding ASCII

    & (Join-Path $fullFixture "Scripts\Windows\clean.ps1") -Scope full

    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $fullFixture "Build"))) `
        "Full Windows clean left the Build directory behind."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $fullFixture "Artifacts"))) `
        "Full Windows clean left package artifacts behind."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $fullFixture "Fixture.sln"))) `
        "Full Windows clean left root generated files behind."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $fullFixture "KeireCore\Fixture.vcxproj"))) `
        "Full Windows clean left project generated files behind."
    Assert-CleanState (Test-Path -LiteralPath (Join-Path $fullFixture "Vendor\sentinel.txt")) `
        "Full Windows clean removed repository inputs."
}
finally {
    Remove-Item -LiteralPath $fullFixture -Recurse -Force -ErrorAction SilentlyContinue
}

$buildFixture = New-CleanFixture
try {
    foreach ($directory in @("Dependencies", "Generated", "Projects", "Bin", "Managed", "UnknownOutput")) {
        New-Item -ItemType Directory -Force (Join-Path $buildFixture "Build\$directory") | Out-Null
        "fixture" | Set-Content (Join-Path $buildFixture "Build\$directory\sentinel.txt") -Encoding ASCII
    }
    "loose" | Set-Content (Join-Path $buildFixture "Build\loose-output.txt") -Encoding ASCII
    New-Item -ItemType Directory -Force (Join-Path $buildFixture "Artifacts") | Out-Null

    & (Join-Path $buildFixture "Scripts\Windows\clean.ps1") -Scope build

    foreach ($preserved in @("Dependencies", "Generated", "Projects")) {
        Assert-CleanState (Test-Path -LiteralPath (Join-Path $buildFixture "Build\$preserved\sentinel.txt")) `
            "Windows build clean removed preserved $preserved state."
    }
    foreach ($removed in @("Bin", "Managed", "UnknownOutput")) {
        Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $buildFixture "Build\$removed"))) `
            "Windows build clean left $removed output behind."
    }
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $buildFixture "Build\loose-output.txt"))) `
        "Windows build clean left a loose output behind."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $buildFixture "Artifacts"))) `
        "Windows build clean left package artifacts behind."
}
finally {
    Remove-Item -LiteralPath $buildFixture -Recurse -Force -ErrorAction SilentlyContinue
}

$generatedFixture = New-CleanFixture
try {
    foreach ($directory in @("Bin", "Dependencies", "Generated", "Projects")) {
        New-Item -ItemType Directory -Force (Join-Path $generatedFixture "Build\$directory") | Out-Null
        "fixture" | Set-Content (Join-Path $generatedFixture "Build\$directory\sentinel.txt") -Encoding ASCII
    }

    & (Join-Path $generatedFixture "Scripts\Windows\clean.ps1") -Scope generated

    Assert-CleanState (Test-Path -LiteralPath (Join-Path $generatedFixture "Build\Bin\sentinel.txt")) `
        "Windows generated clean removed build output."
    Assert-CleanState (Test-Path -LiteralPath (Join-Path $generatedFixture "Build\Dependencies\sentinel.txt")) `
        "Windows generated clean removed dependency state."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $generatedFixture "Build\Generated"))) `
        "Windows generated clean left build identity behind."
    Assert-CleanState (-not (Test-Path -LiteralPath (Join-Path $generatedFixture "Build\Projects"))) `
        "Windows generated clean left generated projects behind."
}
finally {
    Remove-Item -LiteralPath $generatedFixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows clean regression tests passed."
