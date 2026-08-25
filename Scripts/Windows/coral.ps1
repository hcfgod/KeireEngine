[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [switch]$Build,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
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

function Resolve-KeirePinnedDotnetSdk {
    param([Parameter(Mandatory = $true)][string]$Version)

    if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "The locked .NET SDK version is invalid: $Version"
    }
    $candidates = [Collections.Generic.List[string]]::new()
    $candidates.Add((Join-Path $env:LOCALAPPDATA "KeireTools\dotnet10\dotnet.exe"))
    $candidates.Add((Join-Path $Root "Build\Tools\dotnet10\dotnet.exe"))
    $pathCommand = Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue
    if ($pathCommand) { $candidates.Add($pathCommand.Source) }

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        $executable = Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if (-not $executable -or $executable.PSIsContainer) { continue }
        $dotnetRoot = (Get-Item -LiteralPath (Split-Path $executable.FullName -Parent) -Force).FullName
        $expectedSdkRoot = [IO.Path]::GetFullPath((Join-Path $dotnetRoot "sdk")).TrimEnd('\', '/')
        $listing = @(& $executable.FullName --list-sdks 2>$null)
        $listingStatus = $LASTEXITCODE
        if ($listingStatus -ne 0) { continue }
        $reportsPinnedSdk = $false
        foreach ($line in $listing) {
            if ($line -notmatch '^([^\s]+)\s+\[([^\[\]]+)\]\s*$' -or $Matches[1] -ne $Version) { continue }
            $reportedSdkRoot = [IO.Path]::GetFullPath($Matches[2]).TrimEnd('\', '/')
            if ([string]::Equals($reportedSdkRoot, $expectedSdkRoot, [StringComparison]::OrdinalIgnoreCase)) {
                $reportsPinnedSdk = $true
                break
            }
        }
        if (-not $reportsPinnedSdk) { continue }

        $savedDotnetRoot = $env:DOTNET_ROOT
        $savedMultilevelLookup = $env:DOTNET_MULTILEVEL_LOOKUP
        $selectedVersion = ""
        $selectedStatus = 1
        try {
            $env:DOTNET_ROOT = $dotnetRoot
            $env:DOTNET_MULTILEVEL_LOOKUP = "0"
            Push-Location $dotnetRoot
            try {
                $selectedVersion = ([string](& $executable.FullName --version 2>$null)).Trim()
                $selectedStatus = $LASTEXITCODE
            }
            finally {
                Pop-Location
            }
        }
        finally {
            $env:DOTNET_ROOT = $savedDotnetRoot
            $env:DOTNET_MULTILEVEL_LOOKUP = $savedMultilevelLookup
        }
        if ($selectedStatus -eq 0 -and $selectedVersion -eq $Version) {
            return [pscustomobject]@{
                Executable = $executable.FullName
                Root = $dotnetRoot
                Version = $Version
            }
        }
    }
    throw "Coral requires the pinned .NET SDK $Version from one canonical installation."
}

function Resolve-KeirePinnedNetHost {
    param(
        [Parameter(Mandatory = $true)][string]$DotnetRoot,
        [Parameter(Mandatory = $true)][string]$SdkVersion,
        [Parameter(Mandatory = $true)][string]$TargetArchitecture
    )

    $sdkMajor = ([Version]$SdkVersion).Major
    $targetFramework = "net$sdkMajor.0"
    $runtimeIdentifier = if ((Normalize-Architecture $TargetArchitecture) -eq "ARM64") {
        "win-arm64"
    }
    else {
        "win-x64"
    }
    $bundledVersions = Join-Path $DotnetRoot "sdk\$SdkVersion\Microsoft.NETCoreSdk.BundledVersions.props"
    if (-not (Test-Path -LiteralPath $bundledVersions -PathType Leaf)) {
        throw "The pinned .NET SDK is missing Microsoft.NETCoreSdk.BundledVersions.props."
    }
    [xml]$document = Get-Content -LiteralPath $bundledVersions -Raw
    $packs = @($document.Project.ItemGroup.KnownAppHostPack | Where-Object {
        $_.Include -eq "Microsoft.NETCore.App" -and $_.TargetFramework -eq $targetFramework
    })
    if ($packs.Count -ne 1) {
        throw "The pinned .NET SDK must describe exactly one $targetFramework Microsoft.NETCore.App host pack."
    }
    $packVersion = [string]$packs[0].AppHostPackVersion
    if ($packVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "The pinned .NET SDK host-pack version is invalid: $packVersion"
    }
    $supportedRids = @(([string]$packs[0].AppHostRuntimeIdentifiers) -split ';')
    if ($runtimeIdentifier -notin $supportedRids) {
        throw "The pinned .NET SDK host pack does not support $runtimeIdentifier."
    }

    $nativeRoot = Join-Path $DotnetRoot `
        "packs\Microsoft.NETCore.App.Host.$runtimeIdentifier\$packVersion\runtimes\$runtimeIdentifier\native"
    $library = Get-Item -LiteralPath (Join-Path $nativeRoot "nethost.lib") -Force -ErrorAction SilentlyContinue
    $runtime = Get-Item -LiteralPath (Join-Path $nativeRoot "nethost.dll") -Force -ErrorAction SilentlyContinue
    if (-not $library -or $library.PSIsContainer -or -not $runtime -or $runtime.PSIsContainer) {
        throw "The pinned .NET SDK host pack is missing nethost for $runtimeIdentifier $packVersion."
    }
    $libraryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $library.FullName).Hash.ToLowerInvariant()
    $runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtime.FullName).Hash.ToLowerInvariant()
    $canonicalLibrary = $library.FullName.Normalize([Text.NormalizationForm]::FormC).ToUpperInvariant()
    $canonicalRuntime = $runtime.FullName.Normalize([Text.NormalizationForm]::FormC).ToUpperInvariant()
    return [pscustomobject]@{
        RuntimeIdentifier = $runtimeIdentifier
        PackVersion = $packVersion
        Library = $library.FullName
        Runtime = $runtime.FullName
        Identity = "$runtimeIdentifier|$packVersion|$canonicalLibrary|$libraryHash|$canonicalRuntime|$runtimeHash"
    }
}

$SourceRoot = Join-Path $env:LOCALAPPDATA "KeireDependencySources"
$SourceContainerRoot = Split-Path $SourceRoot -Parent
$Source = Join-Path $SourceRoot "coral-$($Lock.CORAL_COMMIT)"
if (Test-Path -LiteralPath $Source) {
    Assert-KeireLockedGitSource -Path $Source -ExpectedCommit $Lock.CORAL_COMMIT -Name "Coral"
}
else {
    New-Item -ItemType Directory -Force -Path $SourceRoot | Out-Null
    $SourceLock = Enter-KeireWorkspaceLock -RepositoryRoot $SourceRoot `
        -CommandName "dependency-source-coral-$($Lock.CORAL_COMMIT)" `
        -LockRelativePath ".locks\coral-$($Lock.CORAL_COMMIT).lock"
    $TemporarySource = Join-Path $SourceRoot "coral-$($Lock.CORAL_COMMIT).tmp-$PID"
    try {
        if (Test-Path -LiteralPath $Source) {
            Assert-KeireLockedGitSource -Path $Source -ExpectedCommit $Lock.CORAL_COMMIT -Name "Coral"
        }
        else {
            if (Get-Item -LiteralPath $TemporarySource -Force -ErrorAction SilentlyContinue) {
                Remove-KeireGeneratedDirectory -RepositoryRoot $SourceContainerRoot -AllowedRoot $SourceRoot `
                    -Path $TemporarySource -Description "temporary Coral source"
            }
            & git clone --quiet --filter=blob:none --no-checkout $Lock.CORAL_URL $TemporarySource
            if ($LASTEXITCODE -ne 0) { throw "Could not clone Coral." }
            & git -C $TemporarySource config core.autocrlf false
            if ($LASTEXITCODE -ne 0) { throw "Could not configure deterministic Coral source line endings." }
            & git -C $TemporarySource fetch --quiet --depth 1 origin $Lock.CORAL_COMMIT
            if ($LASTEXITCODE -ne 0) { throw "Could not fetch locked Coral commit $($Lock.CORAL_COMMIT)." }
            & git -C $TemporarySource checkout --quiet --detach $Lock.CORAL_COMMIT
            if ($LASTEXITCODE -ne 0) { throw "Could not check out locked Coral commit $($Lock.CORAL_COMMIT)." }
            Assert-KeireLockedGitSource -Path $TemporarySource -ExpectedCommit $Lock.CORAL_COMMIT -Name "Coral"
            Move-Item -LiteralPath $TemporarySource -Destination $Source
        }
    }
    catch {
        $Failure = $_
        if (Get-Item -LiteralPath $TemporarySource -Force -ErrorAction SilentlyContinue) {
            try {
                Remove-KeireGeneratedDirectory -RepositoryRoot $SourceContainerRoot -AllowedRoot $SourceRoot `
                    -Path $TemporarySource -Description "temporary Coral source"
            }
            catch {
                Write-Warning "Could not safely clean temporary Coral source '$TemporarySource': $($_.Exception.Message)"
            }
        }
        throw $Failure
    }
    finally {
        Exit-KeireWorkspaceLock -Lock $SourceLock
    }
}
Assert-KeireLockedGitSource -Path $Source -ExpectedCommit $Lock.CORAL_COMMIT -Name "Coral"

$DotnetSdk = Resolve-KeirePinnedDotnetSdk -Version $Lock.DOTNET_SDK_VERSION
$DotnetExecutable = $DotnetSdk.Executable
$env:DOTNET_ROOT = $DotnetSdk.Root
$env:DOTNET_MULTILEVEL_LOOKUP = "0"
$env:PATH = "$env:DOTNET_ROOT;$env:PATH"
$CompilerIdentity = Get-WindowsToolchainIdentity -Generator "ninja" -Toolset "msc" `
    -Architecture $Architecture
$environmentDescriptor = "CL=$([string]$env:CL)`n_CL_=$([string]$env:_CL_)`n" +
    "LINK=$([string]$env:LINK)`n_LINK_=$([string]$env:_LINK_)`n" +
    "CFLAGS=$([string]$env:CFLAGS)`nCXXFLAGS=$([string]$env:CXXFLAGS)`n" +
    "CPPFLAGS=$([string]$env:CPPFLAGS)`nLDFLAGS=$([string]$env:LDFLAGS)"
$environmentHasher = [Security.Cryptography.SHA256]::Create()
try {
    $environmentDigest = $environmentHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($environmentDescriptor))
}
finally {
    $environmentHasher.Dispose()
}
$environmentIdentity = [BitConverter]::ToString($environmentDigest).Replace('-', '').ToLowerInvariant()
$CompilerIdentity = "$CompilerIdentity-env$($environmentIdentity.Substring(0, 16))"
$NetHost = Resolve-KeirePinnedNetHost -DotnetRoot $DotnetSdk.Root -SdkVersion $DotnetSdk.Version `
    -TargetArchitecture $Architecture
$WorkspaceIdentity = Get-KeireWorkspaceIdentity $Root
$BuildVariant = Get-KeireCoralBuildVariantKey -Architecture $Architecture `
    -CompilerIdentity $CompilerIdentity -DotnetSdkVersion $DotnetSdk.Version -DotnetRoot $DotnetSdk.Root `
    -NetHostIdentity $NetHost.Identity -WorkspaceIdentity $WorkspaceIdentity
$BuildRoot = Join-Path $env:LOCALAPPDATA "KeireDependencyBuilds"
$BuildContainerRoot = Split-Path $BuildRoot -Parent
$CacheKey = "$($Lock.CORAL_COMMIT.Substring(0, 12))-$($PatchDigest.Substring(0, 16))-$BuildVariant"
$Patched = Join-Path $BuildRoot "coral-$CacheKey"
$Stamp = Join-Path $Patched "keire-coral-patch.stamp"
$ExpectedStamp = "$($Lock.CORAL_COMMIT)|$PatchDigest|$BuildVariant|$($NetHost.RuntimeIdentifier)|$($NetHost.PackVersion)"
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$BuildLock = Enter-KeireWorkspaceLock -RepositoryRoot $BuildRoot -CommandName "coral-build-$CacheKey" `
    -LockRelativePath ".locks\coral-$CacheKey.lock"
try {
$PatchedItem = Get-Item -LiteralPath $Patched -Force -ErrorAction SilentlyContinue
if ($PatchedItem -and (-not $PatchedItem.PSIsContainer -or
        (($PatchedItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0))) {
    throw "Coral build cache is not an ordinary directory: $Patched"
}
$Prepared = (Test-Path -LiteralPath $Stamp) -and
    ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq $ExpectedStamp)
$NetHostLibrary = $NetHost.Library
$NetHostRuntime = $NetHost.Runtime
if (-not $Prepared) {
    $TemporaryPatched = Join-Path $BuildRoot "coral-$CacheKey.tmp-$PID"
    try {
        if (Get-Item -LiteralPath $TemporaryPatched -Force -ErrorAction SilentlyContinue) {
            Remove-KeireGeneratedDirectory -RepositoryRoot $BuildContainerRoot -AllowedRoot $BuildRoot `
                -Path $TemporaryPatched -Description "temporary Coral build cache"
        }
        & git clone --quiet --no-hardlinks --shared $Source $TemporaryPatched
        if ($LASTEXITCODE -ne 0) { throw "Could not create the Coral patch worktree." }
        foreach ($Patch in $PatchFiles) {
            & git -C $TemporaryPatched apply --whitespace=error-all $Patch.FullName
            if ($LASTEXITCODE -ne 0) { throw "Coral patch failed to apply: $($Patch.Name)" }
        }
        [IO.File]::WriteAllText((Join-Path $TemporaryPatched "keire-coral-patch.stamp"),
            "$ExpectedStamp`n", [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Patched) {
            Remove-KeireGeneratedDirectory -RepositoryRoot $BuildContainerRoot -AllowedRoot $BuildRoot `
                -Path $Patched -Description "Coral build cache"
        }
        Move-Item -LiteralPath $TemporaryPatched -Destination $Patched
    }
    catch {
        $Failure = $_
        if (Get-Item -LiteralPath $TemporaryPatched -Force -ErrorAction SilentlyContinue) {
            try {
                Remove-KeireGeneratedDirectory -RepositoryRoot $BuildContainerRoot -AllowedRoot $BuildRoot `
                    -Path $TemporaryPatched -Description "temporary Coral build cache"
            }
            catch {
                Write-Warning "Could not safely clean temporary Coral cache '$TemporaryPatched': $($_.Exception.Message)"
            }
        }
        throw $Failure
    }
    Write-Host "==> Coral patch cache prepared at $Patched"
}
else {
    Write-Host "==> Coral patch cache is current"
}

if ($Build) {
    $Ninja = Get-NinjaExecutable
    $NativeBuild = Join-Path $Patched "Build\$Configuration"
    if ($Force -and (Test-Path -LiteralPath $NativeBuild)) {
        Remove-KeireGeneratedDirectory -RepositoryRoot $BuildRoot -AllowedRoot $Patched -Path $NativeBuild `
            -Description "Coral native build"
    }
    Initialize-KeireOrdinaryChildDirectory -Root $Patched -Path $NativeBuild `
        -Description "Coral native build path"
    & cmake -S (Join-Path $Patched "cmake") -B $NativeBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" `
        "-DCMAKE_BUILD_TYPE=$Configuration" "-DCORAL_TESTING=OFF" "-DCORAL_EXAMPLE=OFF" `
        "-DDOTNET_EXE=$DotnetExecutable"
    if ($LASTEXITCODE -ne 0) { throw "Patched Coral configuration failed." }
    & cmake --build $NativeBuild --target Coral.Native --parallel
    if ($LASTEXITCODE -ne 0) { throw "Patched Coral build failed." }
    if (-not (Test-Path -LiteralPath (Join-Path $NativeBuild "Coral.Managed.dll"))) {
        throw "Patched Coral build did not produce Coral.Managed.dll."
    }
    Write-Host "==> Patched Coral $Configuration build is ready"
}
}
finally {
    Exit-KeireWorkspaceLock -Lock $BuildLock
}

[PSCustomObject]@{
    Source = $Patched
    Commit = $Lock.CORAL_COMMIT
    PatchDigest = $PatchDigest
    BuildVariant = $BuildVariant
    Configuration = $Configuration
    BuildDirectory = Join-Path $Patched "Build\$Configuration"
    NetHostLibrary = $NetHostLibrary
    NetHostRuntime = $NetHostRuntime
    DotnetRoot = $DotnetSdk.Root
}
