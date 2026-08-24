[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$windowsScripts = Join-Path $root "Scripts\Windows"
$stageScript = Join-Path $windowsScripts "stage-ffmpeg-runtime.ps1"
. (Join-Path $windowsScripts "ffmpeg-runtime-contract.ps1")

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "Assertion failed: $Message"
    }
}

function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try {
        & $Action
    }
    catch {
        return
    }
    throw "Assertion failed: $Message"
}

function New-TestPeImage {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateSet("x86_64", "ARM64")][string]$Architecture,
        [Parameter(Mandatory = $true)][string]$Marker
    )

    $bytes = [byte[]]::new(256)
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    $bytes[0x3C] = 0x80
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    $machine = if ($Architecture -eq "ARM64") { 0xAA64 } else { 0x8664 }
    $bytes[0x84] = $machine -band 0xFF
    $bytes[0x85] = ($machine -shr 8) -band 0xFF
    $markerBytes = [Text.Encoding]::UTF8.GetBytes($Marker)
    if ($markerBytes.Length -gt 80) {
        throw "Test PE marker is too long: $Marker"
    }
    [Array]::Copy($markerBytes, 0, $bytes, 0xA0, $markerBytes.Length)
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Initialize-RuntimeFixture {
    param(
        [Parameter(Mandatory = $true)][string]$FixtureRoot,
        [Parameter(Mandatory = $true)][string]$Configuration,
        [Parameter(Mandatory = $true)][string]$DependencyConfiguration,
        [Parameter(Mandatory = $true)][string]$Commit,
        [Parameter(Mandatory = $true)][string]$Marker
    )

    $contract = Get-KeireFfmpegRuntimeContract
    $zlibKey = "fixture-zlib-x86_64-msc"
    $sourceDirectory = Join-Path $FixtureRoot "Build\Dependencies\ffmpeg\$DependencyConfiguration\install\bin"
    $ffmpegStamp = Join-Path $FixtureRoot `
        "Build\Dependencies\ffmpeg\$DependencyConfiguration\keire-ffmpeg.stamp"
    $zlibStamp = Join-Path $FixtureRoot `
        "Build\Dependencies\windows-x86_64-msc\Release\keire-dependency.stamp"
    $destinationDirectory = Join-Path $FixtureRoot `
        "Build\Bin\$Configuration-windows-x86_64\FixtureAssetWorker"

    [IO.Directory]::CreateDirectory($sourceDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
    [IO.Directory]::CreateDirectory((Split-Path -Parent $ffmpegStamp)) | Out-Null
    [IO.Directory]::CreateDirectory((Split-Path -Parent $zlibStamp)) | Out-Null
    [IO.File]::WriteAllText(
        $ffmpegStamp,
        "$Commit|$DependencyConfiguration|$zlibKey|$($contract.StampFlavor)")
    [IO.File]::WriteAllText($zlibStamp, $zlibKey)
    New-TestPeImage -Path (Join-Path $destinationDirectory "FixtureAssetWorker.exe") `
        -Architecture x86_64 -Marker "$Marker-worker"
    foreach ($runtime in $contract.Files) {
        New-TestPeImage -Path (Join-Path $sourceDirectory $runtime.FileName) `
            -Architecture x86_64 -Marker "$Marker-$($runtime.FileName)"
    }

    return [pscustomobject]@{
        SourceDirectory = $sourceDirectory
        DestinationDirectory = $destinationDirectory
        FfmpegStamp = $ffmpegStamp
        ZlibStamp = $zlibStamp
    }
}

function Invoke-RuntimeStage([string]$FixtureRoot, [string]$Configuration, [string]$Commit) {
    & $stageScript -Root $FixtureRoot -Configuration $Configuration -Architecture x86_64 -Toolset msc `
        -ProjectNamespace Fixture -FfmpegCommit $Commit
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "keire-windows-ffmpeg-runtime-$([Guid]::NewGuid().ToString('N'))"
$releaseFixtureRoot = "$fixtureRoot-release"
$sourceReparseFixtureRoot = "$fixtureRoot-source-reparse"
$sourceReparseOutside = "$fixtureRoot-source-outside"
$destinationReparseFixtureRoot = "$fixtureRoot-destination-reparse"
$destinationReparseOutside = "$fixtureRoot-destination-outside"
$junctions = [Collections.Generic.List[string]]::new()
$mappingFixtureRoots = [Collections.Generic.List[string]]::new()
$commit = "1111111111111111111111111111111111111111"
$contract = Get-KeireFfmpegRuntimeContract

try {
    $fixture = Initialize-RuntimeFixture -FixtureRoot $fixtureRoot -Configuration DebugASan `
        -DependencyConfiguration Debug -Commit $commit -Marker debug
    Invoke-RuntimeStage -FixtureRoot $fixtureRoot -Configuration DebugASan -Commit $commit
    foreach ($runtime in $contract.Files) {
        $source = Join-Path $fixture.SourceDirectory $runtime.FileName
        $destination = Join-Path $fixture.DestinationDirectory $runtime.FileName
        Assert-True (Test-Path -LiteralPath $destination -PathType Leaf) `
            "staging copies exact pinned runtime '$($runtime.FileName)'"
        Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash -eq
                     (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash) `
            "staged runtime '$($runtime.FileName)' matches its source byte-for-byte"
    }

    $unchangedDestination = Join-Path $fixture.DestinationDirectory "avformat-63.dll"
    $unchangedWriteTime = (Get-Item -LiteralPath $unchangedDestination).LastWriteTimeUtc
    $deletedDestination = Join-Path $fixture.DestinationDirectory "avcodec-63.dll"
    Remove-Item -LiteralPath $deletedDestination
    $corruptSource = Join-Path $fixture.SourceDirectory "avutil-61.dll"
    $corruptDestination = Join-Path $fixture.DestinationDirectory "avutil-61.dll"
    $corruptBytes = [IO.File]::ReadAllBytes($corruptDestination)
    $corruptBytes[0xF0] = $corruptBytes[0xF0] -bxor 0xFF
    [IO.File]::WriteAllBytes($corruptDestination, $corruptBytes)
    (Get-Item -LiteralPath $corruptDestination).LastWriteTimeUtc =
        (Get-Item -LiteralPath $corruptSource).LastWriteTimeUtc
    $staleRuntime = Join-Path $fixture.DestinationDirectory "avcodec-62.dll"
    $unexpectedRuntime = Join-Path $fixture.DestinationDirectory "avfilter-12.dll"
    $unrelatedRuntime = Join-Path $fixture.DestinationDirectory "SDL3.dll"
    [IO.File]::WriteAllText($staleRuntime, "stale")
    [IO.File]::WriteAllText($unexpectedRuntime, "unexpected")
    [IO.File]::WriteAllText($unrelatedRuntime, "unrelated")

    $unchangedHandle = [IO.File]::Open(
        $unchangedDestination,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        Invoke-RuntimeStage -FixtureRoot $fixtureRoot -Configuration DebugASan -Commit $commit
    }
    finally {
        $unchangedHandle.Dispose()
    }
    Assert-True (Test-Path -LiteralPath $deletedDestination -PathType Leaf) `
        "a deleted pinned runtime is restored on the next successful build"
    Assert-True ((Get-Item -LiteralPath $unchangedDestination).LastWriteTimeUtc -eq $unchangedWriteTime) `
        "byte-identical runtime files follow the no-op copy path"
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $corruptSource).Hash -eq
                 (Get-FileHash -Algorithm SHA256 -LiteralPath $corruptDestination).Hash) `
        "same-length, same-timestamp corruption is repaired by content comparison"
    Assert-True (-not (Test-Path -LiteralPath $staleRuntime)) `
        "staging prunes stale known FFmpeg-family DLLs"
    Assert-True (-not (Test-Path -LiteralPath $unexpectedRuntime)) `
        "staging prunes unexpected FFmpeg namespace components"
    Assert-True (Test-Path -LiteralPath $unrelatedRuntime -PathType Leaf) `
        "staging preserves unrelated runtime DLLs"

    $exactSource = Join-Path $fixture.SourceDirectory "avcodec-63.dll"
    $savedExactSource = "$exactSource.saved"
    Move-Item -LiteralPath $exactSource -Destination $savedExactSource
    New-TestPeImage -Path (Join-Path $fixture.SourceDirectory "avcodec-62.dll") `
        -Architecture x86_64 -Marker wrong-version
    try {
        Assert-Throws { Invoke-RuntimeStage -FixtureRoot $fixtureRoot -Configuration DebugASan -Commit $commit } `
            "a wrong-version-only FFmpeg source must not satisfy the pinned contract"
    }
    finally {
        Remove-Item -LiteralPath (Join-Path $fixture.SourceDirectory "avcodec-62.dll") -Force
        Move-Item -LiteralPath $savedExactSource -Destination $exactSource
    }

    $unexpectedSource = Join-Path $fixture.SourceDirectory "swscale-10.dll"
    New-TestPeImage -Path $unexpectedSource -Architecture x86_64 -Marker unexpected-component
    try {
        Assert-Throws { Invoke-RuntimeStage -FixtureRoot $fixtureRoot -Configuration DebugASan -Commit $commit } `
            "an unexpected FFmpeg source component must invalidate the pinned closure"
    }
    finally {
        Remove-Item -LiteralPath $unexpectedSource -Force
    }

    $validStamp = [IO.File]::ReadAllText($fixture.FfmpegStamp)
    [IO.File]::WriteAllText(
        $fixture.FfmpegStamp,
        "2222222222222222222222222222222222222222|Debug|fixture-zlib-x86_64-msc|$($contract.StampFlavor)")
    try {
        Assert-Throws { Invoke-RuntimeStage -FixtureRoot $fixtureRoot -Configuration DebugASan -Commit $commit } `
            "runtime staging rejects a source whose pinned provenance stamp does not match"
    }
    finally {
        [IO.File]::WriteAllText($fixture.FfmpegStamp, $validStamp)
    }

    $releaseFixture = Initialize-RuntimeFixture -FixtureRoot $releaseFixtureRoot -Configuration Dist `
        -DependencyConfiguration Release -Commit $commit -Marker release
    Invoke-RuntimeStage -FixtureRoot $releaseFixtureRoot -Configuration Dist -Commit $commit
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath `
                      (Join-Path $releaseFixture.SourceDirectory "avformat-63.dll")).Hash -eq
                 (Get-FileHash -Algorithm SHA256 -LiteralPath `
                      (Join-Path $releaseFixture.DestinationDirectory "avformat-63.dll")).Hash) `
        "Dist maps to the Release FFmpeg dependency configuration"

    $wrongArchitecture = Join-Path $releaseFixture.SourceDirectory "swresample-7.dll"
    New-TestPeImage -Path $wrongArchitecture -Architecture ARM64 -Marker wrong-architecture
    Assert-Throws { Invoke-RuntimeStage -FixtureRoot $releaseFixtureRoot -Configuration Dist -Commit $commit } `
        "runtime staging rejects a source DLL for another architecture"

    $configurationMappings = [ordered]@{
        Debug = "Debug"
        Release = "Release"
        DebugUBSan = "Debug"
        DebugTSan = "Debug"
        Coverage = "Debug"
    }
    foreach ($mapping in $configurationMappings.GetEnumerator()) {
        $mappingRoot = "$fixtureRoot-map-$($mapping.Key)"
        $mappingFixtureRoots.Add($mappingRoot)
        $mappingFixture = Initialize-RuntimeFixture -FixtureRoot $mappingRoot -Configuration $mapping.Key `
            -DependencyConfiguration $mapping.Value -Commit $commit -Marker "map-$($mapping.Key)"
        Invoke-RuntimeStage -FixtureRoot $mappingRoot -Configuration $mapping.Key -Commit $commit
        Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath `
                          (Join-Path $mappingFixture.SourceDirectory "avcodec-63.dll")).Hash -eq
                     (Get-FileHash -Algorithm SHA256 -LiteralPath `
                          (Join-Path $mappingFixture.DestinationDirectory "avcodec-63.dll")).Hash) `
            "$($mapping.Key) maps to the $($mapping.Value) FFmpeg dependency configuration"
    }

    $destinationReparseFixture = Initialize-RuntimeFixture -FixtureRoot $destinationReparseFixtureRoot `
        -Configuration Coverage -DependencyConfiguration Debug -Commit $commit -Marker destination-reparse
    [IO.Directory]::CreateDirectory($destinationReparseOutside) | Out-Null
    Copy-Item -LiteralPath (Join-Path $destinationReparseFixture.DestinationDirectory "FixtureAssetWorker.exe") `
        -Destination $destinationReparseOutside
    Remove-Item -LiteralPath $destinationReparseFixture.DestinationDirectory -Recurse
    New-Item -ItemType Junction -Path $destinationReparseFixture.DestinationDirectory `
        -Target $destinationReparseOutside | Out-Null
    $junctions.Add($destinationReparseFixture.DestinationDirectory)
    Assert-Throws {
        Invoke-RuntimeStage -FixtureRoot $destinationReparseFixtureRoot -Configuration Coverage -Commit $commit
    } "runtime staging rejects a destination-directory reparse-point escape"

    $sourceReparseFixture = Initialize-RuntimeFixture -FixtureRoot $sourceReparseFixtureRoot `
        -Configuration Debug -DependencyConfiguration Debug -Commit $commit -Marker source-reparse
    [IO.Directory]::CreateDirectory($sourceReparseOutside) | Out-Null
    foreach ($runtime in $contract.Files) {
        Copy-Item -LiteralPath (Join-Path $sourceReparseFixture.SourceDirectory $runtime.FileName) `
            -Destination $sourceReparseOutside
    }
    Remove-Item -LiteralPath $sourceReparseFixture.SourceDirectory -Recurse
    New-Item -ItemType Junction -Path $sourceReparseFixture.SourceDirectory -Target $sourceReparseOutside | Out-Null
    $junctions.Add($sourceReparseFixture.SourceDirectory)
    Assert-Throws {
        Invoke-RuntimeStage -FixtureRoot $sourceReparseFixtureRoot -Configuration Debug -Commit $commit
    } "runtime staging rejects a source-directory reparse-point escape"
}
finally {
    foreach ($junction in $junctions) {
        if (Test-Path -LiteralPath $junction) {
            [IO.Directory]::Delete($junction)
        }
    }
    foreach ($path in @(
            $fixtureRoot,
            $releaseFixtureRoot,
            $sourceReparseFixtureRoot,
            $sourceReparseOutside,
            $destinationReparseFixtureRoot,
            $destinationReparseOutside)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    foreach ($path in $mappingFixtureRoots) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}

Write-Host "Windows FFmpeg runtime staging tests passed."
