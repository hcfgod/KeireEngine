[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Build,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
$PatchRoot = Join-Path $Root "Patches\Coral"
$PatchFiles = @(Get-ChildItem -LiteralPath $PatchRoot -Filter "*.patch" -File | Sort-Object Name)
if ($PatchFiles.Count -eq 0) {
    throw "The Kéire Coral patch set is empty."
}

$Hasher = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
try {
    foreach ($Patch in $PatchFiles) {
        $Name = [Text.Encoding]::UTF8.GetBytes($Patch.Name + "`n")
        $Hasher.AppendData($Name)
        $Hasher.AppendData([IO.File]::ReadAllBytes($Patch.FullName))
    }
    $PatchDigest = [BitConverter]::ToString($Hasher.GetHashAndReset()).Replace("-", "").ToLowerInvariant()
}
finally {
    $Hasher.Dispose()
}

$SourceRoot = Join-Path $env:LOCALAPPDATA "KeireDependencySources"
$Source = Join-Path $SourceRoot "coral-$($Lock.CORAL_COMMIT)"
if (-not (Test-Path -LiteralPath (Join-Path $Source ".git"))) {
    New-Item -ItemType Directory -Force -Path $SourceRoot | Out-Null
    $TemporarySource = Join-Path $SourceRoot "coral-$($Lock.CORAL_COMMIT).tmp-$PID"
    try {
        & git clone --quiet --filter=blob:none --no-checkout $Lock.CORAL_URL $TemporarySource
        if ($LASTEXITCODE -ne 0) { throw "Could not clone Coral." }
        & git -C $TemporarySource config core.autocrlf false
        if ($LASTEXITCODE -ne 0) { throw "Could not configure deterministic Coral source line endings." }
        & git -C $TemporarySource fetch --quiet --depth 1 origin $Lock.CORAL_COMMIT
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch locked Coral commit $($Lock.CORAL_COMMIT)." }
        & git -C $TemporarySource checkout --quiet --detach $Lock.CORAL_COMMIT
        if ($LASTEXITCODE -ne 0) { throw "Could not check out locked Coral commit $($Lock.CORAL_COMMIT)." }
        Move-Item -LiteralPath $TemporarySource -Destination $Source
    }
    catch {
        if (Test-Path -LiteralPath $TemporarySource) {
            $ResolvedTemporary = [IO.Path]::GetFullPath($TemporarySource)
            $ResolvedRoot = [IO.Path]::GetFullPath($SourceRoot) + [IO.Path]::DirectorySeparatorChar
            if ($ResolvedTemporary.StartsWith($ResolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $ResolvedTemporary -Recurse -Force
            }
        }
        throw
    }
}
$ActualCommit = ([string](& git -C $Source rev-parse HEAD)).Trim()
if ($LASTEXITCODE -ne 0 -or $ActualCommit -ne $Lock.CORAL_COMMIT) {
    throw "Locked Coral source cache is not the expected commit: $Source"
}

$BuildRoot = Join-Path $env:LOCALAPPDATA "KeireDependencyBuilds"
$CacheKey = "$($Lock.CORAL_COMMIT.Substring(0, 12))-$($PatchDigest.Substring(0, 16))"
$Patched = Join-Path $BuildRoot "coral-$CacheKey"
$Stamp = Join-Path $Patched "keire-coral-patch.stamp"
$ExpectedStamp = "$($Lock.CORAL_COMMIT)|$PatchDigest"
$Prepared = (Test-Path -LiteralPath $Stamp) -and
    ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq $ExpectedStamp)
$NetHostLibrary = ""
if (-not $Prepared) {
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $TemporaryPatched = Join-Path $BuildRoot "coral-$CacheKey.tmp-$PID"
    try {
        & git clone --quiet --no-hardlinks --shared $Source $TemporaryPatched
        if ($LASTEXITCODE -ne 0) { throw "Could not create the Coral patch worktree." }
        foreach ($Patch in $PatchFiles) {
            & git -C $TemporaryPatched apply --whitespace=error-all $Patch.FullName
            if ($LASTEXITCODE -ne 0) { throw "Coral patch failed to apply: $($Patch.Name)" }
        }
        [IO.File]::WriteAllText((Join-Path $TemporaryPatched "keire-coral-patch.stamp"),
            "$ExpectedStamp`n", [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Patched) {
            $ResolvedPatched = [IO.Path]::GetFullPath($Patched)
            $ResolvedRoot = [IO.Path]::GetFullPath($BuildRoot) + [IO.Path]::DirectorySeparatorChar
            if (-not $ResolvedPatched.StartsWith($ResolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to replace a Coral cache outside $BuildRoot."
            }
            Remove-Item -LiteralPath $Patched -Recurse -Force
        }
        Move-Item -LiteralPath $TemporaryPatched -Destination $Patched
    }
    catch {
        if (Test-Path -LiteralPath $TemporaryPatched) {
            $ResolvedTemporary = [IO.Path]::GetFullPath($TemporaryPatched)
            $ResolvedRoot = [IO.Path]::GetFullPath($BuildRoot) + [IO.Path]::DirectorySeparatorChar
            if ($ResolvedTemporary.StartsWith($ResolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $ResolvedTemporary -Recurse -Force
            }
        }
        throw
    }
    Write-Host "==> Coral patch cache prepared at $Patched"
}
else {
    Write-Host "==> Coral patch cache is current"
}

if ($Build) {
    $WorkspaceDotnet = Join-Path $Root "Build\Tools\dotnet10\dotnet.exe"
    $CachedDotnet = Join-Path $env:LOCALAPPDATA "KeireTools\dotnet10\dotnet.exe"
    if (Test-Path -LiteralPath $CachedDotnet) {
        $DotnetExecutable = $CachedDotnet
    }
    elseif (Test-Path -LiteralPath $WorkspaceDotnet) {
        $DotnetExecutable = $WorkspaceDotnet
    }
    else {
        $DotnetExecutable = (Get-Command dotnet -CommandType Application -ErrorAction Stop).Source
    }
    $env:DOTNET_ROOT = Split-Path -Parent $DotnetExecutable
    $env:PATH = "$env:DOTNET_ROOT;$env:PATH"
    $SdkVersions = @(& $DotnetExecutable --list-sdks | ForEach-Object {
        if ($_ -match '^([0-9]+\.[0-9]+\.[0-9]+)') { [Version]$Matches[1] }
    })
    if ($LASTEXITCODE -ne 0 -or -not ($SdkVersions | Where-Object { $_.Major -eq 10 })) {
        throw "Coral requires the .NET 10 SDK. A .NET 10 runtime alone cannot compile Coral.Managed."
    }
    $Architecture = Get-NativeArchitecture
    Enter-WindowsToolEnvironment "ninja" "msc" $Architecture | Out-Null
    $Ninja = Get-NinjaExecutable
    $NativeBuild = Join-Path $Patched "Build\$Configuration"
    if ($Force -and (Test-Path -LiteralPath $NativeBuild)) {
        $ResolvedBuild = [IO.Path]::GetFullPath($NativeBuild)
        $ResolvedPatched = [IO.Path]::GetFullPath($Patched) + [IO.Path]::DirectorySeparatorChar
        if (-not $ResolvedBuild.StartsWith($ResolvedPatched, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace a Coral build outside $Patched."
        }
        Remove-Item -LiteralPath $NativeBuild -Recurse -Force
    }
    & cmake -S (Join-Path $Patched "cmake") -B $NativeBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DCMAKE_BUILD_TYPE=$Configuration" "-DCORAL_TESTING=OFF" "-DCORAL_EXAMPLE=OFF"
    if ($LASTEXITCODE -ne 0) { throw "Patched Coral configuration failed." }
    & cmake --build $NativeBuild --target Coral.Native --parallel
    if ($LASTEXITCODE -ne 0) { throw "Patched Coral build failed." }
    if (-not (Test-Path -LiteralPath (Join-Path $NativeBuild "Coral.Managed.dll"))) {
        throw "Patched Coral build did not produce Coral.Managed.dll."
    }
    $DotnetHostArchitecture = if ($Architecture -eq "ARM64") { "arm64" } else { "x64" }
    $NetHostPack = Join-Path $env:DOTNET_ROOT "packs\Microsoft.NETCore.App.Host.win-$DotnetHostArchitecture"
    $NetHostVersion = Get-ChildItem -LiteralPath $NetHostPack -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.' } |
        Sort-Object { [Version]$_.Name } -Descending |
        Select-Object -First 1
    if ($NetHostVersion) {
        $NetHostLibrary = Join-Path $NetHostVersion.FullName `
            "runtimes\win-$DotnetHostArchitecture\native\nethost.lib"
    }
    if (-not $NetHostLibrary -or -not (Test-Path -LiteralPath $NetHostLibrary -PathType Leaf)) {
        throw "The .NET 10 SDK does not contain the static nethost import library."
    }
    Write-Host "==> Patched Coral $Configuration build is ready"
}

[PSCustomObject]@{
    Source = $Patched
    Commit = $Lock.CORAL_COMMIT
    PatchDigest = $PatchDigest
    Configuration = $Configuration
    BuildDirectory = Join-Path $Patched "Build\$Configuration"
    NetHostLibrary = $NetHostLibrary
    NetHostRuntime = if ($NetHostLibrary) { Join-Path (Split-Path $NetHostLibrary) "nethost.dll" } else { "" }
}
