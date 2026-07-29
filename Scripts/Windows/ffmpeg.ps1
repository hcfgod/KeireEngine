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
$Source = Join-Path $Root "Vendor\ffmpeg"
$Output = Join-Path $Root "Build\Dependencies\ffmpeg\$Configuration"
$Install = Join-Path $Output "install"
$Stamp = Join-Path $Output "keire-ffmpeg.stamp"
$Expected = "$($Lock.FFMPEG_COMMIT)|$Configuration|shared-lgpl-avformat-avcodec-swresample-avutil-v1"

if (-not (Test-Path -LiteralPath (Join-Path $Source "configure"))) {
    throw "Vendor/ffmpeg is unavailable. Initialize the locked FFmpeg submodule first."
}
$Actual = ([string](& git -C $Source rev-parse HEAD)).Trim()
if ($LASTEXITCODE -ne 0 -or $Actual -ne $Lock.FFMPEG_COMMIT) {
    throw "Vendor/ffmpeg is not at the locked commit $($Lock.FFMPEG_COMMIT)."
}
if (-not $Force -and (Test-Path -LiteralPath (Join-Path $Install "include\libavformat\avformat.h")) -and
    (Test-Path -LiteralPath (Join-Path $Install "bin\avformat.lib")) -and
    (Test-Path -LiteralPath $Stamp) -and ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq $Expected)) {
    Write-Host "==> Private FFmpeg $Configuration build is current"
    return
}
if ($Configuration -eq "Release" -and -not $Force) {
    $DebugInstall = Join-Path $Root "Build\Dependencies\ffmpeg\Debug\install"
    $DebugStamp = Join-Path $Root "Build\Dependencies\ffmpeg\Debug\keire-ffmpeg.stamp"
    $DebugExpected = "$($Lock.FFMPEG_COMMIT)|Debug|shared-lgpl-avformat-avcodec-swresample-avutil-v1"
    if ((Test-Path -LiteralPath (Join-Path $DebugInstall "bin\avformat.lib")) -and
        (Test-Path -LiteralPath $DebugStamp) -and
        ((Get-Content -LiteralPath $DebugStamp -Raw).Trim() -eq $DebugExpected)) {
        New-Item -ItemType Directory -Force -Path $Output | Out-Null
        Copy-Item -LiteralPath $DebugInstall -Destination $Output -Recurse -Force
        [IO.File]::WriteAllText($Stamp, "$Expected`n", [Text.UTF8Encoding]::new($false))
        Write-Host "==> Reused the optimized private FFmpeg source build for Release"
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

function Convert-ToBashPath([string]$Path) {
    $Escaped = $Path.Replace("'", "'\''")
    $Result = [string](& $Bash -c "export PATH=/usr/bin:`$PATH; cygpath -u '$Escaped'")
    if ($LASTEXITCODE -ne 0) { throw "Could not convert a path for the FFmpeg build." }
    return $Result.Trim()
}

$SourceBash = Convert-ToBashPath $Source
$OutputBash = Convert-ToBashPath $Output
$InstallBash = Convert-ToBashPath $Install
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
    "--enable-encoder=flac",
    "--enable-muxer=flac"
) -join " "

Write-Host "==> Building private LGPL FFmpeg $Configuration libraries"
$Command = "export PATH=/usr/bin:`$PATH; set -euo pipefail; cd '$OutputBash'; " +
    "'$SourceBash/configure' $Configure $DebugOptions; " +
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
