[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration,
    [Parameter(Mandatory = $true)][string]$Architecture,
    [Parameter(Mandatory = $true)][ValidateSet("msc", "clang", "gcc")][string]$Toolset,
    [Parameter(Mandatory = $true)][string]$ProjectNamespace,
    [Parameter(Mandatory = $true)][string]$FfmpegCommit
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "ffmpeg-runtime-contract.ps1")

$resolvedRoot = [IO.Path]::GetFullPath($Root)
$normalizedArchitecture = Normalize-Architecture $Architecture
$outputArchitecture = Get-ArchitectureOutputName $normalizedArchitecture
$dependencyConfiguration = if ($Configuration -in @("Release", "Dist")) { "Release" } else { "Debug" }
if ($ProjectNamespace -notmatch '^[A-Za-z][A-Za-z0-9_]*$') {
    throw "The project namespace is invalid for FFmpeg runtime staging: $ProjectNamespace"
}
if ($FfmpegCommit -notmatch '^[0-9a-f]{40}$') {
    throw "The FFmpeg commit is invalid for runtime staging: $FfmpegCommit"
}

$contract = Get-KeireFfmpegRuntimeContract
$ffmpegRoot = Join-Path $resolvedRoot "Build\Dependencies\ffmpeg\$dependencyConfiguration"
$sourceDirectory = Join-Path $ffmpegRoot "install\bin"
$ffmpegStamp = Join-Path $ffmpegRoot "keire-ffmpeg.stamp"
$zlibStamp = Join-Path $resolvedRoot `
    "Build\Dependencies\windows-$outputArchitecture-$Toolset\Release\keire-dependency.stamp"
$destinationDirectory = Join-Path $resolvedRoot `
    "Build\Bin\$Configuration-windows-$outputArchitecture\${ProjectNamespace}AssetWorker"
$worker = Join-Path $destinationDirectory "${ProjectNamespace}AssetWorker.exe"

foreach ($path in @($sourceDirectory, $ffmpegStamp, $zlibStamp, $destinationDirectory, $worker)) {
    Assert-KeireContainedWindowsPath -Root $resolvedRoot -Path $path
}
if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
    throw "The pinned FFmpeg runtime directory is missing: $sourceDirectory"
}
if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) {
    throw "The asset worker executable is missing after a successful build: $worker"
}
Assert-KeireWindowsPeArchitecture -Path $worker -Architecture $normalizedArchitecture
if (-not (Test-Path -LiteralPath $zlibStamp -PathType Leaf)) {
    throw "The architecture-specific dependency provenance stamp is missing: $zlibStamp"
}
if (-not (Test-Path -LiteralPath $ffmpegStamp -PathType Leaf)) {
    throw "The FFmpeg provenance stamp is missing: $ffmpegStamp"
}
$zlibKey = (Get-Content -LiteralPath $zlibStamp -Raw).Trim()
$expectedStamp = "$FfmpegCommit|$dependencyConfiguration|$zlibKey|$($contract.StampFlavor)"
$actualStamp = (Get-Content -LiteralPath $ffmpegStamp -Raw).Trim()
if ($actualStamp -ne $expectedStamp) {
    throw "The FFmpeg runtime provenance does not match the requested configuration, architecture, and toolset."
}

$expectedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$copyOperations = @()
foreach ($runtime in $contract.Files) {
    [void]$expectedNames.Add($runtime.FileName)
    $source = Join-Path $sourceDirectory $runtime.FileName
    $destination = Join-Path $destinationDirectory $runtime.FileName
    Assert-KeireContainedWindowsPath -Root $resolvedRoot -Path $source
    Assert-KeireContainedWindowsPath -Root $resolvedRoot -Path $destination
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "The pinned FFmpeg asset-worker runtime is missing '$($runtime.FileName)' from $sourceDirectory."
    }
    Assert-KeireWindowsPeArchitecture -Path $source -Architecture $normalizedArchitecture
    $copyOperations += [pscustomobject]@{ Source = $source; Destination = $destination }
}

$seenSourcePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($pattern in $contract.NamespacePatterns) {
    foreach ($candidate in @(Get-ChildItem -LiteralPath $sourceDirectory -Filter $pattern -Force -ErrorAction Stop)) {
        if (-not $seenSourcePaths.Add($candidate.FullName)) {
            continue
        }
        Assert-KeireContainedWindowsPath -Root $resolvedRoot -Path $candidate.FullName
        if ($candidate.PSIsContainer -or -not $expectedNames.Contains($candidate.Name)) {
            throw "The pinned FFmpeg source contains an unexpected runtime component: $($candidate.FullName)"
        }
    }
}

$staleCandidates = @()
$seenDestinationPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($pattern in $contract.NamespacePatterns) {
    foreach ($candidate in @(
            Get-ChildItem -LiteralPath $destinationDirectory -Filter $pattern -Force -ErrorAction Stop)) {
        if (-not $seenDestinationPaths.Add($candidate.FullName)) {
            continue
        }
        if ($expectedNames.Contains($candidate.Name)) {
            continue
        }
        if ($candidate.PSIsContainer) {
            throw "Refusing to treat FFmpeg-family directory '$($candidate.FullName)' as a runtime DLL."
        }
        Assert-KeireContainedWindowsPath -Root $resolvedRoot -Path $candidate.FullName
        if (($candidate.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to remove stale reparse-point FFmpeg runtime '$($candidate.FullName)'."
        }
        $staleCandidates += $candidate.FullName
    }
}

foreach ($operation in $copyOperations) {
    & (Join-Path $PSScriptRoot "copy-file-if-changed.ps1") `
        -Source $operation.Source -Destination $operation.Destination
}
foreach ($candidate in $staleCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        Remove-Item -LiteralPath $candidate -Force
    }
}

Write-Host "==> Staged pinned FFmpeg runtime for ${ProjectNamespace}AssetWorker"
