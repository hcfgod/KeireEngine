[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake", "compilecommands")]
    [string]$Generator = "vs2022",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$Update,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$PremakeExe = Join-Path $Root "Tools\Windows\premake5.exe"
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset

Assert-SupportedBuildCombination $Generator "Debug" $Architecture $Toolset
& (Join-Path $PSScriptRoot "bootstrap.ps1") -Generators @($Generator) -Architecture $Architecture `
    -Toolset $Toolset -Update:$Update

$premakeArchitecture = Get-PremakeArchitecture $Architecture
$arguments = @("--file=$(Join-Path $Root 'premake5.lua')", "--arch=$premakeArchitecture", "--toolset=$Toolset")
if ($CI) { $arguments += "--ci" }
Write-Host "==> Generating $Generator files for $Architecture with toolset $Toolset"
Push-Location $Root
try {
    if ($Generator -eq "compilecommands") {
        & $PremakeExe @arguments ninja
    }
    else {
        & $PremakeExe @arguments $Generator
    }
    $generationExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($generationExitCode -ne 0) {
    throw "Premake generation failed with exit code $generationExitCode."
}
if ($Generator -eq "compilecommands") {
    $ruleToolset = $Toolset
    $database = & (Get-NinjaExecutable) -C $Root -f build.ninja -t compdb "cxx_$ruleToolset"
    if ($LASTEXITCODE -ne 0) {
        throw "Ninja compile database generation failed with exit code $LASTEXITCODE."
    }
    $debugCommands = ($database -join "`n" | ConvertFrom-Json) | Where-Object {
        $_.output -match 'Build[/\\]Intermediates[/\\]Debug-'
    }
    $debugCommands | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Root "compile_commands.json") -Encoding UTF8
}

$stampDirectory = Join-Path $Root "Build\Generated"
New-Item -ItemType Directory -Force -Path $stampDirectory | Out-Null
Set-Content -Path (Join-Path $stampDirectory "$Generator.stamp") `
    -Value "$Generator|$Architecture|$Toolset|$([bool]$CI)" -Encoding ASCII
