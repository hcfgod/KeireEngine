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
$ToolsDir = Join-Path $Root "Tools\Windows"
$PremakeVersion = "5.0.0-beta8"
$PremakeHash = "e64ce2ed8778e0098f63674cca61fe33941b5f0c8d9a4afd651152bdea3758ab"
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
        $valid = ((& $PremakeExe --version 2>$null) -join "`n") -match [regex]::Escape($PremakeVersion)
    }
    if ($valid) { Write-Step "Premake $PremakeVersion already installed"; return }

    $tempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("premake-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempDirectory | Out-Null
    try {
        $archive = Join-Path $tempDirectory "premake.zip"
        $url = "https://github.com/premake/premake-core/releases/download/v$PremakeVersion/premake-$PremakeVersion-windows.zip"
        Write-Step "Downloading verified Premake $PremakeVersion"
        Invoke-WebRequest -Uri $url -OutFile $archive
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
    $bin = @("C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin") |
        Where-Object { Test-Path (Join-Path $_ "g++.exe") } | Select-Object -First 1
    if (-not $bin) { throw "An MSYS2 GCC environment was not found under C:\msys64." }
    $env:PATH = "$bin;$env:PATH"
    Assert-MinimumVersion "GCC" ((& (Join-Path $bin "g++.exe") -dumpfullversion -dumpversion) -join "") ([version]"12.0")
    Assert-MinimumVersion "GNU Make" ((& (Join-Path $bin "mingw32-make.exe") --version) -join "`n") ([version]"4.3")
}

Install-Premake
Ensure-CommandPackage git "Git.Git"
Assert-MinimumVersion "Git" ((& git --version) -join "") ([version]"2.40")

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
