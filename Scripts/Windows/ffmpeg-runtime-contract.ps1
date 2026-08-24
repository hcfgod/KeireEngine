$ErrorActionPreference = "Stop"

function Get-KeireFfmpegRuntimeContract {
    return [pscustomobject]@{
        StampFlavor = "shared-lgpl-avformat-avcodec-swresample-avutil-zlib-exr-v6"
        NamespacePatterns = @(
            "avcodec-*.dll",
            "avdevice-*.dll",
            "avfilter-*.dll",
            "avformat-*.dll",
            "avresample-*.dll",
            "avutil-*.dll",
            "postproc-*.dll",
            "swresample-*.dll",
            "swscale-*.dll"
        )
        Files = @(
            [pscustomobject]@{ Component = "avformat"; FileName = "avformat-63.dll" },
            [pscustomobject]@{ Component = "avcodec"; FileName = "avcodec-63.dll" },
            [pscustomobject]@{ Component = "swresample"; FileName = "swresample-7.dll" },
            [pscustomobject]@{ Component = "avutil"; FileName = "avutil-61.dll" }
        )
    }
}

function Assert-KeireContainedWindowsPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $separators = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd($separators)
    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd($separators)
    $separator = [IO.Path]::DirectorySeparatorChar
    if ($resolvedPath -ne $resolvedRoot -and
        -not $resolvedPath.StartsWith("$resolvedRoot$separator", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a path outside the repository root: $Path"
    }

    $relativePath = $resolvedPath.Substring($resolvedRoot.Length).TrimStart($separators)
    $currentPath = $resolvedRoot
    foreach ($component in @($relativePath -split '[\\/]' | Where-Object { $_ })) {
        $currentPath = Join-Path $currentPath $component
        $item = Get-Item -LiteralPath $currentPath -Force -ErrorAction SilentlyContinue
        if ($item -and (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw "Refusing to use reparse-point runtime path '$currentPath'."
        }
    }
}

function Get-KeireWindowsPeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw "Runtime file is not a valid PE image: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt $stream.Length - 6) {
            throw "Runtime file has an invalid PE header offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Runtime file is missing its PE signature: $Path"
        }
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Assert-KeireWindowsPeArchitecture {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateSet("x86_64", "ARM64")][string]$Architecture
    )

    $expectedMachine = if ($Architecture -eq "ARM64") { 0xAA64 } else { 0x8664 }
    $actualMachine = Get-KeireWindowsPeMachine -Path $Path
    if ($actualMachine -ne $expectedMachine) {
        throw ("Runtime PE architecture mismatch for {0}: expected 0x{1:X4}, found 0x{2:X4}." -f
            $Path, $expectedMachine, $actualMachine)
    }
}
