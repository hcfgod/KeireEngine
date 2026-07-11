$ErrorActionPreference = "Stop"

function Get-NativeArchitecture {
    if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq
        [System.Runtime.InteropServices.Architecture]::Arm64) {
        return "ARM64"
    }
    return "x86_64"
}

function Normalize-Architecture {
    param([string]$Architecture)

    switch ($Architecture.ToLowerInvariant()) {
        "x64" { return "x86_64" }
        "amd64" { return "x86_64" }
        "x86_64" { return "x86_64" }
        "arm64" { return "ARM64" }
        "aarch64" { return "ARM64" }
        default { throw "Unsupported architecture '$Architecture'. Expected x86_64 or ARM64." }
    }
}

function Get-MSBuildPlatform {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "ARM64" }
    return "x64"
}

function Get-PremakeArchitecture {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "aarch64" }
    return "x86_64"
}

function Get-ArchitectureOutputName {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "AARCH64" }
    return "x86_64"
}

function Get-VisualStudioMajorVersion {
    param([string]$Generator)
    switch ($Generator) {
        "vs2019" { return 16 }
        "vs2022" { return 17 }
        "vs2026" { return 18 }
        default { return $null }
    }
}

function Get-VSInstallation {
    param([int]$MajorVersion)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $json = (& $vswhere -latest -products * -version $range -format json) -join "`n"
    if (-not $json) { return $null }
    return @($json | ConvertFrom-Json)[0]
}

function Get-VSBuildEnvironment {
    param([int]$MajorVersion)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Bootstrap the requested Visual Studio generator first."
    }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $installationJson = (& $vswhere -products * -version $range `
        -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Workload.NativeDesktop -format json) -join "`n"
    $installations = if ($installationJson) { @($installationJson | ConvertFrom-Json) } else { @() }

    foreach ($installation in $installations) {
        $installationPath = $installation.installationPath
        $msbuild = @(
            (Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $installationPath "MSBuild\15.0\Bin\MSBuild.exe")
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1

        $vcTargetsPath = Join-Path $installationPath "MSBuild\Microsoft\VC\v$($MajorVersion)0"
        if ($msbuild -and (Test-Path (Join-Path $vcTargetsPath "Microsoft.Cpp.Default.props"))) {
            return [pscustomobject]@{
                InstallationPath = $installationPath
                InstallationVersion = $installation.installationVersion
                ProductId = $installation.productId
                DisplayName = $installation.displayName
                MSBuild = $msbuild
                VCTargetsPath = $vcTargetsPath.Replace("\", "/") + "/"
                VsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
            }
        }
    }

    throw "A complete Visual Studio $MajorVersion C++ build environment was not found."
}

function Enter-VSDeveloperEnvironment {
    param(
        [int]$MajorVersion,
        [string]$Architecture
    )

    $environment = Get-VSBuildEnvironment $MajorVersion
    $targetArchitecture = if ((Normalize-Architecture $Architecture) -eq "ARM64") { "arm64" } else { "amd64" }
    $output = & $env:ComSpec /s /c "`"$($environment.VsDevCmd)`" -no_logo -arch=$targetArchitecture -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio developer environment setup failed with exit code $LASTEXITCODE."
    }
    foreach ($line in $output) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            [System.Environment]::SetEnvironmentVariable(
                $line.Substring(0, $separator),
                $line.Substring($separator + 1),
                "Process")
        }
    }
    return $environment
}

function Get-MSVCASanRuntimeDirectory {
    param(
        [int]$MajorVersion,
        [string]$Architecture
    )

    $environment = Get-VSBuildEnvironment $MajorVersion
    $targetDirectory = if ((Normalize-Architecture $Architecture) -eq "ARM64") { "arm64" } else { "x64" }
    $runtimePattern = if ($targetDirectory -eq "arm64") { "clang_rt.asan_dynamic-aarch64.dll" } else { "clang_rt.asan_dynamic-x86_64.dll" }
    $toolsets = Get-ChildItem (Join-Path $environment.InstallationPath "VC\Tools\MSVC") -Directory |
        Sort-Object { [version]$_.Name } -Descending

    foreach ($toolset in $toolsets) {
        foreach ($hostArchitecture in @("Hostx64", "Hostx86")) {
            $runtimeDirectory = Join-Path $toolset.FullName "bin\$hostArchitecture\$targetDirectory"
            if (Test-Path (Join-Path $runtimeDirectory $runtimePattern)) {
                return $runtimeDirectory
            }
        }
    }
    throw "The MSVC AddressSanitizer runtime was not found for $Architecture."
}

function Assert-SupportedBuildCombination {
    param(
        [string]$Generator,
        [string]$Configuration,
        [string]$Architecture,
        [string]$Toolset
    )

    $Architecture = Normalize-Architecture $Architecture
    if ($Generator -like "vs*" -and $Toolset -eq "gcc") {
        throw "Visual Studio generators do not support the GCC toolset."
    }
    if ($Generator -eq "gmake" -and $Toolset -notin @("default", "gcc")) {
        throw "Windows GNU Make supports only the default or GCC toolset."
    }
    if ($Generator -eq "gmake" -and $Architecture -eq "ARM64") {
        throw "Windows GNU Make ARM64 is not supported by this template."
    }
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -in @("default", "msc"))
    if ($usesMSVC -and $Configuration -in @("DebugUBSan", "DebugTSan")) {
        throw "$Configuration is not supported by MSVC. Use Linux or macOS with GCC/Clang."
    }
}

function Get-NinjaExecutable {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) { return $link }
    throw "Ninja was not found. Run bootstrap for the Ninja generator."
}
