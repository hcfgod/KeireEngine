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
& (Join-Path $PSScriptRoot "dependencies.ps1") -Generator $Generator -Architecture $Architecture `
    -Toolset $Toolset -Force:$Force

$premakeArchitecture = Get-PremakeArchitecture $Architecture
# Premake beta8 cannot reliably parse a Unicode absolute --file path on
# Windows. Generation already runs from $Root, so keep the script path local.
$arguments = @("--file=premake5.lua", "--arch=$premakeArchitecture", "--toolset=$Toolset")
if ($CI) { $arguments += "--ci" }
Write-Host "==> Generating $Generator files for $Architecture with toolset $Toolset"
$premakeRoot = $Root.Path
$premakeJunction = $null
if ($premakeRoot -match '[^\x00-\x7F]') {
    # Premake beta8 cannot open its project script when the working directory
    # contains Unicode. An ASCII junction preserves the real project layout.
    $premakeJunction = Join-Path ([IO.Path]::GetTempPath()) ("premake-root-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Junction -Path $premakeJunction -Target $premakeRoot | Out-Null
    $premakeRoot = $premakeJunction
}
Push-Location $premakeRoot
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
    if ($premakeJunction -and (Test-Path -LiteralPath $premakeJunction)) {
        # Windows PowerShell 5 can throw a NullReferenceException when
        # Remove-Item targets a directory junction.
        [IO.Directory]::Delete($premakeJunction)
    }
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
