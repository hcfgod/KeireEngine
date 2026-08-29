[CmdletBinding()]
param(
    [ValidateSet('x86_64', 'arm64')]
    [string]$Architecture = 'x86_64',
    [string]$OutputDirectory,
    [string]$SignatureKeyId,
    [ValidateSet('stable', 'preview', 'nightly')]
    [string]$Channel = 'stable',
    [switch]$KeepStaging,
    [Parameter(DontShow = $true)][string]$InstalledLayoutRoot,
    [Parameter(DontShow = $true)][string]$PackagedLayoutPayload,
    [Parameter(DontShow = $true)][string]$PackagedLayoutManifest,
    [Parameter(DontShow = $true)][string]$RuntimeClosureSource,
    [Parameter(DontShow = $true)][string]$RuntimeClosureDestination,
    [Parameter(DontShow = $true)][string]$CatalogPublishSource,
    [Parameter(DontShow = $true)][string]$CatalogPublishOutput,
    [Parameter(DontShow = $true)][string]$CatalogPublishId,
    [Parameter(DontShow = $true)][string]$CatalogPublishEngineVersion = 'test-version',
    [Parameter(DontShow = $true)][string]$CatalogPublishPlatform = 'windows',
    [Parameter(DontShow = $true)][string]$CatalogPublishArchitecture = 'x86_64',
    [Parameter(DontShow = $true)][string]$CleanupProbeDirectory,
    [Parameter(DontShow = $true)][switch]$TestFailCatalogPublish,
    [Parameter(DontShow = $true)][switch]$TestFailCleanup,
    [Parameter(DontShow = $true)][switch]$TestPrimaryFailure
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')

function Assert-RegularPlayerSupportFile {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Player Support requires a regular non-reparse file: $Path"
    }
    $entry = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Player Support requires a regular non-reparse file: $Path"
    }
    return $entry
}

function Assert-PlayerSupportDirectory {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Player Support requires a non-reparse directory: $Path"
    }
    $entry = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Player Support requires a non-reparse directory: $Path"
    }
    return $entry
}

function Copy-FileIfChanged {
    param([string]$Source, [string]$Destination)

    $sourceFile = Assert-RegularPlayerSupportFile -Path $Source
    $destinationFile = Get-Item -LiteralPath $Destination -ErrorAction SilentlyContinue
    if ($destinationFile -and -not $destinationFile.PSIsContainer -and
        $destinationFile.Length -eq $sourceFile.Length -and
        $destinationFile.LastWriteTimeUtc -eq $sourceFile.LastWriteTimeUtc) {
        return
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $sourceFile.FullName -Destination $Destination -Force
}

function Copy-TreeIfChanged {
    param([string]$Source, [string]$Destination)

    $sourceRoot = (Assert-PlayerSupportDirectory -Path $Source).FullName
    foreach ($sourceEntry in Get-ChildItem -LiteralPath $sourceRoot -Force -Recurse) {
        if (($sourceEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Player Support runtime closure contains a symbolic link: $($sourceEntry.FullName)"
        }
        if ($sourceEntry.PSIsContainer) { continue }
        $sourceFile = $sourceEntry
        $relative = [IO.Path]::GetRelativePath($sourceRoot, $sourceFile.FullName)
        Copy-FileIfChanged -Source $sourceFile.FullName -Destination (Join-Path $Destination $relative)
    }
}

function Copy-PlayerSupportLicenses {
    param([string]$Destination, [string]$Architecture)

    $dependencyArchitecture = if ($Architecture -eq 'arm64') { 'AARCH64' } else { 'x86_64' }
    $override = [Environment]::GetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE')
    $sourceRoot = if ($override) { [IO.Path]::GetFullPath($override) } else { $repositoryRoot }
    $dependencyInstall = Join-Path $sourceRoot "Build\Dependencies\windows-$dependencyArchitecture-msc\Release\install"
    Assert-PlayerSupportDirectory -Path $sourceRoot | Out-Null
    Assert-PlayerSupportDirectory -Path $dependencyInstall | Out-Null
    $sources = [ordered]@{
        'Keire-LICENSE.txt' = Join-Path $sourceRoot 'LICENSE.txt'
        'Keire-THIRD_PARTY_NOTICES.md' = Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md'
        'Coral-LICENSE.txt' = Join-Path $sourceRoot 'Build\Dependencies\coral-patched\LICENSE'
        'dotnet-LICENSE.txt' = Join-Path $sourceRoot 'Build\Dependencies\dotnet-sdk\LICENSE.txt'
        'dotnet-ThirdPartyNotices.txt' = Join-Path $sourceRoot 'Build\Dependencies\dotnet-sdk\ThirdPartyNotices.txt'
        'SDL-LICENSE.txt' = Join-Path $dependencyInstall 'licenses\SDL3\LICENSE.txt'
        'assimp-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\assimp\LICENSE'
        'assimp-zlib-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\assimp\contrib\zlib\LICENSE'
        'stb-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\stb\LICENSE'
        'Jolt-LICENSE.txt' = Join-Path $dependencyInstall 'share\licenses\keire\Jolt-LICENSE.txt'
        'Recast-LICENSE.txt' = Join-Path $dependencyInstall 'share\licenses\keire\Recast-LICENSE.txt'
        'miniaudio-LICENSE.txt' = Join-Path $dependencyInstall 'share\licenses\keire\miniaudio-LICENSE.txt'
        'spdlog-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\spdlog\LICENSE'
        'fmt-LICENSE.rst' = Join-Path $sourceRoot 'Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst'
        'nlohmann-json-LICENSE.MIT.txt' = Join-Path $sourceRoot 'Vendor\json\LICENSE.MIT'
        'dear-imgui-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\imgui\LICENSE.txt'
        'zstandard-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\zstd\LICENSE'
        'entt-LICENSE.txt' = Join-Path $sourceRoot 'Vendor\entt\LICENSE'
        'glm-COPYING.txt' = Join-Path $sourceRoot 'Vendor\glm\copying.txt'
    }
    foreach ($name in $sources.Keys) {
        Copy-FileIfChanged -Source $sources[$name] -Destination (Join-Path $Destination $name)
    }
}

function Copy-WindowsPlayerRuntimeLibraries {
    param([string]$Destination, [string]$Architecture)

    $override = [Environment]::GetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_VC_REDIST_ROOT')
    if ($override) {
        $redistRoot = [IO.Path]::GetFullPath($override)
    }
    else {
        $environment = Get-VSBuildEnvironment 17
        Enter-WindowsToolEnvironment 'ninja' 'msc' $Architecture | Out-Null
        if (-not $env:VCToolsRedistDir) { throw 'The MSVC tool environment did not provide VCToolsRedistDir.' }
        $redistRoot = [IO.Path]::GetFullPath($env:VCToolsRedistDir)
        $allowedRoot = [IO.Path]::GetFullPath((Join-Path $environment.InstallationPath 'VC\Redist\MSVC')).TrimEnd('\') + '\'
        if (-not $redistRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The MSVC redistributable directory is outside the selected Visual Studio installation.'
        }
    }
    Assert-PlayerSupportDirectory -Path $redistRoot | Out-Null
    $target = if ($Architecture -eq 'arm64') { 'arm64' } else { 'x64' }
    $runtimeDirectory = Join-Path $redistRoot "$target\Microsoft.VC143.CRT"
    Assert-PlayerSupportDirectory -Path $runtimeDirectory | Out-Null
    foreach ($name in @('MSVCP140.dll', 'MSVCP140_ATOMIC_WAIT.dll', 'MSVCP140_1.dll',
                         'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')) {
        Copy-FileIfChanged -Source (Join-Path $runtimeDirectory $name) -Destination (Join-Path $Destination $name)
    }
}

function Copy-PlayerRuntimeClosure {
    param([string]$Source, [string]$Destination, [bool]$IncludeSymbols, [string]$TargetArchitecture)

    $sourceRoot = (Assert-PlayerSupportDirectory -Path $Source).FullName
    $required = @('KeireRuntime.exe', 'nethost.dll', 'Managed\Coral.Managed.dll',
                  'Managed\Coral.Managed.deps.json', 'Managed\Coral.Managed.runtimeconfig.json',
                  'Managed\Keire.Managed.dll')
    foreach ($relative in $required) {
        $path = Join-Path $sourceRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The player runtime closure is missing: $relative"
        }
    }
    foreach ($prohibited in @('sdk', 'packs', 'templates', 'sdk-manifests', 'metadata', 'tools', 'cache', 'logs')) {
        if (Test-Path -LiteralPath (Join-Path $sourceRoot "Managed\Dotnet\$prohibited")) {
            throw "The player runtime closure contains prohibited .NET content: $prohibited"
        }
    }
    foreach ($rootExecutable in Get-ChildItem -LiteralPath $sourceRoot -File -Filter '*.exe') {
        if ($rootExecutable.Name -ne 'KeireRuntime.exe') {
            throw "The player runtime closure contains an unexpected executable: $($rootExecutable.Name)"
        }
    }

    New-Item -ItemType Directory -Path $Destination | Out-Null
    foreach ($relative in $required) {
        Copy-FileIfChanged -Source (Join-Path $sourceRoot $relative) -Destination (Join-Path $Destination $relative)
    }
    $hostFxr = Get-ChildItem (Join-Path $sourceRoot 'Managed\Dotnet\host\fxr') -Force -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $coreRuntime = Get-ChildItem (Join-Path $sourceRoot 'Managed\Dotnet\shared\Microsoft.NETCore.App') -Force -Directory |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $hostFxr -or -not $coreRuntime) {
        throw 'The player runtime closure has no bundled hostfxr or CoreCLR generation.'
    }
    Assert-PlayerSupportDirectory -Path $hostFxr.FullName | Out-Null
    Assert-PlayerSupportDirectory -Path $coreRuntime.FullName | Out-Null
    foreach ($runtimeFile in @('hostpolicy.dll', 'System.Private.CoreLib.dll')) {
        Assert-RegularPlayerSupportFile -Path (Join-Path $coreRuntime.FullName $runtimeFile) | Out-Null
    }
    Copy-TreeIfChanged -Source $hostFxr.FullName `
        -Destination (Join-Path $Destination "Managed\Dotnet\host\fxr\$($hostFxr.Name)")
    Copy-TreeIfChanged -Source $coreRuntime.FullName `
        -Destination (Join-Path $Destination "Managed\Dotnet\shared\Microsoft.NETCore.App\$($coreRuntime.Name)")
    Copy-PlayerSupportLicenses -Destination (Join-Path $Destination 'Licenses') -Architecture $TargetArchitecture
    Copy-WindowsPlayerRuntimeLibraries -Destination $Destination -Architecture $TargetArchitecture
    if ($IncludeSymbols) {
        foreach ($symbol in @('KeireRuntime.pdb', 'KeireRuntime.ilk')) {
            $symbolPath = Join-Path $sourceRoot $symbol
            if (Test-Path -LiteralPath $symbolPath -PathType Leaf) {
                Copy-FileIfChanged -Source $symbolPath -Destination (Join-Path $Destination $symbol)
            }
        }
    }
    if (@(Get-ChildItem (Join-Path $Destination 'Managed\Dotnet\host\fxr') -Directory).Count -ne 1 -or
        @(Get-ChildItem (Join-Path $Destination 'Managed\Dotnet\shared\Microsoft.NETCore.App') -Directory).Count -ne 1) {
        throw 'The staged player runtime closure contains duplicate .NET ABI generations.'
    }
}

function Remove-OwnedPlayerSupportStaging {
    param([string]$Staging, [string]$Output, [switch]$ForceFailure)

    $resolvedStaging = [IO.Path]::GetFullPath($Staging)
    $resolvedOutput = [IO.Path]::GetFullPath($Output).TrimEnd('\') + '\'
    if (-not $resolvedStaging.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Split-Path -Leaf $resolvedStaging).StartsWith('.staging-', [StringComparison]::Ordinal)) {
        throw 'Refusing to remove an unowned Player Support staging directory.'
    }
    try {
        Assert-PlayerSupportDirectory -Path $resolvedStaging | Out-Null
        Assert-RegularPlayerSupportFile `
            -Path (Join-Path $resolvedStaging '.keire-player-support-operation') | Out-Null
    }
    catch {
        throw 'Refusing to remove an unowned Player Support staging directory.'
    }
    if ($ForceFailure) { throw 'Injected Player Support cleanup failure.' }
    Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
}

function Test-SafePlayerSupportCatalogSegment {
    param([string]$Value)

    return $Value -and $Value -notin @('.', '..') -and $Value.Length -le 128 -and
        $Value -cmatch '^[A-Za-z0-9._-]+$'
}

function Test-SafePlayerSupportCatalogVersion {
    param([string]$Value)

    return $Value -and $Value.Length -le 128 -and $Value -cmatch '^[A-Za-z0-9.+-]+$'
}

function Publish-PackagedPlayerSupportLayout {
    param([string]$Payload, [object]$Manifest, [string]$Output)

    $payloadRoot = (Assert-PlayerSupportDirectory -Path $Payload).FullName
    if (-not (Test-SafePlayerSupportCatalogSegment ([string]$Manifest.id)) -or
        -not (Test-SafePlayerSupportCatalogVersion ([string]$Manifest.engineVersion)) -or
        [string]$Manifest.platform -cne 'windows' -or
        [string]$Manifest.architecture -notin @('x86_64', 'arm64') -or
        [uint64]$Manifest.playerAbi -eq 0 -or [uint64]$Manifest.playerAbi -gt [uint32]::MaxValue -or
        -not [string]$Manifest.moduleFingerprint) {
        throw 'Packaged Build Support has invalid or incomplete host metadata.'
    }

    $outputRoot = [IO.Path]::GetFullPath($Output)
    New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
    Assert-PlayerSupportDirectory -Path $outputRoot | Out-Null
    $versionRoot = Join-Path $outputRoot ([string]$Manifest.engineVersion)
    New-Item -ItemType Directory -Force -Path $versionRoot | Out-Null
    Assert-PlayerSupportDirectory -Path $versionRoot | Out-Null
    $destination = Join-Path $versionRoot ([string]$Manifest.id)
    if (Test-Path -LiteralPath $destination) {
        throw "Refusing to replace an existing packaged Build Support layout: $destination"
    }

    $temporary = Join-Path $versionRoot ('.package-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $operationMarker = Join-Path $temporary '.keire-player-support-layout-operation'
    [IO.File]::WriteAllText($operationMarker, "owned`n", [Text.UTF8Encoding]::new($false))
    try {
        foreach ($entry in Get-ChildItem -LiteralPath $payloadRoot -Force -Recurse) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Packaged Build Support payload contains a redirect: $($entry.FullName)"
            }
            if ($entry.PSIsContainer) { continue }
            $relative = [IO.Path]::GetRelativePath($payloadRoot, $entry.FullName)
            Copy-FileIfChanged -Source $entry.FullName -Destination (Join-Path $temporary $relative)
        }

        $executablePaths = @{}
        foreach ($variant in @($Manifest.variants)) {
            $relative = ([IO.Path]::Combine([string]$variant.root, [string]$variant.executable) -replace '\\', '/')
            $executablePaths[$relative.ToLowerInvariant()] = $true
        }
        $files = @(
            foreach ($file in Get-ChildItem -LiteralPath $temporary -File -Force -Recurse |
                    Where-Object { $_.FullName -ne $operationMarker } |
                    Sort-Object { [IO.Path]::GetRelativePath($temporary, $_.FullName) }) {
                $relative = [IO.Path]::GetRelativePath($temporary, $file.FullName) -replace '\\', '/'
                $executable = $executablePaths.ContainsKey($relative.ToLowerInvariant()) -or
                    $file.Name -ieq 'createdump.exe'
                [ordered]@{
                    path = $relative
                    size = [uint64]$file.Length
                    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                    mode = if ($executable) { 493 } else { 420 }
                }
            }
        )
        if ($files.Count -eq 0) { throw 'Packaged Build Support payload is empty.' }

        $installedManifest = [ordered]@{
            schemaVersion = [int]$Manifest.schemaVersion
            playerAbi = [uint32]$Manifest.playerAbi
            id = [string]$Manifest.id
            engineVersion = [string]$Manifest.engineVersion
            platform = [string]$Manifest.platform
            architecture = [string]$Manifest.architecture
            moduleFingerprint = [string]$Manifest.moduleFingerprint
            sourceModules = @($Manifest.sourceModules)
            variants = @($Manifest.variants)
            files = $files
            brandingSlots = @($Manifest.brandingSlots)
        }
        [IO.File]::WriteAllText((Join-Path $temporary 'manifest.json'),
            (($installedManifest | ConvertTo-Json -Depth 8) + "`n"), [Text.UTF8Encoding]::new($false))
        Remove-Item -LiteralPath $operationMarker -Force
        Move-Item -LiteralPath $temporary -Destination $destination
    }
    catch {
        $operationError = $_
        try {
            if (Test-Path -LiteralPath $temporary) {
                $temporaryName = Split-Path -Leaf $temporary
                if (-not $temporaryName.StartsWith('.package-', [StringComparison]::Ordinal) -or
                    (Split-Path -Parent $temporary) -ne $versionRoot) {
                    throw 'Refusing to clean an unverified packaged Build Support staging directory.'
                }
                Remove-Item -LiteralPath $temporary -Recurse -Force
            }
        }
        catch {
            Write-Warning "Packaged Build Support cleanup also failed: $($_.Exception.Message)"
        }
        throw $operationError
    }
}

function Assert-ExistingPlayerSupportCatalogPackages {
    param([object[]]$Packages, [string]$Output)

    if ($Packages.Count -gt 32) { throw 'Existing Player Support catalog exceeds its package limit.' }
    $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($package in $Packages) {
        $required = @('id', 'file', 'platform', 'architecture', 'size', 'sha256')
        if ($null -eq $package -or @($required | Where-Object { $_ -notin $package.PSObject.Properties.Name }).Count) {
            throw 'Existing Player Support catalog entry is invalid or duplicated.'
        }
        $sizeTypes = @([byte], [sbyte], [uint16], [int16], [uint32], [int32], [uint64], [int64])
        $sizeIsInteger = $false
        foreach ($sizeType in $sizeTypes) {
            if ($package.size -is $sizeType) { $sizeIsInteger = $true; break }
        }
        if ($package.id -isnot [string] -or $package.file -isnot [string] -or
            $package.platform -isnot [string] -or $package.architecture -isnot [string] -or
            $package.sha256 -isnot [string] -or -not $sizeIsInteger) {
            throw 'Existing Player Support catalog entry is invalid or duplicated.'
        }
        $id = $package.id
        $file = $package.file
        $platform = $package.platform
        $architecture = $package.architecture
        $sha256 = $package.sha256
        $size = [uint64]$package.size
        if (-not (Test-SafePlayerSupportCatalogSegment $id) -or
            -not (Test-SafePlayerSupportCatalogSegment $file) -or -not $file.EndsWith('.keireplayersupport') -or
            $platform -notin @('windows', 'linux', 'macos') -or $architecture -notin @('x86_64', 'arm64') -or
            $size -eq 0 -or $size -gt 32GB -or $sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            -not $ids.Add($id) -or -not $files.Add($file)) {
            throw 'Existing Player Support catalog entry is invalid or duplicated.'
        }
        $archive = Join-Path $Output $file
        try { $archiveFile = Assert-RegularPlayerSupportFile -Path $archive } catch {
            throw 'Existing Player Support catalog references a missing or redirected archive.'
        }
        if ([uint64]$archiveFile.Length -ne $size -or
            (Get-FileHash -LiteralPath $archiveFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) {
            throw 'Existing Player Support catalog archive size or digest does not match.'
        }
    }
}

function Publish-PlayerSupportArchive {
    param([string]$Source, [string]$Output, [string]$Id, [string]$EngineVersion,
          [string]$Platform, [string]$Architecture, [switch]$ForceCatalogFailure)

    $sourceFile = Assert-RegularPlayerSupportFile -Path $Source
    New-Item -ItemType Directory -Force -Path $Output | Out-Null
    $Output = (Assert-PlayerSupportDirectory -Path $Output).FullName
    $digest = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $archiveName = "$digest.keireplayersupport"
    $archive = Join-Path $Output $archiveName
    $catalogPath = Join-Path $Output 'player-support-catalog.json'
    $lockPath = Join-Path $Output '.player-support-catalog.lock'
    $lock = $null
    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    while (-not $lock) {
        try {
            if (Test-Path -LiteralPath $lockPath) {
                Assert-RegularPlayerSupportFile -Path $lockPath | Out-Null
            }
            $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite,
                                    [IO.FileShare]::None)
            Assert-RegularPlayerSupportFile -Path $lockPath | Out-Null
        }
        catch [IO.IOException] {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Timed out waiting for the Player Support catalog lock.' }
            Start-Sleep -Milliseconds 50
        }
    }
    $temporary = "$catalogPath.$([guid]::NewGuid().ToString('N')).tmp"
    $published = $false
    try {
        if (-not (Test-SafePlayerSupportCatalogSegment $Id) -or
            -not (Test-SafePlayerSupportCatalogVersion $EngineVersion) -or
            $Platform -notin @('windows', 'linux', 'macos') -or $Architecture -notin @('x86_64', 'arm64') -or
            $sourceFile.Length -eq 0 -or $sourceFile.Length -gt 32GB) {
            throw 'New Player Support catalog entry is invalid.'
        }
        $packages = @()
        if (Test-Path -LiteralPath $catalogPath -PathType Leaf) {
            $catalogFile = Assert-RegularPlayerSupportFile -Path $catalogPath
            if ($catalogFile.Length -gt 4MB) { throw 'Existing player-support-catalog.json exceeds its size limit.' }
            $existingCatalog = Get-Content -LiteralPath $catalogFile.FullName -Raw | ConvertFrom-Json
            if ([int]$existingCatalog.schemaVersion -ne 1 -or $null -eq $existingCatalog.packages) {
                throw 'Existing player-support-catalog.json has an invalid schema.'
            }
            if ($existingCatalog.engineVersion -isnot [string] -or
                -not (Test-SafePlayerSupportCatalogVersion $existingCatalog.engineVersion) -or
                $existingCatalog.engineVersion -cne $EngineVersion) {
                throw 'Existing player-support-catalog.json targets a different engine version.'
            }
            $existingPackages = @($existingCatalog.packages)
            Assert-ExistingPlayerSupportCatalogPackages -Packages $existingPackages -Output $Output
            $packages = @($existingPackages | Where-Object { $_.id -cne $Id })
        }
        $packages += [ordered]@{ id = $Id; platform = $Platform; architecture = $Architecture;
                                file = $archiveName; size = [uint64]$sourceFile.Length; sha256 = $digest }
        $catalog = [ordered]@{ schemaVersion = 1; engineVersion = $EngineVersion;
                              packages = @($packages | Sort-Object id) }
        if ($catalog.packages.Count -gt 32 -or
            @($catalog.packages | Group-Object file | Where-Object Count -gt 1).Count -ne 0) {
            throw 'Merged Player Support catalog exceeds its limit or duplicates an archive file.'
        }
        [IO.File]::WriteAllText($temporary, (($catalog | ConvertTo-Json -Depth 6) + "`n"),
                                [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $archive -PathType Leaf) {
            $existingArchive = Assert-RegularPlayerSupportFile -Path $archive
            if ((Get-FileHash -LiteralPath $existingArchive.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne
                $digest) {
                throw 'A content-addressed Player Support archive has conflicting bytes.'
            }
            Remove-Item -LiteralPath $sourceFile.FullName -Force
        }
        else {
            Move-Item -LiteralPath $sourceFile.FullName -Destination $archive
            $published = $true
        }
        if ($ForceCatalogFailure) { throw 'Injected Player Support catalog publication failure.' }
        Move-Item -LiteralPath $temporary -Destination $catalogPath -Force
        $published = $false
        return $archive
    }
    catch {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue }
        if ($published -and (Test-Path -LiteralPath $archive)) {
            Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
        }
        throw
    }
    finally {
        $lock.Dispose()
    }
}

$BuildArchitecture = if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x86_64' }
$ManifestArchitecture = if ($Architecture -eq 'arm64') { 'arm64' } else { 'x86_64' }

if ($CleanupProbeDirectory) {
    $primaryError = $null
    try {
        if ($TestPrimaryFailure) { throw 'Injected Player Support primary failure.' }
    }
    catch {
        $primaryError = $_
    }
    $cleanupError = $null
    try {
        Remove-OwnedPlayerSupportStaging -Staging $CleanupProbeDirectory `
            -Output (Split-Path -Parent $CleanupProbeDirectory) -ForceFailure:$TestFailCleanup
    }
    catch {
        $cleanupError = $_
    }
    if ($primaryError) {
        if ($cleanupError) { Write-Warning $cleanupError.Exception.Message }
        throw $primaryError
    }
    if ($cleanupError) { throw $cleanupError }
    return
}

if ($CatalogPublishSource -or $CatalogPublishOutput -or $CatalogPublishId) {
    if (-not $CatalogPublishSource -or -not $CatalogPublishOutput -or -not $CatalogPublishId) {
        throw 'Catalog publication test parameters must be supplied together.'
    }
    $published = Publish-PlayerSupportArchive -Source $CatalogPublishSource -Output $CatalogPublishOutput `
        -Id $CatalogPublishId -EngineVersion $CatalogPublishEngineVersion -Platform $CatalogPublishPlatform `
        -Architecture $CatalogPublishArchitecture -ForceCatalogFailure:$TestFailCatalogPublish
    Write-Output $published
    return
}

if ($RuntimeClosureSource -or $RuntimeClosureDestination) {
    if (-not $RuntimeClosureSource -or -not $RuntimeClosureDestination) {
        throw 'RuntimeClosureSource and RuntimeClosureDestination must be supplied together.'
    }
    Copy-PlayerRuntimeClosure -Source $RuntimeClosureSource -Destination $RuntimeClosureDestination `
        -IncludeSymbols $true -TargetArchitecture $ManifestArchitecture
    return
}

if ($PackagedLayoutPayload -or $PackagedLayoutManifest) {
    if (-not $PackagedLayoutPayload -or -not $PackagedLayoutManifest -or -not $InstalledLayoutRoot) {
        throw 'Packaged layout test parameters and InstalledLayoutRoot must be supplied together.'
    }
    $layoutManifest = Get-Content -LiteralPath $PackagedLayoutManifest -Raw | ConvertFrom-Json
    Publish-PackagedPlayerSupportLayout -Payload $PackagedLayoutPayload -Manifest $layoutManifest `
        -Output $InstalledLayoutRoot
    return
}

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'Build\PlayerSupport'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

& (Join-Path $repositoryRoot 'Scripts\project.ps1') build -Generator ninja -Configuration Debug -Architecture x86_64 -Toolset msc -Target KeireAssetTool
if ($LASTEXITCODE -ne 0) { throw 'Could not build the host KeireAssetTool.' }
$assetTool = Join-Path $repositoryRoot 'Build\Bin\Debug-windows-x86_64\KeireAssetTool\KeireAssetTool.exe'
$metadata = (& $assetTool describe-player-support-host | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0) { throw 'Could not query player support metadata.' }

foreach ($configuration in @('Debug', 'Release', 'Dist')) {
    & (Join-Path $repositoryRoot 'Scripts\project.ps1') build -Generator ninja -Configuration $configuration -Architecture $BuildArchitecture -Toolset msc -Target KeireRuntime
    if ($LASTEXITCODE -ne 0) { throw "Could not build the $configuration $Architecture player template." }
}

$packId = "windows-$ManifestArchitecture-$($metadata.engineVersion)"
$staging = Join-Path $OutputDirectory ".staging-$([guid]::NewGuid().ToString('N'))"
$payload = Join-Path $staging 'payload'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
[IO.File]::WriteAllText((Join-Path $staging '.keire-player-support-operation'), "owned`n",
                        [Text.UTF8Encoding]::new($false))
$operationError = $null
try {
    $variants = @()
    foreach ($variant in @(@{ Build = 'Debug'; Name = 'Development' },
                            @{ Build = 'Release'; Name = 'Release' },
                            @{ Build = 'Dist'; Name = 'Dist' })) {
        $source = Join-Path $repositoryRoot "Build\Bin\$($variant.Build)-windows-$BuildArchitecture\KeireRuntime"
        if (-not (Test-Path -LiteralPath (Join-Path $source 'KeireRuntime.exe') -PathType Leaf)) {
            throw "The $($variant.Build) player template is incomplete: $source"
        }
        $destination = Join-Path $payload $variant.Name
        Copy-PlayerRuntimeClosure -Source $source -Destination $destination `
            -IncludeSymbols ($variant.Name -ne 'Dist') -TargetArchitecture $ManifestArchitecture
        $symbols = @()
        foreach ($symbol in @('KeireRuntime.pdb', 'KeireRuntime.ilk')) {
            if (Test-Path -LiteralPath (Join-Path $destination $symbol) -PathType Leaf) { $symbols += $symbol }
        }
        $configurationName = if ($variant.Name -eq 'Development') { 'development' } else { $variant.Name.ToLowerInvariant() }
        $variants += [ordered]@{
            configuration = $configurationName
            root = $variant.Name
            executable = 'KeireRuntime.exe'
            bundle = ''
            symbols = $symbols
        }
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        playerAbi = [int]$metadata.playerAbi
        id = $packId
        engineVersion = [string]$metadata.engineVersion
        platform = 'windows'
        architecture = $ManifestArchitecture
        moduleFingerprint = [string]$metadata.moduleFingerprint
        sourceModules = @($metadata.sourceModules)
        variants = $variants
        files = @()
        brandingSlots = @($variants | ForEach-Object {
            [ordered]@{
                path = "$($_.root)/$($_.executable)"
                kind = 'windows-resource-update'
                offset = 0
                size = 1
            }
        })
    }
    $manifestPath = Join-Path $staging 'manifest.json'
    [IO.File]::WriteAllText($manifestPath, (($manifest | ConvertTo-Json -Depth 8) + "`n"), [Text.UTF8Encoding]::new($false))
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $stagedArchive = Join-Path $staging "$packId.keireplayersupport"
    & $assetTool pack-player-support --catalog $manifestPath --input $payload --output $stagedArchive --compression-level 9
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the Build Support package.' }
    & $assetTool verify-player-support --input $stagedArchive
    if ($LASTEXITCODE -ne 0) { throw 'Build Support package verification failed.' }
    if ($InstalledLayoutRoot) {
        Publish-PackagedPlayerSupportLayout -Payload $payload -Manifest $manifest -Output $InstalledLayoutRoot
    }
    $archive = Publish-PlayerSupportArchive -Source $stagedArchive -Output $OutputDirectory -Id $packId `
        -EngineVersion ([string]$metadata.engineVersion) -Platform windows -Architecture $ManifestArchitecture
    Write-Host "Created $archive"
    if ($SignatureKeyId) {
        & (Join-Path $repositoryRoot 'Scripts\project.ps1') build -Generator ninja -Configuration Debug -Architecture x86_64 -Toolset msc -Target KeireHubPackagePublisher
        if ($LASTEXITCODE -ne 0) { throw 'Could not build KeireHubPackagePublisher.' }
        $publisher = Join-Path $repositoryRoot 'Build\Bin\Debug-windows-x86_64\KeireHubPackagePublisher\KeireHubPackagePublisher.exe'
        & $publisher create-build-support --player-support-package $archive --channel $Channel `
            --output (Join-Path $OutputDirectory "$packId.keirepackage") `
            --manifest-output (Join-Path $OutputDirectory "$packId.manifest.json") `
            --signature-key-id $SignatureKeyId
        if ($LASTEXITCODE -ne 0) { throw 'Could not publish the generic Build Support component package.' }
    }
}
catch {
    $operationError = $_
}
$cleanupError = $null
if (-not $KeepStaging -and (Test-Path -LiteralPath $staging)) {
    try {
        Remove-OwnedPlayerSupportStaging -Staging $staging -Output $OutputDirectory
    }
    catch {
        $cleanupError = $_
    }
}
if ($operationError) {
    if ($cleanupError) { Write-Warning $cleanupError.Exception.Message }
    throw $operationError
}
if ($cleanupError) {
    throw $cleanupError
}
