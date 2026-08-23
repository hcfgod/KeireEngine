[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
Enter-WindowsToolEnvironment "vs2022" "msc" "x86_64" | Out-Null
$VendorSource = Join-Path $Root "Vendor\ffmpeg"
$Output = Join-Path $Root "Build\Dependencies\ffmpeg\$Configuration"
$Install = Join-Path $Output "install"
$Stamp = Join-Path $Output "keire-ffmpeg.stamp"
$ZlibBuild = Join-Path $Root "Build\Dependencies\windows-x86_64-msc\Release"
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
$Expected = "$($Lock.FFMPEG_COMMIT)|$Configuration|$ZlibKey|shared-lgpl-avformat-avcodec-swresample-avutil-zlib-exr-v6"

if (-not (Test-Path -LiteralPath (Join-Path $VendorSource "configure"))) {
    throw "Vendor/ffmpeg is unavailable. Initialize the locked FFmpeg submodule first."
}
$Actual = ([string](& git -C $VendorSource rev-parse HEAD)).Trim()
if ($LASTEXITCODE -ne 0 -or $Actual -ne $Lock.FFMPEG_COMMIT) {
    throw "Vendor/ffmpeg is not at the locked commit $($Lock.FFMPEG_COMMIT)."
}
if (-not $Force -and (Test-Path -LiteralPath (Join-Path $Install "include\libavformat\avformat.h")) -and
    (Test-Path -LiteralPath (Join-Path $Install "bin\avformat-63.dll")) -and
    (Test-Path -LiteralPath (Join-Path $Install "bin\avformat.lib")) -and
    (Test-Path -LiteralPath (Join-Path $Output "config_components.h")) -and
    (Select-String -LiteralPath (Join-Path $Output "config_components.h") -SimpleMatch "#define CONFIG_EXR_DECODER 1" -Quiet) -and
    (Test-Path -LiteralPath $Stamp) -and ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq $Expected)) {
    Write-Host "==> Private FFmpeg $Configuration build is current"
    return
}
if (-not $Force) {
    # Both configurations deliberately use the same optimized /MD FFmpeg build. Keep separate install roots for the
    # generated project contract, but compile the large codec dependency only once on a fresh workstation.
    $AlternateConfiguration = if ($Configuration -eq "Debug") { "Release" } else { "Debug" }
    $AlternateOutput = Join-Path $Root "Build\Dependencies\ffmpeg\$AlternateConfiguration"
    $AlternateInstall = Join-Path $AlternateOutput "install"
    $AlternateStamp = Join-Path $AlternateOutput "keire-ffmpeg.stamp"
    $AlternateComponents = Join-Path $AlternateOutput "config_components.h"
    $AlternateExpected = "$($Lock.FFMPEG_COMMIT)|$AlternateConfiguration|$ZlibKey|shared-lgpl-avformat-avcodec-swresample-avutil-zlib-exr-v6"
    if ((Test-Path -LiteralPath (Join-Path $AlternateInstall "include\libavformat\avformat.h")) -and
        (Test-Path -LiteralPath (Join-Path $AlternateInstall "bin\avformat-63.dll")) -and
        (Test-Path -LiteralPath (Join-Path $AlternateInstall "bin\avformat.lib")) -and
        (Test-Path -LiteralPath $AlternateComponents) -and
        (Select-String -LiteralPath $AlternateComponents -SimpleMatch "#define CONFIG_EXR_DECODER 1" -Quiet) -and
        (Test-Path -LiteralPath $AlternateStamp) -and
        ((Get-Content -LiteralPath $AlternateStamp -Raw).Trim() -eq $AlternateExpected)) {
        New-Item -ItemType Directory -Force -Path $Output | Out-Null
        if (Test-Path -LiteralPath $Install) {
            Remove-Item -LiteralPath $Install -Recurse -Force
        }
        Copy-Item -LiteralPath $AlternateInstall -Destination $Output -Recurse -Force
        Copy-Item -LiteralPath $AlternateComponents -Destination (Join-Path $Output "config_components.h") -Force
        [IO.File]::WriteAllText($Stamp, "$Expected`n", [Text.UTF8Encoding]::new($false))
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

if (Test-Path -LiteralPath $Output) {
    $ResolvedOutput = [IO.Path]::GetFullPath($Output)
    $AllowedRoot = [IO.Path]::GetFullPath((Join-Path $Root "Build\Dependencies\ffmpeg")) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $ResolvedOutput.StartsWith($AllowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an FFmpeg build outside Build/Dependencies/ffmpeg."
    }
    Remove-Item -LiteralPath $Output -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Source = Join-Path $Output "source"
$SourceArchive = Join-Path $Output "ffmpeg-source.tar"
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
$OutputBash = Convert-ToBashPath $Output
$InstallBash = Convert-ToBashPath $Install
$FfbuildDirectory = Join-Path $Output "ffbuild"
$ZlibIncludeDirectory = Join-Path $Output "zlib-include"
$ZlibLinkDirectory = Join-Path $Output "zlib-lib"
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
    "--arch=x86_64",
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
