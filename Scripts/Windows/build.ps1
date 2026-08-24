[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [ValidateSet("auto", "off", "sccache")]
    [string]$CompilerCache = "auto",
    [string]$Target = "",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate,
    [switch]$ProfileBuild
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

function Enter-KeireBuildLock {
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    . (Join-Path $PSScriptRoot "generated-content-cache.ps1")
    return Enter-GeneratedContentLock -Name "native-build" -RepositoryRoot $RepositoryRoot `
        -Timeout ([TimeSpan]::FromHours(2)) `
        -WaitMessage "==> Another build is using this checkout; waiting for it to finish"
}

function Exit-KeireBuildLock {
    param([Parameter(Mandatory = $true)][Threading.Mutex]$Mutex)

    . (Join-Path $PSScriptRoot "generated-content-cache.ps1")
    Exit-GeneratedContentLock -Mutex $Mutex
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildTimer = [Diagnostics.Stopwatch]::StartNew()
$BuildSucceeded = $false
$BuildProfileBinaryLog = $null
$BuildLock = Enter-KeireBuildLock -RepositoryRoot $Root
try {
$Project = Get-ProjectConfig
$WorkspaceName = $Project.PROJECT_IDENTIFIER
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$CompilerCache = Resolve-CompilerCache $Generator $CompilerCache
$Target = if ($Target) { $Target } else { $Project.CLIENT_TARGET }
$expectedStamp = "$Generator|$Architecture|$Toolset|$CompilerCache|$([bool]$CI)|$(Get-ProjectGenerationFingerprint $Root)"
$stamp = Join-Path $Root "Build\Generated\$Generator.stamp"

Assert-SupportedBuildCombination $Generator $Configuration $Architecture $Toolset

function Invoke-GenerationIfNeeded {
    param([string]$ExpectedFile)
    $stampMatches = (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $expectedStamp)
    if ($Generate -or $Update -or -not (Test-Path (Join-Path $Root $ExpectedFile)) -or -not $stampMatches) {
        & (Join-Path $PSScriptRoot "generate.ps1") -Generator $Generator -Architecture $Architecture `
            -Toolset $Toolset -CompilerCache $CompilerCache -CI:$CI -Update:$Update
    }
}

function Get-NinjaExecutable {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) { return $link }
    throw "Ninja was not found. Run bootstrap for the Ninja generator."
}

switch ($Generator) {
    { $_ -like "vs*" } {
        $majorVersion = Get-VisualStudioMajorVersion $Generator
        $solutionName = if ($Generator -eq "vs2026") { "$WorkspaceName.slnx" } else { "$WorkspaceName.sln" }
        Invoke-GenerationIfNeeded $solutionName
        $environment = Get-VSBuildEnvironment $majorVersion
        $platform = Get-MSBuildPlatform $Architecture
        Write-Host "==> Building $Target $Configuration for $Architecture with $Generator"
        $msbuildArguments = @(
            (Join-Path $Root $solutionName), "/m", "/t:$Target", "/p:Configuration=$Configuration",
            "/p:Platform=$platform", "/p:VCTargetsPath=$($environment.VCTargetsPath)"
        )
        $hostArchitecture = if ($env:PROCESSOR_ARCHITEW6432) {
            $env:PROCESSOR_ARCHITEW6432
        } else {
            $env:PROCESSOR_ARCHITECTURE
        }
        if ($hostArchitecture -eq "AMD64") {
            $msbuildArguments += "/p:PreferredToolArchitecture=x64"
        }
        if ($ProfileBuild) {
            $profileDirectory = Join-Path $Root "Build\Reports\BuildProfiles"
            New-Item -ItemType Directory -Force -Path $profileDirectory | Out-Null
            $BuildProfileBinaryLog = Join-Path $profileDirectory "latest-$Generator-$Target-$Configuration.binlog"
            $msbuildArguments += "/bl:$BuildProfileBinaryLog"
        }
        & $environment.MSBuild @msbuildArguments
        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE." }
        break
    }
    "ninja" {
        Invoke-GenerationIfNeeded "build.ninja"
        Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
        Write-Host "==> Building $Target $Configuration for $Architecture with Ninja"
        $ninjaArguments = @("-C", $Root, "-f", "build.ninja")
        if ($ProfileBuild) { $ninjaArguments += @("-d", "stats") }
        $ninjaArguments += "$($Target)_$Configuration"
        & (Get-NinjaExecutable) @ninjaArguments
        if ($LASTEXITCODE -ne 0) { throw "Ninja failed with exit code $LASTEXITCODE." }
        break
    }
    "gmake" {
        Invoke-GenerationIfNeeded "Makefile"
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

if ($Generator -eq "ninja") {
    foreach ($managedHostTarget in @(Get-ManagedHostStagingTargets -Project $Project -Target $Target)) {
        & (Join-Path $PSScriptRoot "stage-managed-host.ps1") -Root $Root -Configuration $Configuration `
            -Architecture $Architecture -Target $managedHostTarget -IfPresent
    }
}

$assetWorkerTarget = "$($Project.PROJECT_NAMESPACE)AssetWorker"
$assetWorkerConsumers = @(
    $assetWorkerTarget,
    "$($Project.PROJECT_NAMESPACE)AssetTool",
    "$($Project.PROJECT_NAMESPACE)EditorTests",
    "$($Project.PROJECT_NAMESPACE)EditorDev"
)
if ($Target -in $assetWorkerConsumers) {
    $dependencyLock = Get-DependencyLock
    & (Join-Path $PSScriptRoot "stage-ffmpeg-runtime.ps1") -Root $Root -Configuration $Configuration `
        -Architecture $Architecture -Toolset $Toolset -ProjectNamespace $Project.PROJECT_NAMESPACE `
        -FfmpegCommit $dependencyLock.FFMPEG_COMMIT
}

$runtimeStagingTarget = if ($Target -eq "$($Project.PROJECT_NAMESPACE)EditorDev") {
    $Project.CLIENT_TARGET
}
else {
    $Target
}
if ($Generator -eq "ninja" -and $runtimeStagingTarget -in @($Project.HUB_TARGET, $Project.CLIENT_TARGET)) {
    $outputArchitecture = Get-ArchitectureOutputName $Architecture
    $dependencyConfiguration = if ($Configuration -in @("Release", "Dist")) { "Release" } else { "Debug" }
    $sodiumRuntime = Join-Path $Root `
        "Build\Dependencies\windows-$outputArchitecture-$Toolset\$dependencyConfiguration\install\bin\libsodium.dll"
    $targetDirectory = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$runtimeStagingTarget"
    if (-not (Test-Path -LiteralPath $sodiumRuntime -PathType Leaf)) {
        throw "The pinned marketplace signature verifier runtime is missing: $sodiumRuntime"
    }
    & (Join-Path $PSScriptRoot "copy-file-if-changed.ps1") -Source $sodiumRuntime `
        -Destination (Join-Path $targetDirectory "libsodium.dll")
    Write-Host "==> Staged pinned marketplace signature verifier for $runtimeStagingTarget"
}
$BuildSucceeded = $true
}
finally {
    Exit-KeireBuildLock -Mutex $BuildLock
    $BuildTimer.Stop()
    if ($ProfileBuild) {
        $profileDirectory = Join-Path $Root "Build\Reports\BuildProfiles"
        New-Item -ItemType Directory -Force -Path $profileDirectory | Out-Null
        $profilePath = Join-Path $profileDirectory "latest-$Generator-$Target-$Configuration.json"
        [ordered]@{
            SchemaVersion = 1
            TimestampUtc = [DateTime]::UtcNow.ToString("o")
            Generator = $Generator
            Configuration = $Configuration
            Architecture = $Architecture
            Toolset = $Toolset
            CompilerCache = $CompilerCache
            Target = $Target
            Succeeded = $BuildSucceeded
            ElapsedMilliseconds = [Math]::Round($BuildTimer.Elapsed.TotalMilliseconds, 3)
            BinaryLog = $(if ($BuildProfileBinaryLog) { $BuildProfileBinaryLog } else { $null })
        } | ConvertTo-Json | Set-Content -LiteralPath $profilePath -Encoding UTF8
        Write-Host "==> Build profile: $profilePath"
    }
}
