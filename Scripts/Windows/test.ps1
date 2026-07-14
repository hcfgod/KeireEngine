[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$TestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.TESTS_TARGET)\$($Project.TESTS_TARGET).exe"

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
    -Architecture $Architecture -Toolset $Toolset -Target $Project.TESTS_TARGET -CI:$CI -Update:$Update -Generate:$Generate
if (-not (Test-Path $TestsExe)) { throw "KeireTests executable was not found: $TestsExe" }

$originalPath = $env:PATH
$exitCode = 1
Push-Location $Root
try {
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -eq "msc")
    if ($Configuration -eq "DebugASan" -and $usesMSVC) {
        $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
        $runtimeDirectory = Get-MSVCASanRuntimeDirectory $majorVersion $Architecture
        Write-Host "==> Using MSVC AddressSanitizer runtime from $runtimeDirectory"
        $env:PATH = "$runtimeDirectory;$env:PATH"
    }
    Write-Host "==> Running KeireTests $Configuration for $Architecture"
    & $TestsExe
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0 -and $Configuration -in @("Debug", "DebugASan", "DebugUBSan", "DebugTSan")) {
        $probeOutput = Join-Path $env:TEMP ("core-assert-probe-" + [guid]::NewGuid().ToString("N") + ".txt")
        try {
            $probe = Start-Process -FilePath $TestsExe -ArgumentList "--core-assert-probe" -NoNewWindow -Wait `
                -PassThru -RedirectStandardError $probeOutput
            if ($probe.ExitCode -eq 0) { throw "Assertion probe unexpectedly succeeded." }
            $diagnostic = Get-Content $probeOutput -Raw
            if ($diagnostic -notmatch "Assertion failed: false" -or $diagnostic -notmatch "assertion probe") {
                throw "Assertion probe did not emit the required diagnostic."
            }
        }
        finally { Remove-Item $probeOutput -Force -ErrorAction SilentlyContinue }
    }
}
finally {
    $env:PATH = $originalPath
    Pop-Location
}
exit $exitCode
