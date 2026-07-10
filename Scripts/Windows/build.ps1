[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan")]
    [string]$Configuration = "Debug",
    [string]$Target = "Client",
    [switch]$Generate
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$WorkspaceName = "CrossPlatformCoreClientTemplate"

function Get-NinjaExecutable {
    $command = Get-Command "ninja" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) {
        return $link
    }

    $packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (Test-Path $packageRoot) {
        $packageNinja = Get-ChildItem -Path $packageRoot -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($packageNinja) {
            return $packageNinja.FullName
        }
    }

    throw "Ninja was not found. Run Scripts\Windows\bootstrap.ps1 -Generators ninja."
}

function Get-MSBuild {
    param([int]$MajorVersion)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Run Scripts\Windows\bootstrap.ps1 for the requested Visual Studio generator."
    }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $matches = & $vswhere -latest -products * -version $range -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe"
    $msbuild = $matches | Select-Object -First 1

    if (-not $msbuild) {
        throw "MSBuild was not found for Visual Studio major version $MajorVersion."
    }

    return $msbuild
}

function Invoke-VSBuild {
    param([int]$MajorVersion)

    $solutionName = if ($Generator -eq "vs2026") { "$WorkspaceName.slnx" } else { "$WorkspaceName.sln" }
    $solution = Join-Path $Root $solutionName

    if ($Generate -or -not (Test-Path $solution)) {
        & (Join-Path $PSScriptRoot "generate.ps1") -Generator $Generator
    }

    if (-not (Test-Path $solution)) {
        $fallbackSolution = Join-Path $Root "$WorkspaceName.sln"
        if (Test-Path $fallbackSolution) {
            $solution = $fallbackSolution
        }
        else {
            throw "Expected generated solution was not found: $solution"
        }
    }

    $msbuild = Get-MSBuild -MajorVersion $MajorVersion
    Write-Host "==> Building $Target $Configuration with $Generator"
    & $msbuild $solution "/m" "/t:$Target" "/p:Configuration=$Configuration" "/p:Platform=x64"
}

function Invoke-NinjaBuild {
    if ($Generate -or -not (Test-Path (Join-Path $Root "build.ninja"))) {
        & (Join-Path $PSScriptRoot "generate.ps1") -Generator "ninja"
    }

    Write-Host "==> Building $Target $Configuration with Ninja"
    $ninja = Get-NinjaExecutable
    & $ninja -C $Root -f "build.ninja" "$($Target)_$Configuration"
}

function Invoke-GMakeBuild {
    $make = Get-Command "make" -ErrorAction SilentlyContinue
    if (-not $make) {
        $make = Get-Command "mingw32-make" -ErrorAction SilentlyContinue
    }
    if (-not $make) {
        throw "GNU Make was not found. Run Scripts\Windows\bootstrap.ps1 -Generators gmake."
    }

    if ($Generate -or -not (Test-Path (Join-Path $Root "Makefile"))) {
        & (Join-Path $PSScriptRoot "generate.ps1") -Generator "gmake"
    }

    Write-Host "==> Building $Target $Configuration with GNU Make"
    & $make.Source -C $Root "config=$($Configuration.ToLowerInvariant())" $Target
}

switch ($Generator) {
    "vs2019" { Invoke-VSBuild -MajorVersion 16 }
    "vs2022" { Invoke-VSBuild -MajorVersion 17 }
    "vs2026" { Invoke-VSBuild -MajorVersion 18 }
    "ninja" { Invoke-NinjaBuild }
    "gmake" { Invoke-GMakeBuild }
}
