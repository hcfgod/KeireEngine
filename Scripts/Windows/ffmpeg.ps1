[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "ffmpeg-runtime-contract.ps1")

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset "vs2022" $Toolset
if ($Toolset -eq "gcc") {
    throw "Private FFmpeg builds on Windows do not support the gcc toolset because GNU import libraries are not published."
}
$OutputArchitecture = Get-ArchitectureOutputName $Architecture
$FfmpegArchitecture = if ($Architecture -eq "ARM64") { "aarch64" } else { "x86_64" }
Enter-WindowsToolEnvironment "vs2022" "msc" $Architecture | Out-Null
$VendorSource = Join-Path $Root "Vendor\ffmpeg"
$Output = Join-Path $Root "Build\Dependencies\ffmpeg\$Configuration"
$CacheBase = Join-Path $Root "Build\Dependencies\ffmpeg-cache\windows-$OutputArchitecture-msc-producer-$Toolset"
$CacheOutput = Join-Path $CacheBase $Configuration
$Install = Join-Path $CacheOutput "install"
$Stamp = Join-Path $CacheOutput "keire-ffmpeg.stamp"
$ZlibBuild = Join-Path $Root "Build\Dependencies\windows-$OutputArchitecture-$Toolset\Release"
$ZlibSourceInclude = Join-Path $Root "Vendor\assimp\contrib\zlib"
$ZlibGeneratedInclude = Join-Path $ZlibBuild "Assimp\contrib\zlib"
$ZlibLibrary = Join-Path $ZlibBuild "install\lib\zlibstatic.lib"
$ZlibStamp = Join-Path $ZlibBuild "keire-dependency.stamp"
if (-not (Test-Path -LiteralPath (Join-Path $ZlibSourceInclude "zlib.h")) -or
    -not (Test-Path -LiteralPath (Join-Path $ZlibGeneratedInclude "zconf.h")) -or
    -not (Test-Path -LiteralPath $ZlibLibrary) -or -not (Test-Path -LiteralPath $ZlibStamp)) {
    throw "The Release native dependency build is missing the zlib files required by private FFmpeg."
}
$ZlibKey = (Get-Content -LiteralPath $ZlibStamp -Raw).Trim()
$RuntimeContract = Get-KeireFfmpegRuntimeContract
$Expected = "$($Lock.FFMPEG_COMMIT)|$Configuration|$ZlibKey|$($RuntimeContract.StampFlavor)"

function Test-FfmpegOutput([string]$Path, [string]$ExpectedStamp) {
    $candidateInstall = Join-Path $Path "install"
    $candidateStamp = Join-Path $Path "keire-ffmpeg.stamp"
    $candidateComponents = Join-Path $Path "config_components.h"
    if (-not (Test-Path -LiteralPath (Join-Path $candidateInstall "include\libavformat\avformat.h")) -or
        -not (Test-Path -LiteralPath $candidateComponents) -or
        -not (Select-String -LiteralPath $candidateComponents -SimpleMatch "#define CONFIG_EXR_DECODER 1" -Quiet) -or
        -not (Test-Path -LiteralPath $candidateStamp) -or
        (Get-Content -LiteralPath $candidateStamp -Raw).Trim() -ne $ExpectedStamp) {
        return $false
    }

    foreach ($component in $RuntimeContract.Files) {
        $importLibrary = Join-Path $candidateInstall "bin\$($component.Component).lib"
        $runtimeLibrary = Join-Path $candidateInstall "bin\$($component.FileName)"
        if (-not (Test-Path -LiteralPath $importLibrary -PathType Leaf) -or
            -not (Test-Path -LiteralPath $runtimeLibrary -PathType Leaf)) {
            return $false
        }
    }

    $expectedRuntimeNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($component in $RuntimeContract.Files) {
        [void]$expectedRuntimeNames.Add($component.FileName)
    }
    $seenRuntimePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($pattern in $RuntimeContract.NamespacePatterns) {
        foreach ($candidate in @(
                Get-ChildItem -LiteralPath (Join-Path $candidateInstall "bin") -Filter $pattern -Force `
                    -ErrorAction SilentlyContinue)) {
            if (-not $seenRuntimePaths.Add($candidate.FullName)) {
                continue
            }
            if ($candidate.PSIsContainer -or -not $expectedRuntimeNames.Contains($candidate.Name)) {
                return $false
            }
        }
    }
    return $true
}

function Remove-FfmpegOutput([string]$Path, [string]$AllowedBase) {
    Assert-FfmpegOutputPath -Path $Path -AllowedBase $AllowedBase
    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

function Assert-FfmpegOutputPath([string]$Path, [string]$AllowedBase) {
    $pathSeparators = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd($pathSeparators)
    $resolvedBase = [IO.Path]::GetFullPath($AllowedBase).TrimEnd($pathSeparators)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd($pathSeparators)
    $separator = [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedBase.StartsWith("$resolvedRoot$separator", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use an FFmpeg output base outside the repository: $AllowedBase."
    }
    if (-not $resolvedPath.StartsWith("$resolvedBase$separator", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an FFmpeg build outside $AllowedBase."
    }

    $relativePath = $resolvedPath.Substring($resolvedRoot.Length).TrimStart($pathSeparators)
    $currentPath = $resolvedRoot
    foreach ($component in @($relativePath -split '[\\/]' | Where-Object { $_ })) {
        $currentPath = Join-Path $currentPath $component
        if (Test-Path -LiteralPath $currentPath) {
            $item = Get-Item -LiteralPath $currentPath -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing to use reparse-point FFmpeg output path '$currentPath'."
            }
        }
    }
}

function Set-RelocatableFfmpegManifests([string]$InstallRoot) {
    $manifestRoot = Join-Path $InstallRoot "lib\pkgconfig"
    foreach ($manifest in @(Get-ChildItem -LiteralPath $manifestRoot -Filter "*.pc" -File -ErrorAction SilentlyContinue)) {
        $text = [IO.File]::ReadAllText($manifest.FullName)
        $text = [Text.RegularExpressions.Regex]::Replace($text, '(?m)^prefix=.*$', 'prefix=${pcfiledir}/../..')
        $text = [Text.RegularExpressions.Regex]::Replace($text, '(?m)^exec_prefix=.*$', 'exec_prefix=${prefix}')
        $text = [Text.RegularExpressions.Regex]::Replace($text, '(?m)^libdir=.*$', 'libdir=${prefix}/lib')
        $text = [Text.RegularExpressions.Regex]::Replace($text, '(?m)^includedir=.*$', 'includedir=${prefix}/include')
        [IO.File]::WriteAllText($manifest.FullName, $text, [Text.UTF8Encoding]::new($false))
    }
}

function Publish-FfmpegOutput([string]$Source, [string]$Destination) {
    Assert-FfmpegOutputPath -Path $Source -AllowedBase $CacheBase
    Assert-FfmpegOutputPath -Path $Destination -AllowedBase (Join-Path $Root "Build\Dependencies\ffmpeg")
    Set-RelocatableFfmpegManifests (Join-Path $Source "install")
    Remove-FfmpegOutput $Destination (Join-Path $Root "Build\Dependencies\ffmpeg")
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $Source "install") -Destination $Destination -Recurse
    Copy-Item -LiteralPath (Join-Path $Source "config_components.h"),
        (Join-Path $Source "keire-ffmpeg.stamp") -Destination $Destination
}

Assert-FfmpegOutputPath -Path $CacheOutput -AllowedBase $CacheBase
Assert-FfmpegOutputPath -Path $Output -AllowedBase (Join-Path $Root "Build\Dependencies\ffmpeg")

if (-not (Test-Path -LiteralPath (Join-Path $VendorSource "configure"))) {
    throw "Vendor/ffmpeg is unavailable. Initialize the locked FFmpeg submodule first."
}
$Actual = ([string](& git -C $VendorSource rev-parse HEAD)).Trim()
if ($LASTEXITCODE -ne 0 -or $Actual -ne $Lock.FFMPEG_COMMIT) {
    throw "Vendor/ffmpeg is not at the locked commit $($Lock.FFMPEG_COMMIT)."
}
if (-not $Force -and (Test-FfmpegOutput $CacheOutput $Expected)) {
    if (-not (Test-FfmpegOutput $Output $Expected)) {
        Publish-FfmpegOutput $CacheOutput $Output
        Write-Host "==> Restored private FFmpeg $Configuration from the windows-$OutputArchitecture-msc-producer-$Toolset cache"
    }
    else {
        Write-Host "==> Private FFmpeg $Configuration build is current"
    }
    return
}
if (-not $Force -and (Test-FfmpegOutput $Output $Expected)) {
    Remove-FfmpegOutput $CacheOutput $CacheBase
    New-Item -ItemType Directory -Force -Path $CacheOutput | Out-Null
    Copy-Item -LiteralPath (Join-Path $Output "install") -Destination $CacheOutput -Recurse
    Copy-Item -LiteralPath (Join-Path $Output "config_components.h"),
        (Join-Path $Output "keire-ffmpeg.stamp") -Destination $CacheOutput
    Write-Host "==> Adopted private FFmpeg $Configuration into the windows-$OutputArchitecture-msc-producer-$Toolset cache"
    return
}
if (-not $Force) {
    # Both configurations deliberately use the same optimized /MD FFmpeg build. Keep separate install roots for the
    # generated project contract, but compile the large codec dependency only once on a fresh workstation.
    $AlternateConfiguration = if ($Configuration -eq "Debug") { "Release" } else { "Debug" }
    $AlternateOutput = Join-Path $CacheBase $AlternateConfiguration
    $AlternateComponents = Join-Path $AlternateOutput "config_components.h"
    $AlternateExpected =
        "$($Lock.FFMPEG_COMMIT)|$AlternateConfiguration|$ZlibKey|$($RuntimeContract.StampFlavor)"
    Assert-FfmpegOutputPath -Path $AlternateOutput -AllowedBase $CacheBase
    if (Test-FfmpegOutput $AlternateOutput $AlternateExpected) {
        Remove-FfmpegOutput $CacheOutput $CacheBase
        New-Item -ItemType Directory -Force -Path $CacheOutput | Out-Null
        Copy-Item -LiteralPath (Join-Path $AlternateOutput "install") -Destination $CacheOutput -Recurse
        Copy-Item -LiteralPath $AlternateComponents -Destination (Join-Path $CacheOutput "config_components.h")
        [IO.File]::WriteAllText($Stamp, "$Expected`n", [Text.UTF8Encoding]::new($false))
        Publish-FfmpegOutput $CacheOutput $Output
        Write-Host "==> Reused the identical private FFmpeg $AlternateConfiguration build for $Configuration"
        return
    }
}

$Bash = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path -LiteralPath $Bash)) {
    throw "MSYS2 Bash is required to configure the private FFmpeg source build. Run bootstrap prerequisites."
}
if (-not (Test-Path -LiteralPath "C:\msys64\usr\bin\make.exe")) {
    throw "GNU Make is required to source-build private FFmpeg. Run bootstrap prerequisites."
}

Remove-FfmpegOutput $CacheOutput $CacheBase
New-Item -ItemType Directory -Force -Path $CacheOutput | Out-Null
$Source = Join-Path $CacheOutput "source"
$SourceArchive = Join-Path $CacheOutput "ffmpeg-source.tar"
New-Item -ItemType Directory -Force -Path $Source | Out-Null
# Git's locked object bytes are authoritative. A Windows checkout may translate FFmpeg's Makefiles and shell files to
# CRLF, which changes continuation semantics under MSYS Make even though the C/C++ sources remain valid.
# `git archive` applies the caller's core.autocrlf policy on Windows. Disable it for this transaction so the extracted
# shell and Make sources retain the canonical LF bytes regardless of the user's global Git configuration.
& git -c core.autocrlf=false -C $VendorSource archive --format=tar --output=$SourceArchive $Lock.FFMPEG_COMMIT
if ($LASTEXITCODE -ne 0) { throw "Could not materialize the locked FFmpeg source archive." }
& tar -xf $SourceArchive -C $Source
if ($LASTEXITCODE -ne 0) { throw "Could not extract the locked FFmpeg source archive." }
Remove-Item -LiteralPath $SourceArchive -Force
if (-not (Test-Path -LiteralPath (Join-Path $Source "configure"))) {
    throw "The canonical FFmpeg archive is missing its configure script."
}

# The locked FFmpeg revision predates upstream f101fce22d64db10f500242e23e43a251fe14414, which removed an orphaned
# MSVC preprocessor here-document body. Apply that exact upstream correction only to the disposable build tree rather
# than advancing or modifying the pinned vendor submodule.
$ConfigurePath = Join-Path $Source "configure"
$BrokenMsvcProbe = [string]::Join("`n", @(
    "#ifdef WINAPI_FAMILY",
    "#include <winapifamily.h>",
    "#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)",
    "#error not desktop",
    "#endif",
    "#endif",
    "EOF",
    ""
))
$ConfigureText = [IO.File]::ReadAllText($ConfigurePath)
$ProbeIndex = $ConfigureText.IndexOf($BrokenMsvcProbe, [StringComparison]::Ordinal)
if ($ProbeIndex -lt 0 -or
    $ConfigureText.LastIndexOf($BrokenMsvcProbe, [StringComparison]::Ordinal) -ne $ProbeIndex) {
    throw "The locked FFmpeg MSVC configure defect no longer matches its validated upstream correction."
}
[IO.File]::WriteAllText($ConfigurePath, $ConfigureText.Replace($BrokenMsvcProbe, ""),
    [Text.UTF8Encoding]::new($false))

function Convert-ToBashPath([string]$Path) {
    $Escaped = $Path.Replace("'", "'\''")
    $Result = [string](& $Bash -c "export PATH=/usr/bin:`$PATH; cygpath -u '$Escaped'")
    if ($LASTEXITCODE -ne 0) { throw "Could not convert a path for the FFmpeg build." }
    return $Result.Trim()
}

$SourceBash = Convert-ToBashPath $Source
$OutputBash = Convert-ToBashPath $CacheOutput
$InstallBash = Convert-ToBashPath $Install
$FfbuildDirectory = Join-Path $CacheOutput "ffbuild"
$ZlibIncludeDirectory = Join-Path $CacheOutput "zlib-include"
$ZlibLinkDirectory = Join-Path $CacheOutput "zlib-lib"
# FFmpeg opens ffbuild/config.log before its configure script materializes the rest of the out-of-tree directory
# layout. Create that log parent explicitly so a clean cache cannot fail before configuration begins.
New-Item -ItemType Directory -Force -Path $FfbuildDirectory, $ZlibIncludeDirectory, $ZlibLinkDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $ZlibSourceInclude "zlib.h"), (Join-Path $ZlibGeneratedInclude "zconf.h") `
    -Destination $ZlibIncludeDirectory
# FFmpeg's private config.h defines the generic HAVE_UNISTD_H macro because its compatibility headers provide one.
# The native MSVC zlib build does not use that compatibility layer, so keep zconf.h from inferring a Unix API here.
$ZconfPath = Join-Path $ZlibIncludeDirectory "zconf.h"
$ZconfText = [IO.File]::ReadAllText($ZconfPath)
$ZconfUnistdProbe = "#ifdef HAVE_UNISTD_H    /* may be set to #if 1 by ./configure */"
if ($ZconfText.IndexOf($ZconfUnistdProbe, [StringComparison]::Ordinal) -lt 0) {
    throw "The pinned zlib configuration no longer contains the expected unistd compatibility probe."
}
[IO.File]::WriteAllText($ZconfPath,
    $ZconfText.Replace($ZconfUnistdProbe, "#if defined(HAVE_UNISTD_H) && !defined(_WIN32)"),
    [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $ZlibLibrary -Destination (Join-Path $ZlibLinkDirectory "zlib.lib")
$ZlibIncludeDirectoryBash = Convert-ToBashPath $ZlibIncludeDirectory
$ZlibLinkDirectoryBash = Convert-ToBashPath $ZlibLinkDirectory
$DebugOptions = "--disable-debug"
$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)
$Configure = @(
    "--prefix='$InstallBash'",
    "--toolchain=msvc",
    "--arch=$FfmpegArchitecture",
    "--target-os=win64",
    "--enable-shared",
    "--disable-static",
    "--disable-programs",
    "--disable-doc",
    "--disable-network",
    "--disable-avdevice",
    "--disable-avfilter",
    "--disable-swscale",
    "--disable-autodetect",
    "--disable-x86asm",
    "--disable-gpl",
    "--disable-nonfree",
    "--disable-version3",
    "--enable-avformat",
    "--enable-avcodec",
    "--enable-swresample",
    "--enable-avutil",
    "--enable-zlib",
    "--enable-decoder=exr",
    "--enable-encoder=flac",
    "--enable-muxer=flac",
    "--extra-cflags=`"-MD -I$ZlibIncludeDirectoryBash`"",
    "--extra-ldflags=`"-libpath:$ZlibLinkDirectoryBash`""
) -join " "

Write-Host "==> Building private LGPL FFmpeg $Configuration libraries"
$Command = "export PATH=/usr/bin:`$PATH; set -euo pipefail; cd '$OutputBash'; " +
    "'$SourceBash/configure' $Configure $DebugOptions; " +
    "cd '$SourceBash'; find . -type d -exec mkdir -p '$OutputBash'/{} \;; cd '$OutputBash'; " +
    "make -j$Jobs; make install"
& $Bash -c $Command
if ($LASTEXITCODE -ne 0) { throw "Private FFmpeg $Configuration source build failed." }

$LicenseRoot = Join-Path $Install "share\licenses\ffmpeg"
New-Item -ItemType Directory -Force -Path $LicenseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $Source "COPYING.LGPLv2.1") -Destination $LicenseRoot
Copy-Item -LiteralPath (Join-Path $Source "COPYING.LGPLv3") -Destination $LicenseRoot
[IO.File]::WriteAllText((Join-Path $LicenseRoot "SOURCE.txt"),
    "Source: Vendor/ffmpeg`nCommit: $($Lock.FFMPEG_COMMIT)`nConfiguration: $Configuration`n",
    [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($Stamp, "$Expected`n", [Text.UTF8Encoding]::new($false))
Publish-FfmpegOutput $CacheOutput $Output
