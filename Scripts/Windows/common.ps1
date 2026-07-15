$ErrorActionPreference = "Stop"

function Read-KeyValueFile {
    param([string]$Path)
    $values = @{}
    # Project files are UTF-8 without a BOM. Windows PowerShell 5 otherwise
    # decodes them using the legacy system code page.
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -match '^([A-Z0-9_]+)=(.*)$') {
            if ($values.ContainsKey($Matches[1])) { throw "Duplicate key '$($Matches[1])' in $Path." }
            $values[$Matches[1]] = $Matches[2]
        }
        elseif ($line.Length -ne 0) { throw "Malformed configuration line in $Path`: $line" }
    }
    return $values
}

function Test-SemanticVersion {
    param([string]$Version)
    return $Version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$'
}

function Get-RepositoryRoot { return (Resolve-Path (Join-Path $PSScriptRoot "..\..")) }
function Get-ProjectConfig { return Read-KeyValueFile (Join-Path (Get-RepositoryRoot) "Config\Project.conf") }
function Get-DependencyLock { return Read-KeyValueFile (Join-Path (Get-RepositoryRoot) "Config\Dependencies.lock") }

function Get-CMakeExecutable {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $installed = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $installed) { return $installed }
    return $null
}

function Get-GitWorktreeRoot {
    param([string]$Path)
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return $null }
    try {
        $topLevel = (& git -C $Path rev-parse --show-toplevel 2>$null) -join ""
    }
    catch {
        return $null
    }
    if ($LASTEXITCODE -ne 0 -or -not $topLevel) { return $null }
    return (Resolve-Path $topLevel)
}

function Get-GitHeadCommit {
    param([string]$Path, [string]$Fallback = "uncommitted")
    try {
        $commit = (& git -C $Path rev-parse --verify HEAD 2>$null) -join ""
    }
    catch {
        return $Fallback
    }
    if ($LASTEXITCODE -ne 0 -or -not $commit) { return $Fallback }
    return $commit.Trim()
}

function Test-GitRepository {
    param([string]$Path)
    try {
        $inside = (& git -C $Path rev-parse --is-inside-work-tree 2>$null) -join ""
    }
    catch {
        return $false
    }
    return $LASTEXITCODE -eq 0 -and $inside.Trim() -eq "true"
}

function ConvertTo-MacroPrefix {
    param([string]$Identifier)
    $value = [regex]::Replace($Identifier, '([A-Z]+)([A-Z][a-z])', '$1_$2')
    $value = [regex]::Replace($value, '([a-z0-9])([A-Z])', '$1_$2')
    return $value.ToUpperInvariant()
}

function Assert-WindowsPackageStage {
    param([string]$Stage, [string]$ClientTarget, [string]$CoreTarget, [string]$Namespace)
    $required = @(
        "bin\$ClientTarget.exe", "lib\$CoreTarget.lib", "Config\Client.json", "include\$Namespace\Core.h", "include\$Namespace\Log.h",
        "include\$Namespace\Api.h", "include\$Namespace\Application.h", "include\$Namespace\Assert.h", "include\$Namespace\BuildInfo.h",
        "include\$Namespace\EntryPoint.h", "include\$Namespace\Event.h", "include\$Namespace\Layer.h", "include\$Namespace\Ref.h",
        "include\$Namespace\Time.h", "include\$Namespace\Window.h", "include\$Namespace\WindowConfig.h",
        "third-party\spdlog\spdlog.h", "third-party\licenses\spdlog-LICENSE.txt",
        "third-party\licenses\fmt-LICENSE.rst", "third-party\licenses\doctest-LICENSE.txt",
        "third-party\licenses\nlohmann-json-LICENSE.MIT.txt", "third-party\SDL3\include\SDL3\SDL.h",
        "third-party\SDL3\lib\SDL3-static.lib", "third-party\SDL3\cmake\SDL3Config.cmake",
        "third-party\SDL3\licenses\SDL3\LICENSE.txt",
        "examples\consumer\Main.cpp", "examples\consumer\Client.json", "examples\consumer\CMakeLists.txt", "examples\consumer\README.md",
        "examples\managed-consumer\ClientApplication.cpp", "examples\managed-consumer\CMakeLists.txt", "examples\managed-consumer\README.md",
        "README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json"
    )
    foreach ($path in $required) {
        if (-not (Test-Path (Join-Path $Stage $path) -PathType Leaf)) { throw "Package is missing required content: $path" }
    }
    if (-not (Get-ChildItem (Join-Path $Stage "lib\cmake") -Filter "*Config.cmake" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        throw "Package is missing its CMake package configuration."
    }
}

function Resolve-WindowsToolset {
    param([string]$Generator, [string]$Toolset)
    if ($Toolset -ne "default") { return $Toolset }
    if ($Generator -eq "gmake") { return "gcc" }
    return "msc"
}

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
    if ($Configuration -eq "Coverage" -and ($Generator -ne "ninja" -or $Toolset -ne "clang")) {
        throw "Coverage requires the Ninja generator and Clang toolset."
    }
}

function Get-NinjaExecutable {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) { return $link }
    throw "Ninja was not found. Run bootstrap for the Ninja generator."
}

function Add-LLVMToPath {
    $bin = "C:\Program Files\LLVM\bin"
    if (-not (Test-Path (Join-Path $bin "clang.exe"))) { throw "LLVM was not found under $bin." }
    $shimDirectory = Join-Path (Get-RepositoryRoot) "Tools\Windows\llvm-bin"
    New-Item -ItemType Directory -Force $shimDirectory | Out-Null
    $llvmAr = Join-Path $bin "llvm-ar.exe"
    if (Test-Path $llvmAr) { Copy-Item $llvmAr (Join-Path $shimDirectory "ar.exe") -Force }
    $env:PATH = "$shimDirectory;$bin;$env:PATH"
    return $bin
}

function Add-MSYS2ToPath {
    $bin = @("C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin") |
        Where-Object { Test-Path (Join-Path $_ "g++.exe") } | Select-Object -First 1
    if (-not $bin) { throw "An MSYS2 GCC environment was not found under C:\msys64." }
    $env:PATH = "$bin;$env:PATH"
    return $bin
}

function Enter-WindowsToolEnvironment {
    param([string]$Generator, [string]$Toolset, [string]$Architecture)
    $resolved = Resolve-WindowsToolset $Generator $Toolset
    switch ($resolved) {
        "msc" {
            $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
            Enter-VSDeveloperEnvironment $majorVersion $Architecture | Out-Null
        }
        "clang" { Add-LLVMToPath | Out-Null }
        "gcc" { Add-MSYS2ToPath | Out-Null }
    }
    return $resolved
}
