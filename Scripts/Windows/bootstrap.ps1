[CmdletBinding()]
param(
    [string[]]$Generators = @(),
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$InstallOptional,
    [switch]$Update,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Lock = Get-DependencyLock
$ToolsDir = Join-Path $Root "Tools\Windows"
$PremakeVersion = $Lock.PREMAKE_VERSION
$PremakeUrl = $Lock.PREMAKE_WINDOWS_X86_64_URL
$PremakeHash = $Lock.PREMAKE_WINDOWS_X86_64_SHA256
$PremakeExe = Join-Path $ToolsDir "premake5.exe"
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }

function Write-Step([string]$Message) { Write-Host "==> $Message" }
function Test-Command([string]$Name) { return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue) }

function Assert-MinimumVersion {
    param([string]$Name, [string]$VersionText, [version]$Minimum)
    $match = [regex]::Match($VersionText, '\d+(?:\.\d+)+')
    if (-not $match.Success -or [version]$match.Value -lt $Minimum) {
        throw "$Name version '$VersionText' is older than required $Minimum. Rerun bootstrap with -Update."
    }
}

function Test-WingetPackageInstalled([string]$PackageId) {
    & winget list --id $PackageId --exact --disable-interactivity | Out-Null
    return $LASTEXITCODE -eq 0
}

function Invoke-WingetPackage {
    param([string]$PackageId, [string]$Override = "")
    if (-not (Test-Command winget)) { throw "winget is required for automatic Windows package installation." }
    $verb = if ($Update -and (Test-WingetPackageInstalled $PackageId)) { "upgrade" } else { "install" }
    $arguments = @($verb, "--id", $PackageId, "--exact", "--silent", "--accept-package-agreements", "--accept-source-agreements")
    if ($Override) { $arguments += @("--override", $Override) }
    Write-Step "$verb $PackageId"
    & winget @arguments
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
        throw "winget $verb failed for $PackageId with exit code $LASTEXITCODE."
    }
}

function Get-InstalledVisualStudioPackageId {
    param([pscustomobject]$Environment, [int]$MajorVersion, [string]$FallbackPackageId)

    if (-not (Test-Command winget)) { throw "winget is required to update Visual Studio automatically." }

    $sku = $Environment.ProductId -replace '^Microsoft\.VisualStudio\.Product\.', ''
    $year = switch ($MajorVersion) {
        16 { "2019" }
        17 { "2022" }
        18 { "2026" }
        default { "" }
    }
    $candidates = @()
    if ($year -and $sku) { $candidates += "Microsoft.VisualStudio.$year.$sku" }
    if ($sku) { $candidates += "Microsoft.VisualStudio.$sku" }
    $candidates += $FallbackPackageId

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-WingetPackageInstalled $candidate) { return $candidate }
    }
    return $null
}

function Install-Premake {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $valid = $false
    if (-not $Force -and (Test-Path $PremakeExe)) {
        # Premake beta8 attempts to resolve the current working directory as
        # UTF-8 even for --version. Query from an ASCII temporary directory so
        # repositories with names such as Kéire do not trigger a false reinstall.
        Push-Location ([IO.Path]::GetTempPath())
        try { $versionText = (& $PremakeExe --version 2>$null) -join "`n" }
        finally { Pop-Location }
        $valid = $versionText -match [regex]::Escape($PremakeVersion)
    }
    if ($valid) { Write-Step "Premake $PremakeVersion already installed"; return }

    $tempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("premake-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempDirectory | Out-Null
    try {
        $archive = Join-Path $tempDirectory "premake.zip"
        Write-Step "Downloading verified Premake $PremakeVersion"
        Invoke-WebRequest -Uri $PremakeUrl -OutFile $archive
        $actualHash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
        if ($actualHash -ne $PremakeHash) { throw "Premake archive checksum mismatch." }
        Expand-Archive $archive -DestinationPath $tempDirectory -Force
        $downloaded = Get-ChildItem $tempDirectory -Filter premake5.exe -Recurse | Select-Object -First 1
        if (-not $downloaded) { throw "Premake archive did not contain premake5.exe." }
        Copy-Item $downloaded.FullName $PremakeExe -Force
    }
    finally { Remove-Item $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue }
}

function Ensure-VisualStudio {
    param([int]$MajorVersion, [string]$PackageId)
    $environment = $null
    $installation = $null
    try {
        $environment = Get-VSBuildEnvironment $MajorVersion
        $installation = $environment
        if (-not $Update) {
            Write-Step "Visual Studio $MajorVersion C++ toolchain available"
            return
        }
    }
    catch {
        Write-Verbose "Visual Studio $MajorVersion is missing or incomplete: $($_.Exception.Message)"
        $installation = Get-VSInstallation $MajorVersion
    }

    if ($installation) {
        $installedPackageId = Get-InstalledVisualStudioPackageId $installation $MajorVersion $PackageId
        if (-not $installedPackageId) {
            Write-Warning "$($installation.DisplayName) is not managed by Winget; update it with Visual Studio Installer."
            return
        }
        $PackageId = $installedPackageId
    }

    $override = if ($environment) { "" } else { "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended" }
    Invoke-WingetPackage $PackageId $override
    Get-VSBuildEnvironment $MajorVersion | Out-Null
}

function Ensure-CommandPackage([string]$Command, [string]$Package) {
    if (-not (Test-Command $Command) -or $Update) { Invoke-WingetPackage $Package }
    else { Write-Step "$Command already available" }
}

function Ensure-LLVM {
    $llvmBin = "C:\Program Files\LLVM\bin"
    if (Test-Path (Join-Path $llvmBin "clang.exe")) {
        $env:PATH = "$llvmBin;$env:PATH"
    }
    if (-not (Test-Command clang) -or $Update) {
        Invoke-WingetPackage "LLVM.LLVM"
        if (Test-Path $llvmBin) { $env:PATH = "$llvmBin;$env:PATH" }
    }
    else { Write-Step "clang already available" }
    Assert-MinimumVersion "Clang" ((& clang --version) -join "`n") ([version]"16.0")
}

function Ensure-MSYS2 {
    if ($Update -or -not (Test-Path "C:\msys64")) { Invoke-WingetPackage "MSYS2.MSYS2" }
    & "C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm make diffutils"
    if ($LASTEXITCODE -ne 0) { throw "Could not install the MSYS2 tools required for source-built FFmpeg." }
    $bin = Add-MSYS2ToPath
    Assert-MinimumVersion "GCC" ((& (Join-Path $bin "g++.exe") -dumpfullversion -dumpversion) -join "") ([version]"12.0")
    Assert-MinimumVersion "GNU Make" ((& (Join-Path $bin "mingw32-make.exe") --version) -join "`n") ([version]"4.3")
}

Install-Premake
Ensure-CommandPackage git "Git.Git"
Assert-MinimumVersion "Git" ((& git --version) -join "") ([version]"2.40")
$cmakeBin = "C:\Program Files\CMake\bin"
if (Test-Path (Join-Path $cmakeBin "cmake.exe")) { $env:PATH = "$cmakeBin;$env:PATH" }
Ensure-CommandPackage cmake "Kitware.CMake"
if (Test-Path (Join-Path $cmakeBin "cmake.exe")) { $env:PATH = "$cmakeBin;$env:PATH" }
Assert-MinimumVersion "CMake" ((& cmake --version) -join "") ([version]"3.20")
Ensure-CommandPackage ninja "Ninja-build.Ninja"
Assert-MinimumVersion "Ninja" ((& (Get-NinjaExecutable) --version) -join "") ([version]"1.11")
if (-not (Test-Path "C:\msys64\usr\bin\make.exe")) {
    if (-not (Test-Path "C:\msys64")) { Invoke-WingetPackage "MSYS2.MSYS2" }
    & "C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm make diffutils"
    if ($LASTEXITCODE -ne 0) { throw "Could not install the source-build tools required by FFmpeg." }
}

foreach ($generator in $Generators) {
    switch ($generator) {
        "vs2019" { Ensure-VisualStudio 16 "Microsoft.VisualStudio.2019.BuildTools" }
        "vs2022" { Ensure-VisualStudio 17 "Microsoft.VisualStudio.2022.BuildTools" }
        "vs2026" { Ensure-VisualStudio 18 "Microsoft.VisualStudio.BuildTools" }
        "ninja" {
            Ensure-CommandPackage ninja "Ninja-build.Ninja"
            Assert-MinimumVersion "Ninja" ((& (Get-NinjaExecutable) --version) -join "") ([version]"1.11")
            if ($Toolset -in @("default", "msc")) { Ensure-VisualStudio 17 "Microsoft.VisualStudio.2022.BuildTools" }
            elseif ($Toolset -eq "clang") { Ensure-LLVM }
            elseif ($Toolset -eq "gcc") { Ensure-MSYS2 }
        }
        "gmake" { Ensure-MSYS2 }
        "compilecommands" {
            Ensure-CommandPackage ninja "Ninja-build.Ninja"
            Assert-MinimumVersion "Ninja" ((& (Get-NinjaExecutable) --version) -join "") ([version]"1.11")
            if ($Toolset -in @("default", "msc")) { Ensure-VisualStudio 17 "Microsoft.VisualStudio.2022.BuildTools" }
            elseif ($Toolset -eq "clang") { Ensure-LLVM }
            elseif ($Toolset -eq "gcc") { Ensure-MSYS2 }
        }
        default { throw "Unsupported Windows generator '$generator'." }
    }
}

if ($InstallOptional) {
    foreach ($package in @("Ninja-build.Ninja", "LLVM.LLVM", "MSYS2.MSYS2")) { Invoke-WingetPackage $package }
}

& (Join-Path $PSScriptRoot "vendor.ps1")
Write-Step "Windows prerequisites are ready for $Architecture"
