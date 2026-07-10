[CmdletBinding()]
param(
    [string[]]$Generators = @(),
    [switch]$InstallOptional,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$ToolsDir = Join-Path $Root "Tools\Windows"
$PremakeVersion = "5.0.0-beta8"
$PremakeExe = Join-Path $ToolsDir "premake5.exe"

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Test-Command {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-Ninja {
    if (Test-Command "ninja") {
        return $true
    }

    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) {
        return $true
    }

    $packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    return (Test-Path $packageRoot) -and ($null -ne (Get-ChildItem -Path $packageRoot -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1))
}

function Invoke-WingetInstall {
    param(
        [Parameter(Mandatory = $true)][string]$PackageId,
        [string]$Override = ""
    )

    if (-not (Test-Command "winget")) {
        throw "winget is required for automatic Windows package installs."
    }

    Write-Step "Installing or updating $PackageId"
    $args = @(
        "install",
        "--id", $PackageId,
        "--exact",
        "--silent",
        "--accept-package-agreements",
        "--accept-source-agreements"
    )

    if ($Override) {
        $args += @("--override", $Override)
    }

    & winget @args
}

function Get-PremakeAssetUrl {
    $releaseUrl = "https://api.github.com/repos/premake/premake-core/releases/tags/v$PremakeVersion"
    $fallbackUrl = "https://github.com/premake/premake-core/releases/download/v$PremakeVersion/premake-$PremakeVersion-windows.zip"

    try {
        $release = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "cross-platform-core-client-template-bootstrap" }
        $asset = $release.assets |
            Where-Object { $_.name -match "windows" -and $_.name -match "\.zip$" } |
            Select-Object -First 1

        if ($asset) {
            return $asset.browser_download_url
        }
    }
    catch {
        Write-Warning "Could not query GitHub release metadata. Falling back to the known Premake asset URL."
    }

    return $fallbackUrl
}

function Install-Premake {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

    $needsInstall = $Force -or -not (Test-Path $PremakeExe)
    if (-not $needsInstall) {
        $versionOutput = (& $PremakeExe --version 2>$null) -join "`n"
        $needsInstall = $versionOutput -notmatch [regex]::Escape($PremakeVersion)
    }

    if (-not $needsInstall) {
        Write-Step "Premake $PremakeVersion already installed at $PremakeExe"
        return
    }

    Write-Step "Downloading Premake $PremakeVersion"
    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("premake-" + [guid]::NewGuid().ToString("N"))
    $archive = Join-Path $tempDir "premake.zip"

    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
    try {
        Invoke-WebRequest -Uri (Get-PremakeAssetUrl) -OutFile $archive
        Expand-Archive -Path $archive -DestinationPath $tempDir -Force

        $downloadedExe = Get-ChildItem -Path $tempDir -Filter "premake5.exe" -Recurse | Select-Object -First 1
        if (-not $downloadedExe) {
            throw "Downloaded Premake archive did not contain premake5.exe."
        }

        Copy-Item -Path $downloadedExe.FullName -Destination $PremakeExe -Force
        Write-Step "Installed Premake at $PremakeExe"
    }
    finally {
        Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-VSWhere {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        return $vswhere
    }

    return $null
}

function Test-VisualStudioCpp {
    param([int]$MajorVersion)

    $vswhere = Get-VSWhere
    if (-not $vswhere) {
        return $false
    }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $path = & $vswhere -latest -products * -version $range -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath
    return -not [string]::IsNullOrWhiteSpace(($path -join ""))
}

function Ensure-VisualStudioCpp {
    param(
        [int]$MajorVersion,
        [string]$PackageId
    )

    if (Test-VisualStudioCpp $MajorVersion) {
        Write-Step "Visual Studio $MajorVersion C++ toolchain already installed"
        return
    }

    $override = "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
    Invoke-WingetInstall -PackageId $PackageId -Override $override
}

function Ensure-GeneratorPrerequisites {
    param([string]$Generator)

    switch ($Generator) {
        "vs2019" { Ensure-VisualStudioCpp -MajorVersion 16 -PackageId "Microsoft.VisualStudio.2019.BuildTools" }
        "vs2022" { Ensure-VisualStudioCpp -MajorVersion 17 -PackageId "Microsoft.VisualStudio.2022.BuildTools" }
        "vs2026" { Ensure-VisualStudioCpp -MajorVersion 18 -PackageId "Microsoft.VisualStudio.BuildTools" }
        "ninja" {
            if (-not (Test-Ninja)) {
                Invoke-WingetInstall -PackageId "Ninja-build.Ninja"
            }
            else {
                Write-Step "Ninja already available"
            }
        }
        "gmake" {
            if (-not (Test-Command "make") -and -not (Test-Command "mingw32-make")) {
                Invoke-WingetInstall -PackageId "MSYS2.MSYS2"
            }
            else {
                Write-Step "GNU Make already available"
            }
        }
        "compilecommands" {
            Ensure-GeneratorPrerequisites -Generator "ninja"
        }
        default {
            if ($Generator) {
                throw "Unsupported Windows generator '$Generator'."
            }
        }
    }
}

Install-Premake

if (-not (Test-Command "git")) {
    Invoke-WingetInstall -PackageId "Git.Git"
}
else {
    Write-Step "Git already available"
}

if ($InstallOptional) {
    foreach ($package in @("Ninja-build.Ninja", "LLVM.LLVM", "MSYS2.MSYS2")) {
        Invoke-WingetInstall -PackageId $package
    }
}

foreach ($generator in $Generators) {
    Ensure-GeneratorPrerequisites -Generator $generator
}

& (Join-Path $PSScriptRoot "vendor.ps1") -Force:$Force

Write-Step "Windows prerequisites are ready"
