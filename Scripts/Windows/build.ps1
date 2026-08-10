[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [string]$Target = "",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Project = Get-ProjectConfig
$WorkspaceName = $Project.PROJECT_IDENTIFIER
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$Target = if ($Target) { $Target } else { $Project.CLIENT_TARGET }
$expectedStamp = "$Generator|$Architecture|$Toolset|$([bool]$CI)|$(Get-ProjectGenerationFingerprint $Root)"
$stamp = Join-Path $Root "Build\Generated\$Generator.stamp"

Assert-SupportedBuildCombination $Generator $Configuration $Architecture $Toolset

function Invoke-GenerationIfNeeded {
    param([string]$ExpectedFile)
    $stampMatches = (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $expectedStamp)
    if ($Generate -or $Update -or -not (Test-Path (Join-Path $Root $ExpectedFile)) -or -not $stampMatches) {
        & (Join-Path $PSScriptRoot "generate.ps1") -Generator $Generator -Architecture $Architecture `
            -Toolset $Toolset -CI:$CI -Update:$Update -Force:$Generate
    }
}

function Get-NinjaExecutable {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) { return $link }
    throw "Ninja was not found. Run bootstrap for the Ninja generator."
}

function Invoke-ManagedBuild {
    Write-Host "==> Building managed runtime API"
    & (Join-Path $PSScriptRoot "build-managed.ps1")
    if ($LASTEXITCODE -ne 0) { throw "Managed runtime API build failed with exit code $LASTEXITCODE." }
}

Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build-info.ps1") } "Build metadata generation"

switch ($Generator) {
    { $_ -like "vs*" } {
        $majorVersion = Get-VisualStudioMajorVersion $Generator
        $solutionName = if ($Generator -eq "vs2026") { "$WorkspaceName.slnx" } else { "$WorkspaceName.sln" }
        Invoke-GenerationIfNeeded $solutionName
        Invoke-ManagedBuild
        $environment = Get-VSBuildEnvironment $majorVersion
        $platform = Get-MSBuildPlatform $Architecture
        Write-Host "==> Building $Target $Configuration for $Architecture with $Generator"
        & $environment.MSBuild (Join-Path $Root $solutionName) "/m" "/t:$Target" `
            "/p:Configuration=$Configuration" "/p:Platform=$platform" `
            "/p:VCTargetsPath=$($environment.VCTargetsPath)"
        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE." }
        break
    }
    "ninja" {
        Invoke-GenerationIfNeeded "build.ninja"
        Invoke-ManagedBuild
        Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
        Write-Host "==> Building $Target $Configuration for $Architecture with Ninja"
        & (Get-NinjaExecutable) -C $Root -f build.ninja "$($Target)_$Configuration"
        if ($LASTEXITCODE -ne 0) { throw "Ninja failed with exit code $LASTEXITCODE." }
        break
    }
    "gmake" {
        Invoke-GenerationIfNeeded "Makefile"
        Invoke-ManagedBuild
        Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
        $make = Get-Command mingw32-make, make -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $make) {
            $make = @("C:\msys64\ucrt64\bin\mingw32-make.exe", "C:\msys64\mingw64\bin\mingw32-make.exe") |
                Where-Object { Test-Path $_ } | Select-Object -First 1
            if ($make) {
                $env:PATH = "$(Split-Path $make);$env:PATH"
                $make = Get-Item $make
            }
        }
        if (-not $make) { throw "GNU Make was not found in PATH or a standard MSYS2 installation." }
        Write-Host "==> Building $Target $Configuration for $Architecture with GNU Make"
        & $make.Source -C $Root "config=$($Configuration.ToLowerInvariant())" $Target
        if ($LASTEXITCODE -ne 0) { throw "GNU Make failed with exit code $LASTEXITCODE." }
        break
    }
}

& (Join-Path $PSScriptRoot "stage-managed-host.ps1") -Root $Root -Configuration $Configuration `
    -Architecture $Architecture -Target $Target -IfPresent

if ($Target -eq $Project.HUB_TARGET) {
    $outputArchitecture = Get-ArchitectureOutputName $Architecture
    $dependencyConfiguration = if ($Configuration -in @("Release", "Dist")) { "Release" } else { "Debug" }
    $sodiumRuntime = Join-Path $Root `
        "Build\Dependencies\windows-$outputArchitecture-$Toolset\$dependencyConfiguration\install\bin\libsodium.dll"
    $hubDirectory = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$Target"
    if (-not (Test-Path -LiteralPath $sodiumRuntime -PathType Leaf)) {
        throw "The pinned Hub signature verifier runtime is missing: $sodiumRuntime"
    }
    Copy-Item -LiteralPath $sodiumRuntime -Destination $hubDirectory -Force
    Write-Host "==> Staged pinned Hub signature verifier for $Target"
}
