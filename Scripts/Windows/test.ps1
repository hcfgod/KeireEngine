[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan")]
    [string]$Configuration = "Debug",
    [switch]$Generate
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$TestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-x86_64\Tests\Tests.exe"

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Target "Tests" -Generate:$Generate

if (-not (Test-Path $TestsExe)) {
    throw "Tests executable was not found: $TestsExe"
}

Write-Host "==> Running Tests $Configuration"
& $TestsExe
exit $LASTEXITCODE
