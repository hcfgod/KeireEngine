param([ValidateSet("All", "Fast", "Integration")][string]$Suite = "All")

$ErrorActionPreference = "Stop"
$started = [Diagnostics.Stopwatch]::StartNew()
$runFast = $Suite -in @("All", "Fast")
$runIntegration = $Suite -in @("All", "Integration")
$Windows = Resolve-Path (Join-Path $PSScriptRoot "..\Windows")
. (Join-Path $Windows "common.ps1")

function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -ne $Expected) { throw "$Message. Expected '$Expected', got '$Actual'." }
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "$Message failed." }
}
function Invoke-RepositoryBatchSearch([bool]$FixedStrings, [string[]]$Patterns) {
    $root = Get-RepositoryRoot
    $ripgrep = Get-Command rg -ErrorAction SilentlyContinue
    if ($ripgrep) {
        $arguments = @("-n")
        if ($FixedStrings) { $arguments += "-F" }
        $arguments += @(
            "--glob", "!.git/**", "--glob", "!.vs/**", "--glob", "!Vendor/**", "--glob", "!Tools/**",
            "--glob", "!Build/**", "--glob", "!Logs/**", "--glob", "!Artifacts/**", "--glob", "!Scripts/Tests/**"
        )
        foreach ($pattern in $Patterns) { $arguments += @("-e", $pattern) }
        $arguments += $root
        $output = & $ripgrep.Source @arguments 2>&1
    }
    else {
        $arguments = @("-C", $root, "grep", "-n")
        if ($FixedStrings) { $arguments += "-F" } else { $arguments += "-E" }
        foreach ($pattern in $Patterns) { $arguments += @("-e", $pattern) }
        $arguments += @(
            "--", ".", ":(exclude)Vendor/**", ":(exclude)Tools/**", ":(exclude)Build/**",
            ":(exclude)Logs/**", ":(exclude)Artifacts/**", ":(exclude)Scripts/Tests/**"
        )
        $output = & git @arguments 2>&1
    }
    $exitCode = $LASTEXITCODE
    $output | Write-Host
    return $exitCode
}

$project = Get-ProjectConfig
$python = Get-PythonInvocation
$storePython = Get-Command python -ErrorAction SilentlyContinue
$pythonLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($storePython -and $pythonLauncher -and $storePython.Source -like "*\Microsoft\WindowsApps\python.exe") {
    Assert-Equal ([IO.Path]::GetFileName($python.Executable)) "py.exe" "Microsoft Store Python alias rejection"
}
if ($runFast) {
$workspaceIdentityFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-workspace-identity-" +
    [guid]::NewGuid().ToString("N"))
$firstWorkspace = Join-Path $workspaceIdentityFixture "first"
$secondWorkspace = Join-Path $workspaceIdentityFixture "second"
$workspaceJunctions = [Collections.Generic.List[string]]::new()
try {
    New-Item -ItemType Directory -Force -Path $firstWorkspace, $secondWorkspace | Out-Null
    $firstIdentity = Get-KeireWorkspaceIdentity $firstWorkspace
    $firstAliasIdentity = Get-KeireWorkspaceIdentity ($firstWorkspace.ToUpperInvariant() +
        [IO.Path]::DirectorySeparatorChar)
    $secondIdentity = Get-KeireWorkspaceIdentity $secondWorkspace
    Assert-True ($firstIdentity -match '^[0-9a-f]{16}$') "Windows workspace cache identity format"
    Assert-Equal $firstAliasIdentity $firstIdentity "Windows workspace cache canonicalization"
    Assert-True ($secondIdentity -ne $firstIdentity) "Linked-worktree cache identity isolation"

    $coralDotnetRoot = Join-Path $firstWorkspace "dotnet"
    $coralVariant = Get-KeireCoralBuildVariantKey -Architecture x86_64 `
        -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
        -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-x64|10.0.10|fixture" `
        -WorkspaceIdentity $firstIdentity
    $coralVariantAlias = Get-KeireCoralBuildVariantKey -Architecture x86_64 `
        -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
        -DotnetRoot ($coralDotnetRoot.ToUpperInvariant() + [IO.Path]::DirectorySeparatorChar) `
        -NetHostIdentity "win-x64|10.0.10|fixture" -WorkspaceIdentity $firstIdentity
    Assert-True ($coralVariant -match '^windows-x86_64-[0-9a-f]{24}$') `
        "Windows Coral build variant key format"
    Assert-Equal $coralVariantAlias $coralVariant "Windows Coral SDK-root canonicalization"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture ARM64 `
                -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
                -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-arm64|10.0.10|fixture" `
                -WorkspaceIdentity $firstIdentity) -ne $coralVariant) "Windows Coral architecture isolation"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture x86_64 `
                -CompilerIdentity "msc-vs17-vc14.45-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
                -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-x64|10.0.10|fixture" `
                -WorkspaceIdentity $firstIdentity) -ne $coralVariant) "Windows Coral compiler isolation"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture x86_64 `
                -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.303" `
                -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-x64|10.0.10|fixture" `
                -WorkspaceIdentity $firstIdentity) -ne $coralVariant) "Windows Coral SDK-version isolation"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture x86_64 `
                -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
                -DotnetRoot (Join-Path $secondWorkspace "dotnet") `
                -NetHostIdentity "win-x64|10.0.10|fixture" -WorkspaceIdentity $firstIdentity) -ne $coralVariant) `
        "Windows Coral SDK-installation isolation"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture x86_64 `
                -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
                -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-x64|10.0.11|fixture" `
                -WorkspaceIdentity $firstIdentity) -ne $coralVariant) "Windows Coral nethost isolation"
    Assert-True ((Get-KeireCoralBuildVariantKey -Architecture x86_64 `
                -CompilerIdentity "msc-vs17-vc14.44-sdk10.0.28000.0" -DotnetSdkVersion "10.0.302" `
                -DotnetRoot $coralDotnetRoot -NetHostIdentity "win-x64|10.0.10|fixture" `
                -WorkspaceIdentity $secondIdentity) -ne $coralVariant) "Windows Coral worktree isolation"
    Assert-Throws {
        Get-KeireCoralBuildVariantKey -Architecture x86_64 -CompilerIdentity "compiler`nspoof" `
            -DotnetSdkVersion "10.0.302" -DotnetRoot $coralDotnetRoot `
            -NetHostIdentity "win-x64|10.0.10|fixture" -WorkspaceIdentity $firstIdentity
    } "Multiline Windows Coral compiler identity rejection"
    Assert-Throws {
        Get-KeireCoralBuildVariantKey -Architecture x86_64 -CompilerIdentity "compiler" `
            -DotnetSdkVersion "10.0-preview" -DotnetRoot $coralDotnetRoot `
            -NetHostIdentity "win-x64|10.0.10|fixture" -WorkspaceIdentity $firstIdentity
    } "Non-exact Windows Coral SDK identity rejection"

    $junctionBase = Join-Path $workspaceIdentityFixture "junctions"
    New-Item -ItemType Directory -Force $junctionBase | Out-Null
    $legacyJunction = Join-Path $junctionBase "kesc"
    New-Item -ItemType Junction -Path $legacyJunction -Target $firstWorkspace | Out-Null
    $workspaceJunctions.Add($legacyJunction)
    $firstJunction = Get-KeireWorkspaceJunctionPath -BasePath $junctionBase -Prefix "kesc" `
        -RepositoryRoot $firstWorkspace
    $secondJunction = Get-KeireWorkspaceJunctionPath -BasePath $junctionBase -Prefix "kesc" `
        -RepositoryRoot $secondWorkspace
    Assert-True ($firstJunction -ne $secondJunction) "Distinct workspace junction path selection"
    Initialize-KeireWorkspaceJunction -Path $firstJunction -Target $firstWorkspace | Out-Null
    Initialize-KeireWorkspaceJunction -Path $secondJunction -Target $secondWorkspace | Out-Null
    $workspaceJunctions.Add($firstJunction)
    $workspaceJunctions.Add($secondJunction)
    Assert-Equal ([string]((Get-Item -LiteralPath $legacyJunction -Force).Target | Select-Object -First 1)) `
        $firstWorkspace "Legacy unsuffixed junction preservation"

    $wrongTypeJunction = Get-KeireWorkspaceJunctionPath -BasePath $junctionBase -Prefix "wrong-type" `
        -RepositoryRoot $firstWorkspace
    New-Item -ItemType File -Path $wrongTypeJunction | Out-Null
    Assert-Throws {
        Initialize-KeireWorkspaceJunction -Path $wrongTypeJunction -Target $firstWorkspace
    } "Wrong-type workspace junction rejection"
    Assert-Throws {
        Initialize-KeireWorkspaceJunction -Path $firstJunction -Target $secondWorkspace
    } "Workspace identity collision target rejection"
    Assert-Throws {
        Get-KeireWorkspaceJunctionPath -BasePath $junctionBase -Prefix "..\escape" `
            -RepositoryRoot $firstWorkspace
    } "Unsafe workspace junction prefix rejection"
}
finally {
    foreach ($junction in $workspaceJunctions) {
        if (Test-Path -LiteralPath $junction) { [IO.Directory]::Delete($junction, $false) }
    }
    Remove-Item -LiteralPath $workspaceIdentityFixture -Recurse -Force -ErrorAction SilentlyContinue
}
$lockedSourceFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-locked-source-" +
    [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force $lockedSourceFixture | Out-Null
    & git -C $lockedSourceFixture init --quiet
    Set-Content -LiteralPath (Join-Path $lockedSourceFixture "source.txt") -Encoding UTF8 -Value "locked"
    & git -C $lockedSourceFixture add source.txt
    & git -C $lockedSourceFixture -c user.name=fixture -c user.email=fixture@example.invalid commit --quiet -m fixture
    if ($LASTEXITCODE -ne 0) { throw "Could not prepare locked-source fixture." }
    $lockedSourceCommit = ([string](& git -C $lockedSourceFixture rev-parse HEAD)).Trim()
    Assert-KeireLockedGitSource -Path $lockedSourceFixture -ExpectedCommit $lockedSourceCommit -Name "fixture"
    Set-Content -LiteralPath (Join-Path $lockedSourceFixture "untracked.txt") -Encoding UTF8 -Value "dirty"
    Assert-Throws {
        Assert-KeireLockedGitSource -Path $lockedSourceFixture -ExpectedCommit $lockedSourceCommit -Name "fixture"
    } "Untracked locked-source cache rejection"
    Remove-Item -LiteralPath (Join-Path $lockedSourceFixture "untracked.txt")
    Set-Content -LiteralPath (Join-Path $lockedSourceFixture "source.txt") -Encoding UTF8 -Value "modified"
    Assert-Throws {
        Assert-KeireLockedGitSource -Path $lockedSourceFixture -ExpectedCommit $lockedSourceCommit -Name "fixture"
    } "Modified locked-source cache rejection"
}
finally {
    Remove-Item -LiteralPath $lockedSourceFixture -Recurse -Force -ErrorAction SilentlyContinue
}
$powerShellExecutable = (Get-Process -Id $PID).Path
$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$ffmpegToolsetError = & $powerShellExecutable -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $Windows "ffmpeg.ps1") -Configuration Debug -Architecture x86_64 -Toolset gcc 2>&1
$ffmpegToolsetStatus = $LASTEXITCODE
$ErrorActionPreference = $savedErrorActionPreference
Assert-True ($ffmpegToolsetStatus -ne 0) "Non-MSVC Windows FFmpeg producer rejection status"
Assert-True (($ffmpegToolsetError | Out-String).Contains("do not support the gcc toolset")) `
    "GNU Windows FFmpeg consumer rejection diagnostic"
$binaryOutputFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-binary-output-" + [guid]::NewGuid().ToString("N"))
$binaryOutputExternal = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-binary-output-external-" + [guid]::NewGuid().ToString("N"))
$binaryOutputJunction = Join-Path $binaryOutputFixture "Build\Bin\Debug-windows-x86_64"
$intermediateOutput = Join-Path $binaryOutputFixture "Build\Intermediates\Debug-windows-x86_64-msc"
$otherIntermediateOutput = Join-Path $binaryOutputFixture "Build\Intermediates\Debug-windows-AARCH64-msc"
$toolchainIdentity = "msc-vs17.0-vc14.44.35207-sdk10.0.28000.0"
$expectedBinaryIdentity = "ninja|x86_64|msc|off|False|$toolchainIdentity|fingerprint"
try {
    New-Item -ItemType Directory -Force -Path `
        $binaryOutputJunction, `
        (Join-Path $binaryOutputFixture "Build\Bin\Release-windows-x86_64"), `
        (Join-Path $binaryOutputFixture "Build\Bin\Debug-windows-AARCH64"), `
        $intermediateOutput, `
        $otherIntermediateOutput, `
        $binaryOutputExternal | Out-Null
    New-Item -ItemType File -Force -Path `
        (Join-Path $binaryOutputJunction "sentinel"), `
        (Join-Path $binaryOutputFixture "Build\Bin\Release-windows-x86_64\sentinel"), `
        (Join-Path $binaryOutputFixture "Build\Bin\Debug-windows-AARCH64\sentinel"), `
        (Join-Path $intermediateOutput "sentinel"), `
        (Join-Path $otherIntermediateOutput "sentinel"), `
        (Join-Path $binaryOutputExternal "sentinel") | Out-Null
    $binaryIdentityStamp = Join-Path $binaryOutputFixture "generation.stamp"
    Set-Content -LiteralPath $binaryIdentityStamp -Encoding ASCII `
        -Value "ninja|x86_64|msc|off|False|$toolchainIdentity|fingerprint"
    Remove-IncompatibleBuildBinaries -Root $binaryOutputFixture -Architecture x86_64 -Toolset msc `
        -ToolchainIdentity $toolchainIdentity -ExpectedIdentity $expectedBinaryIdentity `
        -IdentityStamp $binaryIdentityStamp
    Assert-True (Test-Path -LiteralPath (Join-Path $binaryOutputJunction "sentinel") -PathType Leaf) `
        "Same-toolchain Windows binary preservation"
    Assert-True (Test-Path -LiteralPath (Join-Path $intermediateOutput "sentinel") -PathType Leaf) `
        "Same-toolchain Windows intermediate preservation"
    Set-Content -LiteralPath $binaryIdentityStamp -Encoding ASCII `
        -Value "ninja|x86_64|msc|off|False|msc-vs18.0-vc14.51.0-sdk10.0.28000.0|fingerprint"
    Remove-IncompatibleBuildBinaries -Root $binaryOutputFixture -Architecture x86_64 -Toolset msc `
        -ToolchainIdentity $toolchainIdentity -ExpectedIdentity $expectedBinaryIdentity `
        -IdentityStamp $binaryIdentityStamp
    Assert-True (-not (Test-Path -LiteralPath $binaryOutputJunction)) `
        "Changed-toolchain Windows binary invalidation"
    Assert-True (-not (Test-Path -LiteralPath $intermediateOutput)) `
        "Changed-toolchain Windows intermediate invalidation"
    Assert-True (-not (Test-Path -LiteralPath `
        (Join-Path $binaryOutputFixture "Build\Bin\Release-windows-x86_64"))) `
        "All-configuration Windows binary invalidation"
    Assert-True (Test-Path -LiteralPath `
        (Join-Path $binaryOutputFixture "Build\Bin\Debug-windows-AARCH64\sentinel") -PathType Leaf) `
        "Other-architecture Windows binary preservation"
    Assert-True (Test-Path -LiteralPath (Join-Path $otherIntermediateOutput "sentinel") -PathType Leaf) `
        "Other-architecture Windows intermediate preservation"
    Assert-True (Test-Path -LiteralPath (Join-Path $binaryOutputExternal "sentinel") -PathType Leaf) `
        "Outside Windows binary sentinel preservation"

    New-Item -ItemType Directory -Force -Path $binaryOutputJunction | Out-Null
    Remove-Item -LiteralPath $binaryIdentityStamp -Force
    Remove-IncompatibleBuildBinaries -Root $binaryOutputFixture -Architecture x86_64 -Toolset msc `
        -ToolchainIdentity $toolchainIdentity -ExpectedIdentity $expectedBinaryIdentity `
        -IdentityStamp $binaryIdentityStamp
    Assert-True (-not (Test-Path -LiteralPath $binaryOutputJunction)) `
        "Unknown-provenance Windows binary invalidation"

    New-Item -ItemType Junction -Path $binaryOutputJunction -Target $binaryOutputExternal | Out-Null
    Set-Content -LiteralPath $binaryIdentityStamp -Encoding ASCII `
        -Value "ninja|x86_64|clang|off|False|clang-fixture|fingerprint"
    Assert-Throws {
        Remove-IncompatibleBuildBinaries -Root $binaryOutputFixture -Architecture x86_64 -Toolset msc `
            -ToolchainIdentity $toolchainIdentity -ExpectedIdentity $expectedBinaryIdentity `
            -IdentityStamp $binaryIdentityStamp
    } "Reparse-point Windows binary output rejection"
    Assert-True (Test-Path -LiteralPath (Join-Path $binaryOutputExternal "sentinel") -PathType Leaf) `
        "Reparse target sentinel preservation"
}
finally {
    $junction = Get-Item -LiteralPath $binaryOutputJunction -Force -ErrorAction SilentlyContinue
    if ($junction -and (($junction.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        [IO.Directory]::Delete($binaryOutputJunction)
    }
    Remove-Item -LiteralPath $binaryOutputFixture, $binaryOutputExternal -Recurse -Force -ErrorAction SilentlyContinue
}
$cacheRemovalFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-cache-removal-" +
    [guid]::NewGuid().ToString("N"))
$cacheRemovalExternal = Join-Path ([IO.Path]::GetTempPath()) ("keire-cache-removal-external-" +
    [guid]::NewGuid().ToString("N"))
$cacheRemovalAllowed = Join-Path $cacheRemovalFixture "Build\Tools\ShaderCompiler\Cache"
$cacheRemovalJunction = Join-Path $cacheRemovalAllowed "windows-x86_64-msc"
$cacheRemovalNestedJunction = Join-Path $cacheRemovalAllowed "nested\external"
try {
    New-Item -ItemType Directory -Force -Path $cacheRemovalAllowed, $cacheRemovalExternal | Out-Null
    New-Item -ItemType File -Path (Join-Path $cacheRemovalExternal "sentinel") | Out-Null
    New-Item -ItemType Junction -Path $cacheRemovalJunction -Target $cacheRemovalExternal | Out-Null
    Assert-Throws {
        Remove-KeireGeneratedDirectory -RepositoryRoot $cacheRemovalFixture -AllowedRoot $cacheRemovalAllowed `
            -Path $cacheRemovalJunction -Description "shader compiler cache"
    } "Reparse-point shader cache removal rejection"
    Assert-True (Test-Path -LiteralPath (Join-Path $cacheRemovalExternal "sentinel") -PathType Leaf) `
        "Shader cache reparse target preservation"
    [IO.Directory]::Delete($cacheRemovalJunction, $false)

    New-Item -ItemType Directory -Force -Path (Split-Path $cacheRemovalNestedJunction) | Out-Null
    New-Item -ItemType Junction -Path $cacheRemovalNestedJunction -Target $cacheRemovalExternal | Out-Null
    Assert-Throws {
        Remove-KeireGeneratedDirectory -RepositoryRoot $cacheRemovalFixture -AllowedRoot $cacheRemovalAllowed `
            -Path (Split-Path $cacheRemovalNestedJunction) -Description "shader compiler cache"
    } "Nested reparse-point shader cache removal rejection"
    Assert-True (Test-Path -LiteralPath (Join-Path $cacheRemovalExternal "sentinel") -PathType Leaf) `
        "Nested shader cache reparse target preservation"

    $cacheRemovalFile = Join-Path $cacheRemovalAllowed "wrong-type"
    New-Item -ItemType File -Path $cacheRemovalFile | Out-Null
    Assert-Throws {
        Remove-KeireGeneratedDirectory -RepositoryRoot $cacheRemovalFixture -AllowedRoot $cacheRemovalAllowed `
            -Path $cacheRemovalFile -Description "shader compiler cache"
    } "Wrong-type shader cache removal rejection"
    $cacheRemovalDirectory = Join-Path $cacheRemovalAllowed "ordinary"
    New-Item -ItemType Directory -Path $cacheRemovalDirectory | Out-Null
    Remove-KeireGeneratedDirectory -RepositoryRoot $cacheRemovalFixture -AllowedRoot $cacheRemovalAllowed `
        -Path $cacheRemovalDirectory -Description "shader compiler cache"
    Assert-True (-not (Test-Path -LiteralPath $cacheRemovalDirectory)) "Ordinary shader cache removal"
}
finally {
    foreach ($junctionPath in @($cacheRemovalJunction, $cacheRemovalNestedJunction)) {
        $junction = Get-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
        if ($junction -and (($junction.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            [IO.Directory]::Delete($junctionPath, $false)
        }
    }
    Remove-Item -LiteralPath $cacheRemovalFixture, $cacheRemovalExternal -Recurse -Force `
        -ErrorAction SilentlyContinue
}
$workspaceLockFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-workspace-lock-" + [guid]::NewGuid().ToString("N"))
$savedWorkspaceLockToken = $env:KEIRE_WORKSPACE_LOCK_TOKEN
$savedWorkspaceLockTimeout = $env:KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS
$savedWorkspaceLockStale = $env:KEIRE_WORKSPACE_LOCK_STALE_SECONDS
$savedWorkspaceLockHeartbeat = $env:KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS
$workspaceLock = $null
$workspaceLockParentJunction = Join-Path $workspaceLockFixture ".locks"
$workspaceLockExternal = Join-Path ([IO.Path]::GetTempPath()) ("keire-workspace-lock-external-" +
    [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force $workspaceLockFixture | Out-Null
    $env:KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS = "1"
    $env:KEIRE_WORKSPACE_LOCK_STALE_SECONDS = "10"
    $env:KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS = "1"
    $workspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "first"
    Assert-True $workspaceLock.Acquired "Initial Windows workspace lock acquisition"
    $owner = Get-KeireWorkspaceLockOwner -LockPath $workspaceLock.Path
    Assert-Equal $owner.platform "windows" "Windows workspace lock protocol metadata"
    $nestedLock = Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "nested"
    Assert-True (-not $nestedLock.Acquired) "Inherited Windows workspace lock reentry"
    Exit-KeireWorkspaceLock -Lock $nestedLock

    $activeToken = $env:KEIRE_WORKSPACE_LOCK_TOKEN
    $env:KEIRE_WORKSPACE_LOCK_TOKEN = $null
    Assert-Throws {
        Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "contender"
    } "Concurrent Windows workspace lock timeout"
    $env:KEIRE_WORKSPACE_LOCK_TOKEN = $activeToken
    Exit-KeireWorkspaceLock -Lock $workspaceLock
    $workspaceLock = $null

    $stalePath = Join-Path $workspaceLockFixture "Tools\.locks\project-command.lock"
    New-Item -ItemType Directory -Force $stalePath | Out-Null
    [IO.File]::WriteAllText((Join-Path $stalePath "owner"),
        "token=expired`nplatform=unix`npid=123`nhost=fixture`ncommand=test`nstarted=expired`n",
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $stalePath "heartbeat"), "", [Text.UTF8Encoding]::new($false))
    [IO.File]::SetLastWriteTimeUtc((Join-Path $stalePath "heartbeat"), [DateTime]::UtcNow.AddSeconds(-20))
    $workspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "recovery"
    Assert-True $workspaceLock.Acquired "Expired cross-platform workspace lock recovery"
    Exit-KeireWorkspaceLock -Lock $workspaceLock
    $workspaceLock = $null
    Assert-True (-not (Test-Path -LiteralPath $stalePath)) "Windows workspace lock release"

    $workspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture `
        -CommandName "dependency-source-fixture" -LockRelativePath ".locks\dependency-fixture.lock"
    Assert-Equal $workspaceLock.Path (Join-Path $workspaceLockFixture ".locks\dependency-fixture.lock") `
        "Windows shared-cache lock path"
    $activeToken = $env:KEIRE_WORKSPACE_LOCK_TOKEN
    $env:KEIRE_WORKSPACE_LOCK_TOKEN = $null
    Assert-Throws {
        Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture `
            -CommandName "dependency-source-contender" -LockRelativePath ".locks\dependency-fixture.lock"
    } "Concurrent Windows shared-cache lock timeout"
    $env:KEIRE_WORKSPACE_LOCK_TOKEN = $activeToken
    Exit-KeireWorkspaceLock -Lock $workspaceLock
    $workspaceLock = $null
    [IO.Directory]::Delete($workspaceLockParentJunction, $false)
    New-Item -ItemType Directory -Path $workspaceLockExternal | Out-Null
    New-Item -ItemType File -Path (Join-Path $workspaceLockExternal "sentinel") | Out-Null
    New-Item -ItemType Junction -Path $workspaceLockParentJunction -Target $workspaceLockExternal | Out-Null
    Assert-Throws {
        Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "redirected" `
            -LockRelativePath ".locks\redirected.lock"
    } "Reparse-point Windows shared-cache lock parent rejection"
    Assert-True (Test-Path -LiteralPath (Join-Path $workspaceLockExternal "sentinel") -PathType Leaf) `
        "Windows shared-cache lock reparse target preservation"
    [IO.Directory]::Delete($workspaceLockParentJunction, $false)
    Assert-Throws {
        Enter-KeireWorkspaceLock -RepositoryRoot $workspaceLockFixture -CommandName "escape" `
            -LockRelativePath "..\outside.lock"
    } "Escaping Windows shared-cache lock path rejection"
}
finally {
    if ($workspaceLock) { Exit-KeireWorkspaceLock -Lock $workspaceLock }
    $workspaceLockParent = Get-Item -LiteralPath $workspaceLockParentJunction -Force -ErrorAction SilentlyContinue
    if ($workspaceLockParent -and
        (($workspaceLockParent.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        [IO.Directory]::Delete($workspaceLockParentJunction, $false)
    }
    $env:KEIRE_WORKSPACE_LOCK_TOKEN = $savedWorkspaceLockToken
    $env:KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS = $savedWorkspaceLockTimeout
    $env:KEIRE_WORKSPACE_LOCK_STALE_SECONDS = $savedWorkspaceLockStale
    $env:KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS = $savedWorkspaceLockHeartbeat
    Remove-Item $workspaceLockFixture -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $workspaceLockExternal -Recurse -Force -ErrorAction SilentlyContinue
}
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "check-repository-layout.py")
if ($LASTEXITCODE -ne 0) { throw "Repository layout checks failed." }
& (Join-Path $PSScriptRoot "test-generated-content-cache-windows.ps1")
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "..\Packaging\sync-sandbox-template.py") --check
if ($LASTEXITCODE -ne 0) { throw "Sandbox template synchronization checks failed." }
& (Join-Path $PSScriptRoot "test-clean-windows.ps1")
& (Join-Path $PSScriptRoot "test-managed-host-staging-windows.ps1")
$installerTransactionGates = @(
    "test-installer-windows.ps1",
    "test-hub-installer-windows.ps1"
)
$installerPrerequisite = Join-Path $PSScriptRoot "prepare-install-worker-runtime-windows.ps1"
$installerPrerequisiteSource = Get-Content -LiteralPath $installerPrerequisite -Raw
foreach ($contract in @('Scripts\Windows', 'build.ps1', '-Generator ninja', '-Configuration $configuration',
        '-Architecture $architecture', '-Toolset msc', 'InstallWorker', 'InstallVerifyFixture',
        'Test-Path -LiteralPath $executable -PathType Leaf')) {
    Assert-True ($installerPrerequisiteSource.Contains($contract)) `
        "Installer runtime prerequisite provisioning retains '$contract'"
}
foreach ($gate in $installerTransactionGates) {
    $gatePath = Join-Path $PSScriptRoot $gate
    $gateSource = Get-Content -LiteralPath $gatePath -Raw
    Assert-True ($gateSource.Contains('prepare-install-worker-runtime-windows.ps1') -and
                 $gateSource.Contains('test-install-worker-runtime-windows.ps1') -and
                 $gateSource.Contains('test-nsis-worker-runtime-windows.ps1')) `
        "$gate provisions and runs the worker and actual-NSIS transaction matrices"
    & $gatePath
}
$distributionPackageGates = @(
    "test-editor-package-windows.ps1",
    "test-hub-package-windows.ps1"
)
foreach ($gate in $distributionPackageGates) {
    $gatePath = Join-Path $PSScriptRoot $gate
    $gateSource = Get-Content -LiteralPath $gatePath -Raw
    Assert-True ($gateSource.Contains('--verify-installation')) `
        "$gate exercises the real Dist executable verification contract"
    & $gatePath
}
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-prepare-distribution-snapshot.py")
if ($LASTEXITCODE -ne 0) { throw "Distribution snapshot preparation checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-website.py")
if ($LASTEXITCODE -ne 0) { throw "Website checks failed." }
$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
    & $node.Source (Join-Path $PSScriptRoot "test-website-downloads.mjs")
    if ($LASTEXITCODE -ne 0) { throw "Website download catalog checks failed." }
    & $node.Source (Join-Path $PSScriptRoot "test-website-contact.mjs")
    if ($LASTEXITCODE -ne 0) { throw "Website contact form checks failed." }
    & $node.Source (Join-Path $PSScriptRoot "test-website-contact-function.mjs")
    if ($LASTEXITCODE -ne 0) { throw "Website contact Edge Function checks failed." }
    & $node.Source (Join-Path $PSScriptRoot "test-website-docs.mjs")
    if ($LASTEXITCODE -ne 0) { throw "Website documentation source checks failed." }
}
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-supabase-config.py")
if ($LASTEXITCODE -ne 0) { throw "Supabase desktop configuration checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-marketplace-migrations.py")
if ($LASTEXITCODE -ne 0) { throw "Marketplace migration security-contract checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-marketplace-edge.py")
if ($LASTEXITCODE -ne 0) { throw "Marketplace Edge trust-boundary checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-marketplace-upload-sample.py")
if ($LASTEXITCODE -ne 0) { throw "Marketplace package-fixture checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-patch-ninja-depfiles.py")
if ($LASTEXITCODE -ne 0) { throw "Ninja dependency-file and PCH-path checks failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path $PSScriptRoot "test-ninja-compiler-cache.py")
if ($LASTEXITCODE -ne 0) { throw "Ninja compiler-cache checks failed." }
& (Join-Path $PSScriptRoot "test-distribution-service-package-windows.ps1")
& (Join-Path $PSScriptRoot "test-rclone-distribution-backup-windows.ps1")
$generateScript = Get-Content (Join-Path $Windows "generate.ps1") -Raw
Assert-True ($generateScript.Contains('--file=premake5.lua')) "Unicode-safe relative Premake script path"
Assert-True ($generateScript.Contains('Get-ProjectGenerationFingerprint')) "Source-inventory project regeneration"
Assert-True ($generateScript.Contains('"dependencies.ps1"') -and
             -not $generateScript.Contains('-Toolset $Toolset -Force:$Force')) `
    "Forced project generation preserves third-party dependency caches"
Assert-True ($generateScript.Contains('$Generator -eq "compilecommands"') -and
             $generateScript.Contains('(Join-Path $stampDirectory "ninja.stamp")') -and
             $generateScript.Contains('"ninja|$Architecture|$Toolset|$CompilerCache|$([bool]$CI)|$toolchainIdentity|$generationFingerprint"')) `
    "Compile database generation records the shared Ninja artifact identity"
Assert-True ($generateScript.Contains('$identityGenerator = if ($Generator -eq "compilecommands") { "ninja" }') -and
             $generateScript.Contains('Get-WindowsToolchainIdentity') -and
             $generateScript.Contains('windows-$(Get-ArchitectureOutputName $Architecture)-output.stamp') -and
             $generateScript.Contains('Remove-IncompatibleBuildBinaries') -and
             $generateScript.Contains('-ToolchainIdentity $toolchainIdentity') -and
             $generateScript.Contains('-ExpectedIdentity $expectedOutputIdentity') -and
             $generateScript.Contains('-IdentityStamp $outputIdentityStamp')) `
    "Windows generation invalidates stable outputs with incompatible compiler provenance"
$windowsCommonSource = Get-Content (Join-Path $Windows "common.ps1") -Raw
Assert-True ($windowsCommonSource.Contains('$generationInfrastructureInputs') -and
             $windowsCommonSource.Contains('Scripts\Unix\dependencies.sh') -and
             $windowsCommonSource.Contains('Scripts\Dependencies')) "Dependency-infrastructure project regeneration"
Assert-True ($windowsCommonSource.Contains('$_.Name -notin @(".git", "Build", "Vendor", "Tools")') -and
             $windowsCommonSource.Contains('Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter "premake5.lua"')) `
    "Windows Premake inventory skips non-source roots before recursive discovery"
Assert-True ($windowsCommonSource.Contains('function Remove-IncompatibleBuildBinaries') -and
             $windowsCommonSource.Contains('function Get-WindowsToolchainIdentity') -and
             $windowsCommonSource.Contains('[IO.FileAttributes]::ReparsePoint') -and
             $windowsCommonSource.Contains('Build\Bin\$_-windows-$outputArchitecture') -and
             $windowsCommonSource.Contains('$intermediateBase = Join-Path $Root "Build\Intermediates"') -and
             $windowsCommonSource.Contains('Join-Path $intermediateBase "$_-windows-$outputArchitecture-$Toolset"') -and
             $windowsCommonSource.Contains('$expectedParts.Count -ne 7')) `
    "Windows toolchain-output invalidation is contained, complete, and rejects reparse points"
$windowsCommon = Get-Content (Join-Path $Windows "common.ps1") -Raw
Assert-True ($windowsCommon.Contains('"KeireHubRuntime"') -and $windowsCommon.Contains('"KeireHubTests"') -and
             $windowsCommon.Contains('"KeireHubWorker"')) "Hub target source-inventory project regeneration"
Assert-True ($windowsCommon.Contains('Get-ChildItem -LiteralPath (Join-Path $Root "Scripts\Premake")') -and
             $windowsCommon.Contains('-Filter "*.lua"')) "Premake policy content project regeneration"
$bootstrapScript = Get-Content (Join-Path $Windows "bootstrap.ps1") -Raw
Assert-True ($bootstrapScript.Contains('GetTempPath') -and $bootstrapScript.Contains('$PremakeExe --version')) "Unicode-safe Premake version validation"
$macBootstrapScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Mac\bootstrap.sh") -Raw
$macGenerateScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Mac\generate.sh") -Raw
$unixCommonScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Unix\common.sh") -Raw
Assert-True ($unixCommonScript.Contains('stage_unix_asset_worker_runtime()') -and
             $unixCommonScript.Contains('copy-files-if-changed.sh" "$source_directory" "$destination_directory"')) `
    "Unix builds restore the isolated asset worker FFmpeg runtime closure"
Assert-True ($macBootstrapScript.Contains('run_homebrew_installer "$CI" "$script"') -and
             -not $macBootstrapScript.Contains('NONINTERACTIVE=1 /bin/bash "$script"') -and
             $macBootstrapScript.Contains('brew_install pkg-config pkgconf') -and
             $macBootstrapScript.Contains('check_version pkg-config "$(pkg-config --version)" 0.29.2') -and
             $macBootstrapScript.Contains('brew_install rg ripgrep') -and
             $macBootstrapScript.Contains('brew_install python3 python') -and
             $macBootstrapScript.Contains('Refusing to use a symbolic PyYAML installation') -and
             $macBootstrapScript.Contains('install_dotnet_sdk()') -and
             $macBootstrapScript.Contains('install_pyyaml()') -and
             $macBootstrapScript.Contains('PYYAML_SOURCE_SHA256') -and
             $macBootstrapScript.Contains('python_packages_link="$ROOT/Tools/Mac/python-packages"') -and
             $macBootstrapScript.Contains('DOTNET_MACOS_X86_64_SHA512') -and
             $macBootstrapScript.Contains('DOTNET_MACOS_ARM64_SHA512') -and
             $macBootstrapScript.Contains('shasum -a 512 "$archive"') -and
             ([regex]::Matches(
                 $macBootstrapScript,
                 [regex]::Escape('dotnet_sdk_listing_matches_installation "$listing" "$DOTNET_SDK_VERSION"')).Count -eq 2) -and
             $macBootstrapScript.Contains('[[ ! -L "$cache_root" ]]') -and
             ([regex]::Matches(
                 $macBootstrapScript,
                 [regex]::Escape('[[ ! -e "$dotnet_link" || -L "$dotnet_link" ]]')).Count -eq 2) -and
             $macBootstrapScript.Contains('ln -sfn "$install_root/dotnet" "$dotnet_link"') -and
             $macBootstrapScript.Contains('brew_install nasm nasm') -and
             $macBootstrapScript.Contains('check_version NASM "$(nasm -v | extract_version)" 2.14') -and
             $unixCommonScript.Contains('run_homebrew_installer()') -and
             $unixCommonScript.Contains('dotnet_sdk_listing_matches_installation()') -and
             $unixCommonScript.Contains('resolved_reported_sdk="$(cd -P "$reported_sdk" && pwd -P)"') -and
             $unixCommonScript.Contains('[[ -t 0 ]]') -and
             $unixCommonScript.Contains('NONINTERACTIVE=1 /bin/bash "$installer"')) `
    "macOS bootstrap handles authorization and provides pinned SDK, Python, pkg-config, and NASM inputs"
$linuxBootstrapScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Linux\bootstrap.sh") -Raw
Assert-True $linuxBootstrapScript.Contains('ensure_command rg ripgrep') `
    "Linux bootstrap provides ripgrep for repository identity validation"
Assert-True $macGenerateScript.Contains('[[ $CI -eq 1 ]] && bootstrap+=(--ci)') `
    "macOS generation forwards CI mode to nested bootstrap"
Assert-True ($macBootstrapScript.Contains('export PATH="$ROOT/Tools/Mac:$PATH"') -and
             $macGenerateScript.Contains('export PATH="$ROOT/Tools/Mac:$PATH"')) `
    "macOS bootstrap and generation expose the pinned .NET SDK launcher"
Assert-True ($macBootstrapScript.Contains('probe_cxx20_thread_library clang++') -and
             $unixCommonScript.Contains('probe_cxx20_thread_library()') -and
             $unixCommonScript.Contains('"$compiler" -std=c++20 -pthread') -and
             $unixCommonScript.Contains('std::stop_token and std::jthread') -and
             $unixCommonScript.Contains('sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer') -and
             $unixCommonScript.Contains('Do not enable _LIBCPP_ENABLE_EXPERIMENTAL')) `
    "macOS bootstrap rejects standard libraries without production C++20 stoppable threads"
$dependencyScript = Get-Content (Join-Path $Windows "dependencies.ps1") -Raw
Assert-True ($dependencyScript.Contains('$Toolset -eq "msc"') -and
             $dependencyScript.Contains('AssimpZlibDebugLibrary = "$debugInstall/lib/$zlibDebugName"') -and
             $dependencyScript.Contains('$sodiumToolsetVersion')) `
    "Toolset-aware Assimp zlib and deterministic libsodium caches"
$windowsBuildScript = Get-Content (Join-Path $Windows "build.ps1") -Raw
Assert-True ($windowsBuildScript.Contains('windows-$(Get-ArchitectureOutputName $Architecture)-output.stamp') -and
             $windowsBuildScript.Contains('function Initialize-KeireGeneratedBuild') -and
             $windowsBuildScript.Contains('Remove-IncompatibleBuildBinaries') -and
             $windowsBuildScript.Contains('-ToolchainIdentity $toolchainIdentity') -and
             $windowsBuildScript.Contains('-ExpectedIdentity $expectedStamp') -and
             $windowsBuildScript.Contains('-IdentityStamp $outputIdentityStamp')) `
    "Windows builds enforce canonical shared-output compiler provenance"
$buildOrderFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-build-order-" +
    [guid]::NewGuid().ToString("N"))
$savedBuildOrderPath = $env:PATH
try {
    $fixtureWindows = Join-Path $buildOrderFixture "Scripts\Windows"
    $fixtureTools = Join-Path $buildOrderFixture "tools"
    New-Item -ItemType Directory -Force -Path $fixtureWindows, $fixtureTools | Out-Null
    Copy-Item (Join-Path $Windows "build.ps1") (Join-Path $fixtureWindows "build.ps1")
    @'
$ErrorActionPreference = "Stop"
function Get-ProjectConfig {
    return [pscustomobject]@{
        PROJECT_IDENTIFIER = "BuildOrderFixture"
        PROJECT_NAMESPACE = "Fixture"
        CLIENT_TARGET = "FixtureClient"
        HUB_TARGET = "FixtureHub"
    }
}
function Get-NativeArchitecture { return "x86_64" }
function Normalize-Architecture([string]$Architecture) { return "x86_64" }
function Resolve-WindowsToolset { return "msc" }
function Resolve-CompilerCache { return "off" }
function Get-ArchitectureOutputName { return "x86_64" }
function Assert-SupportedBuildCombination {}
function Get-ProjectGenerationFingerprint { return "fixture-fingerprint" }
function Get-WindowsToolchainIdentity {
    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    if (-not (Test-Path -LiteralPath (Join-Path $root "bootstrap.ready") -PathType Leaf)) {
        throw "Toolchain identity was queried before bootstrap generation."
    }
    return "fixture-toolchain"
}
function Remove-IncompatibleBuildBinaries {
    param($Root, $Architecture, $Toolset, $ToolchainIdentity, $ExpectedIdentity, $IdentityStamp)
    if (-not (Test-Path -LiteralPath (Join-Path $Root "bootstrap.ready") -PathType Leaf) -or
        $ToolchainIdentity -ne "fixture-toolchain" -or
        $ExpectedIdentity -ne
            "ninja|x86_64|msc|off|False|fixture-toolchain|fixture-fingerprint") {
        throw "Output provenance was evaluated before the generated toolchain became ready."
    }
    Set-Content -LiteralPath (Join-Path $Root "provenance.checked") -Encoding ASCII -Value "ready"
}
function Enter-WindowsToolEnvironment {
    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    if (-not (Test-Path -LiteralPath (Join-Path $root "bootstrap.ready") -PathType Leaf)) {
        throw "Build environment was entered before bootstrap generation."
    }
    return "msc"
}
function Get-ManagedHostStagingTargets { return @() }
'@ | Set-Content (Join-Path $fixtureWindows "common.ps1") -Encoding UTF8
    @'
function Enter-GeneratedContentLock {
    return [Threading.Mutex]::new($false)
}
function Exit-GeneratedContentLock {
    param([Threading.Mutex]$Mutex)
    $Mutex.Dispose()
}
'@ | Set-Content (Join-Path $fixtureWindows "generated-content-cache.ps1") -Encoding UTF8
    @'
param(
    [string]$Generator,
    [string]$Architecture,
    [string]$Toolset,
    [string]$CompilerCache,
    [switch]$CI,
    [switch]$Update
)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
New-Item -ItemType Directory -Force -Path (Join-Path $root "Build\Generated") | Out-Null
Set-Content -LiteralPath (Join-Path $root "bootstrap.ready") -Encoding ASCII -Value "ready"
Set-Content -LiteralPath (Join-Path $root "build.ninja") -Encoding ASCII -Value "# fixture"
Set-Content -LiteralPath (Join-Path $root "Build\Generated\ninja.stamp") -Encoding ASCII `
    -Value "ninja|x86_64|msc|off|False|fixture-toolchain|fixture-fingerprint"
'@ | Set-Content (Join-Path $fixtureWindows "generate.ps1") -Encoding UTF8
    "@echo off`r`nexit /b 0`r`n" | Set-Content (Join-Path $fixtureTools "ninja.cmd") -Encoding ASCII
    $env:PATH = "$fixtureTools;$savedBuildOrderPath"
    $global:LASTEXITCODE = 0
    $buildOrderOutput = @(& $powerShellExecutable -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `
        (Join-Path $fixtureWindows "build.ps1") -Generator ninja -Configuration Debug -Architecture x86_64 `
        -Toolset msc -CompilerCache off -Target FixtureTarget 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Clean Windows build did not generate/bootstrap before reading toolchain provenance:`n$($buildOrderOutput | Out-String)"
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $buildOrderFixture "bootstrap.ready") -PathType Leaf) `
        "Clean Windows build generation fixture"
    Assert-True (Test-Path -LiteralPath (Join-Path $buildOrderFixture "provenance.checked") -PathType Leaf) `
        "Post-generation Windows toolchain provenance fixture"
}
finally {
    $env:PATH = $savedBuildOrderPath
    Remove-Item -LiteralPath $buildOrderFixture -Recurse -Force -ErrorAction SilentlyContinue
}
$hubPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHub\premake5.lua") -Raw
Assert-True ($windowsBuildScript.Contains('$Project.HUB_TARGET') -and
             $windowsBuildScript.Contains('libsodium.dll') -and
             $hubPremake.Contains('DependencyManifest.SodiumDebugRuntime') -and
             $hubPremake.Contains('DependencyManifest.SodiumReleaseRuntime')) `
    "Development Hub builds stage their pinned catalog verifier"
$shaderCompilerScript = Get-Content (Join-Path $Windows "shader-compiler.ps1") -Raw
Assert-True ($shaderCompilerScript.Contains('$hostToolset = "msc"') -and
             $shaderCompilerScript.Contains('"-DCMAKE_C_COMPILER=cl.exe"') -and
             $shaderCompilerScript.Contains('-not $configuredKey') -and
             $shaderCompilerScript.Contains('Get-KeireWorkspaceJunctionPath') -and
             $shaderCompilerScript.Contains('Initialize-KeireWorkspaceJunction') -and
             $shaderCompilerScript.Contains('Remove-KeireGeneratedDirectory') -and
             $shaderCompilerScript.Contains('"short-workspace-v2"')) `
    "Windows host shader compiler uses MSVC, an isolated short workspace, and safe cache replacement"
$dependencyBridge = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Dependencies\CMakeLists.txt") -Raw
Assert-True ($dependencyBridge.Contains('if(APPLE AND TARGET zlibstatic)') -and
             $dependencyBridge.Contains('target_compile_options(zlibstatic PRIVATE -UTARGET_OS_MAC)')) `
    "Apple dependency builds suppress zlib's obsolete classic-Mac branch"
$qualityWorkflow = Get-Content (Join-Path (Get-RepositoryRoot) ".github\workflows\quality.yml") -Raw
Assert-True ($qualityWorkflow.Contains('clang-format==22.1.8') -and
             $qualityWorkflow.Contains('$HOME/.local/bin/clang-format') -and
             -not $qualityWorkflow.Contains('clang-format-18')) `
    "Hosted formatting matches the pinned local Clang 22 formatter"
Assert-True ($qualityWorkflow.Contains('Tools/Linux') -and
             $qualityWorkflow.Contains('linux-${{ runner.arch }}-clang-dependencies-') -and
             $qualityWorkflow.Contains('xargs -0 -n 1 -P "$(nproc)" clang-tidy-18')) `
    "Hosted clang-tidy restores dependency caches and analyzes translation units in parallel"
$ciWorkflow = Get-Content (Join-Path (Get-RepositoryRoot) ".github\workflows\ci.yml") -Raw
Assert-True ($ciWorkflow.Contains("Scripts/Windows/coral.ps1") -and
             $ciWorkflow.Contains("Scripts/Unix/shader-compiler.sh") -and
             $ciWorkflow.Contains("Scripts/Dependencies/**")) `
    "Hosted dependency caches include every build-pipeline input"
$processSource = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Process.cpp") -Raw
Assert-True ($processSource.Contains('CommandLineToArgvW(GetCommandLineW()') -and $processSource.Contains('WideCharToMultiByte(CP_UTF8')) "Shared UTF-8 Windows process command line"
$menuScript = Get-Content (Join-Path $Windows "..\project.ps1") -Raw
Assert-True ($menuScript.Contains('$script:Target = $Project.CLIENT_TARGET')) "Post-rename client target refresh"
Assert-True ($menuScript.Contains('"package-editor"') -and $menuScript.Contains('"package-hub"') -and
             $menuScript.Contains('$Configuration = "Dist"')) "Dist product package launcher commands"
Assert-True ($menuScript.Contains('-AllowDirty (package commands only; emits a local development artifact and is rejected in CI)')) `
    "Windows launcher documents the dirty-package development-only boundary"
$testScript = Get-Content (Join-Path $Windows "test.ps1") -Raw
$coverageScript = Get-Content (Join-Path $Windows "coverage.ps1") -Raw
Assert-True ($testScript.Contains('-Target $Project.CLIENT_TARGET')) "Complete client compile test gate"
$workspaceSmokePattern = '(?s)-Target \$Project\.CLIENT_TARGET.*?SDL_VIDEODRIVER = "dummy".*?' +
    '& \$ClientExe --smoke-workspace'
Assert-True ($testScript -match $workspaceSmokePattern) "Headless editor workspace smoke follows the client build gate"
$clientApplicationSource = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\Source\ClientApplication.cpp") -Raw
Assert-True ($clientApplicationSource.Contains('"--smoke-workspace"') -and
             $clientApplicationSource.Contains('commandLine.SmokeWorkspace ? UiMode::Headless')) `
    "Production headless editor workspace smoke mode"
Assert-True ($testScript.Contains('$hubTestsTarget = "$($Project.PROJECT_NAMESPACE)HubTests"') -and
             $testScript.Contains('& $hubTestsExe')) "Private Hub test suite execution"
Assert-True ($coverageScript.Contains('$target -eq $Project.CLIENT_TARGET') -and
             $coverageScript.Contains('"--smoke-project"') -and
             $coverageScript.Contains('Remove-Item -Force')) "Coverage client smoke and fresh profile execution"
Assert-True ($coverageScript.Contains('$minimumCoreLineCoverage = 74.5') -and
             $coverageScript.Contains('$minimumAggregateLineCoverage = 63.0') -and
             $coverageScript.Contains('llvm-cov core summary')) "Core and aggregate coverage gates"
$editorTestsPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireEditorTests\premake5.lua") -Raw
$hubTestsPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHubTests\premake5.lua") -Raw
$editorTestsMain = Get-Content (Join-Path (Get-RepositoryRoot) "KeireEditorTests\Source\Main.cpp") -Raw
$hubTestsMain = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHubTests\Source\Main.cpp") -Raw
Assert-True (-not $editorTestsPremake.Contains('HubInstance.cpp') -and
             -not $editorTestsPremake.Contains('HubRuntimeTarget') -and
             -not $editorTestsMain.Contains('--hub-instance-secondary') -and
             $hubTestsPremake.Contains('HubInstance.cpp') -and
             $hubTestsMain.Contains('--hub-instance-secondary')) "Hub instance tests remain inside the private Hub suite"
$editorTestWorkingDirectory = '(?s)\$editorOriginalPath = \$env:PATH\s+Push-Location \$Root\s+try \{.*?' +
    '& \$editorTestsExe.*?finally \{.*?Pop-Location'
Assert-True ($testScript -match $editorTestWorkingDirectory) "Editor tests use the repository working directory"
$launcherFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-launcher-exit-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $launcherFixture "Scripts\Windows") | Out-Null
    Copy-Item (Join-Path $Windows "..\project.ps1") (Join-Path $launcherFixture "Scripts\project.ps1")
    @'
function Get-ProjectConfig {
    return [pscustomobject]@{ CLIENT_TARGET = "Client"; PROJECT_IDENTIFIER = "ExitFixture" }
}
function Normalize-Architecture([string]$Architecture) { return "x86_64" }
function Get-RepositoryRoot { return (Resolve-Path (Join-Path $PSScriptRoot "..\..")) }
function Enter-KeireWorkspaceLock { return [pscustomobject]@{ Acquired = $false } }
function Exit-KeireWorkspaceLock {}
'@ | Set-Content (Join-Path $launcherFixture "Scripts\Windows\common.ps1") -Encoding UTF8
    'exit 23' | Set-Content (Join-Path $launcherFixture "Scripts\Windows\test.ps1") -Encoding ASCII
    $launcher = Start-Process -FilePath (Get-Command powershell.exe).Source -ArgumentList @(
        "-NoProfile", "-NonInteractive", "-File", (Join-Path $launcherFixture "Scripts\project.ps1"),
        "test", "-Generator", "ninja", "-Architecture", "x86_64", "-Toolset", "msc"
    ) -Wait -PassThru -WindowStyle Hidden
    Assert-Equal $launcher.ExitCode 23 "Top-level Windows launcher child exit propagation"
}
finally {
    Remove-Item $launcherFixture -Recurse -Force -ErrorAction SilentlyContinue
}
$checkedCommandFailed = $false
try {
    Invoke-CheckedWindowsCommand { & cmd.exe /d /c exit 19 } "Nested package fixture"
}
catch {
    $checkedCommandFailed = $_.Exception.Message.Contains("exit code 19")
}
finally {
    $global:LASTEXITCODE = 0
}
Assert-True $checkedCommandFailed "Nested Windows package child exit propagation"
$packagePolicyFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-package-policy-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force $packagePolicyFixture | Out-Null
    & git -C $packagePolicyFixture init --quiet
    & git -C $packagePolicyFixture config user.email "tests@keire.invalid"
    & git -C $packagePolicyFixture config user.name "Kéire Tests"
    "clean" | Set-Content (Join-Path $packagePolicyFixture "tracked.txt") -Encoding UTF8
    & git -C $packagePolicyFixture add tracked.txt
    & git -C $packagePolicyFixture commit --quiet -m fixture
    $cleanPolicy = Get-WindowsPackageWorktreePolicy -Root $packagePolicyFixture
    Assert-True (-not $cleanPolicy.Dirty -and -not $cleanPolicy.DevelopmentArtifact) "Clean production package policy"
    "dirty" | Set-Content (Join-Path $packagePolicyFixture "untracked.txt") -Encoding UTF8
    Assert-Throws { Get-WindowsPackageWorktreePolicy -Root $packagePolicyFixture } "Dirty production package rejection"
    $dirtyPolicy = Get-WindowsPackageWorktreePolicy -Root $packagePolicyFixture -AllowDirty
    Assert-True ($dirtyPolicy.Dirty -and $dirtyPolicy.DevelopmentArtifact) "Local dirty development package policy"
    Assert-Throws { Get-WindowsPackageWorktreePolicy -Root $packagePolicyFixture -AllowDirty -CI } "CI dirty override rejection"
}
finally {
    Remove-Item $packagePolicyFixture -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-True (-not [string]::IsNullOrWhiteSpace($project.PROJECT_IDENTIFIER)) "Project manifest"
Assert-True ($project.PROJECT_VERSION -match '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') "Semantic project version"
Assert-True (Test-SemanticVersion "1.2.3-alpha.1+build.5") "Complete Semantic Version"
Assert-True (-not (Test-SemanticVersion "01.2.3")) "Semantic Version major leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3-01")) "Semantic Version prerelease leading zero rejection"
Assert-True (-not (Test-SemanticVersion "1.2.3+")) "Empty Semantic Version build rejection"
Assert-Equal $project.PROJECT_MACRO_PREFIX (ConvertTo-MacroPrefix $project.PROJECT_IDENTIFIER) "Project macro prefix"
Assert-Equal (ConvertTo-MacroPrefix "HTTPServer2Client") "HTTP_SERVER2_CLIENT" "Macro prefix derivation"
$securityWorkflow = Get-Content (Join-Path (Get-RepositoryRoot) ".github\workflows\security.yml") -Raw
Assert-True ($securityWorkflow -match "(?m)^  security-status:\s*$") "Security activation sentinel"
Assert-True ($securityWorkflow -match "(?m)^    if: always\(\)\s*$") "Security sentinel always runs"
Assert-True ($securityWorkflow.Contains("ENABLE_ADVANCED_SECURITY")) "Advanced security opt-in variable"
Assert-True (-not $securityWorkflow.Contains("continue-on-error")) "Strict advanced security checks"
$python = Get-PythonInvocation
& $python.Executable @($python.PrefixArguments) (Join-Path (Get-RepositoryRoot) "Scripts\Tests\check-text-integrity.py")
if ($LASTEXITCODE -ne 0) { throw "Versioned text integrity validation failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path (Get-RepositoryRoot) "Scripts\Tests\check-source-budgets.py")
if ($LASTEXITCODE -ne 0) { throw "Source-file budget validation failed." }
& $python.Executable @($python.PrefixArguments) `
    (Join-Path (Get-RepositoryRoot) "Scripts\Tests\check-render-test-boundary.py")
if ($LASTEXITCODE -ne 0) { throw "Renderer production/test-hook boundary validation failed." }
& $python.Executable @($python.PrefixArguments) `
    (Join-Path (Get-RepositoryRoot) "Scripts\Tests\check-diagnostic-bundle-integration.py")
if ($LASTEXITCODE -ne 0) { throw "Diagnostic-bundle product integration validation failed." }
& $python.Executable @($python.PrefixArguments) (Join-Path (Get-RepositoryRoot) "Scripts\Tests\validate-workflows.py")
if ($LASTEXITCODE -ne 0) { throw "GitHub Actions workflow parsing failed." }
$emptyRepository = Join-Path ([IO.Path]::GetTempPath()) ("template-empty-git-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $emptyRepository | Out-Null
try {
    Assert-True (-not (Test-GitRepository $emptyRepository)) "Non-repository detection"
    & git -C $emptyRepository init --quiet
    Assert-Equal (Get-GitHeadCommit $emptyRepository) "uncommitted" "Empty Git commit fallback"
    Assert-True (Test-GitRepository $emptyRepository) "Empty Git repository detection"
}
finally {
    Remove-Item $emptyRepository -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-Equal (Normalize-Architecture "amd64") "x86_64" "x64 normalization"
Assert-Equal (Normalize-Architecture "aarch64") "ARM64" "ARM normalization"
Assert-Equal (Resolve-WindowsToolset "vs2022" "default") "msc" "VS default toolset"
Assert-Equal (Resolve-WindowsToolset "ninja" "default") "msc" "Ninja default toolset"
Assert-Equal (Resolve-WindowsToolset "gmake" "default") "gcc" "GNU Make default toolset"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "DebugUBSan" "x86_64" "msc" } "MSVC UBSan validation"
Assert-Throws { Assert-SupportedBuildCombination "vs2022" "Coverage" "x86_64" "clang" } "Coverage generator validation"
$lock = Get-DependencyLock
Assert-Equal $lock.SPDLOG_COMMIT "79524ddd08a4ec981b7fea76afd08ee05f83755d" "spdlog lock"
Assert-Equal $lock.DOCTEST_COMMIT "2d0a9359a60c51affe2a9bebb1be1dca47868151" "doctest lock"
Assert-Equal $lock.SDL_COMMIT "11a9d3212ef3063a2755982ce71a26b365cef32a" "SDL lock"
Assert-Equal $lock.JSON_COMMIT "55f93686c01528224f448c19128836e7df245f72" "JSON lock"
Assert-Equal $lock.IMGUI_COMMIT "b61e56346a92cfcaf1f43a545ca37b0b32239654" "Dear ImGui lock"
Assert-Equal $lock.ZSTD_COMMIT "f8745da6ff1ad1e7bab384bd1f9d742439278e99" "Zstandard lock"
Assert-Equal $lock.ENTT_COMMIT "85c6bba014049b5de8fad49d25424df2f1f6a8c1" "EnTT lock"
Assert-Equal $lock.GLM_COMMIT "33b0eb9fa336ffd8551024b1d2690e418014553b" "GLM lock"
Assert-Equal $lock.SDL_SHADERCROSS_COMMIT "e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba" "SDL_shadercross lock"
Assert-Equal $lock.SDL_SHADERCROSS_DXC_COMMIT "2c84a1c5ab7091608c97df6ba5ccf46e71c322eb" "DXC recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT "1a6169566c73d3da552748fc372fe2bbb856e46e" "SPIRV-Cross recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT "ad9184e76a66b1001c29db9b0a3e87f646c64de0" "SPIRV-Headers recursive lock"
Assert-Equal $lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT "0539c81f69a3daeb706fd3477dca61435b475156" "SPIRV-Tools recursive lock"
Assert-Equal $lock.ASSIMP_COMMIT "392a658f9c271be965271f45e7521a1b80ea4392" "Assimp lock"
Assert-Equal $lock.STB_COMMIT "2c980bb59875b0d32144a71867fbdebb2f77cd20" "stb lock"
Assert-Equal $lock.FFMPEG_COMMIT "89153eb701d372f54a5d7d29de5067abc09e11d3" "FFmpeg lock"
Assert-Equal $lock.LIBSODIUM_COMMIT "77e1ce5d6dee871c49ef211222ba18ef0c486bda" "libsodium lock"
Assert-Equal $lock.TRACY_COMMIT "05cceee0df3b8d7c6fa87e9638af311dbabc63cb" "Tracy lock"
$vendorScript = Get-Content (Join-Path $Windows "vendor.ps1") -Raw
$vendorUpdateScript = Get-Content (Join-Path $Windows "vendor-update.ps1") -Raw
Assert-True ($vendorScript.Contains('Vendor/imgui') -and $vendorScript.Contains('$Lock.IMGUI_COMMIT')) "Dear ImGui vendor mapping"
Assert-True ($vendorScript.Contains('Scripts\Premake\DearImGui.lua') -and $vendorScript.Contains('imgui_impl_sdlgpu3.cpp')) "Dear ImGui integration validation"
Assert-True ($vendorUpdateScript.Contains('"imgui"')) "Dear ImGui vendor update support"
Assert-True ($vendorScript.Contains('Vendor/zstd') -and $vendorScript.Contains('$Lock.ZSTD_COMMIT') -and $vendorScript.Contains('Scripts\Premake\Zstd.lua')) "Zstandard vendor mapping"
Assert-True ($vendorUpdateScript.Contains('"zstd"')) "Zstandard vendor update support"
Assert-True ($vendorScript.Contains('Vendor/entt') -and $vendorScript.Contains('$Lock.ENTT_COMMIT') -and $vendorScript.Contains('Vendor/glm') -and $vendorScript.Contains('$Lock.GLM_COMMIT')) "ECS and math vendor mappings"
Assert-True ($vendorUpdateScript.Contains('"entt"') -and $vendorUpdateScript.Contains('"glm"')) "ECS and math vendor update support"
Assert-True ($vendorScript.Contains('Vendor/SDL_shadercross') -and $vendorScript.Contains('SDL_SHADERCROSS_DXC_COMMIT') -and $vendorScript.Contains('SPIRV-Tools')) "Recursive shader compiler vendor mapping"
Assert-True ($vendorUpdateScript.Contains('"SDL_shadercross"')) "Shader compiler vendor update support"
Assert-True ($vendorScript.Contains('Vendor/assimp') -and $vendorScript.Contains('$Lock.ASSIMP_COMMIT') -and $vendorScript.Contains('Vendor/stb') -and $vendorScript.Contains('$Lock.STB_COMMIT')) "Asset importer vendor mappings"
Assert-True ($vendorScript.Contains('[switch]$IncludeProfileDependencies') -and
             $vendorScript.Contains('Build/Dependencies/tracy') -and
             $vendorScript.Contains('$Lock.TRACY_COMMIT') -and
             $vendorScript.Contains('$managedProfileCheckout') -and
             $vendorScript.Contains('fetch --no-tags origin $dependency.Commit') -and
             -not $vendorScript.Contains('Vendor/tracy')) `
    "Tracy is an opt-in immutable Profile dependency outside Vendor"
Assert-True ($vendorUpdateScript.Contains('"Tracy"') -and
             $vendorUpdateScript.Contains('Build\Dependencies\tracy') -and
             $vendorUpdateScript.Contains('if ($Dependency -eq "Tracy")') -and
             $vendorUpdateScript.Contains('git add Config/Dependencies.lock') -and
             $vendorUpdateScript.Contains('git add Vendor/$Dependency Config/Dependencies.lock')) `
    "Tracy vendor updates are lock-only while submodule updates retain staging guidance"
$dependencyScript = Get-Content (Join-Path $Windows "dependencies.ps1") -Raw
Assert-True ($dependencyScript.Contains('$Lock.SDL_COMMIT') -and $dependencyScript.Contains('$compiler') -and $dependencyScript.Contains('keire-dependency.stamp')) "Dependency cache identity inputs"
Assert-True ($dependencyScript.Contains('Get-KeireWorkspaceJunctionPath') -and
             $dependencyScript.Contains('Initialize-KeireWorkspaceJunction') -and
             $shaderCompilerScript.Contains('Get-KeireWorkspaceJunctionPath') -and
             $shaderCompilerScript.Contains('Initialize-KeireWorkspaceJunction')) `
    "Checkout-bound Windows dependency junctions use validated distinct workspace identities"
Assert-True ($dependencyScript.Contains('$sourceLayoutIdentity = "workspace-assimp-v3:${assimpPatchedSource}:${assimpPatchDigest}"') -and
             $dependencyScript.Contains('$Lock.LIBSODIUM_COMMIT, $sourceLayoutIdentity, $Architecture')) `
    "Windows native dependency stamps include the isolated patched Assimp source layout"
Assert-True ($dependencyScript.Contains('Patches\Assimp') -and
             $dependencyScript.Contains('keire-assimp-patch.stamp') -and
             $dependencyScript.Contains('git -C $temporary apply --whitespace=error-all') -and
             $dependencyScript.Contains('"-DKEIRE_ASSIMP_SOURCE=$assimpPatchedSource"') -and
             $dependencyScript.Contains('AssimpPatchDigest = "$assimpPatchDigest"')) `
    "Windows dependencies apply and identify the committed Assimp patch set"
$assimpIdentityPatch = Get-Content (Join-Path (Get-RepositoryRoot) `
    "Patches\Assimp\fbx-model-id-names.patch") -Raw
Assert-True ($assimpIdentityPatch.Contains('void FBXConverter::BuildModelNames()') -and
             $assimpIdentityPatch.Contains('ModelName(*cluster->TargetNode())') -and
             $assimpIdentityPatch.Contains('ModelName(*model)')) `
    "Assimp FBX patch shares one model-ID name mapping across nodes, bones, and animations"
Assert-True ($dependencyScript.Contains('Assert-KeireLockedGitSource') -and
             $dependencyScript.Contains('Enter-KeireWorkspaceLock') -and
             $dependencyScript.Contains('-LockRelativePath ".locks\$Name-$Commit.lock"')) `
    "Windows immutable dependency sources fail closed and serialize first population"
Assert-True ($dependencyScript.Contains('[string]::IsNullOrWhiteSpace($LinkTarget)') -and
             $dependencyScript.Contains('Dependency junction target is not an existing directory') -and
             $dependencyScript.Contains('Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue') -and
             $dependencyScript.Contains('[IO.Directory]::Delete($Item.FullName, $false)') -and
             $dependencyScript.Contains('[IO.File]::Delete($Item.FullName)') -and
             -not $dependencyScript.Contains('Remove-Item -LiteralPath $Path -Force')) `
    "Dependency bootstrap repairs dangling junctions without normalizing an empty target"
Assert-True ($dependencyScript.Contains('"Debug", "Release"') -and $dependencyScript.Contains('SDL_DUMMYVIDEO=ON') -and $dependencyScript.Contains('SDL_OFFSCREEN=ON')) "SDL variants and headless drivers"
Assert-True ($dependencyScript.Contains('SDL_GPU=ON') -and $dependencyScript.Contains('SDL_RENDER=OFF')) "SDL GPU renderer policy"
Assert-True ($dependencyScript.Contains('SDL_JOYSTICK=ON') -and
             $dependencyScript.Contains('SDL_HAPTIC=ON') -and
             $dependencyScript.Contains('SDL_HIDAPI=ON') -and
             $dependencyScript.Contains('SDL_HIDAPI_JOYSTICK=ON') -and
             $dependencyScript.Contains('SDL_HIDAPI_LIBUSB=OFF') -and
             $dependencyScript.Contains('SDL_VIRTUAL_JOYSTICK=ON') -and
             -not $dependencyScript.Contains('SDL_JOYSTICK=OFF') -and
             -not $dependencyScript.Contains('SDL_HAPTIC=OFF')) "SDL desktop gamepad capability profile"
Assert-True ($dependencyScript.Contains('Assert-SdlInputBackends') -and
             $dependencyScript.Contains('SDL_JOYSTICK_RAWINPUT') -and
             $dependencyScript.Contains('SDL_HAPTIC_DINPUT') -and
             $dependencyScript.Contains('"hid", "mincore", "dinput8"')) `
    "Windows SDL gamepad backend and static-link validation"
Assert-True ($dependencyScript.Contains('shader-compiler.ps1')) "Host shader compiler bootstrap"
Assert-True ($dependencyScript.Contains('$Lock.LIBSODIUM_COMMIT') -and
             $dependencyScript.Contains('ReleaseDLL') -and
             $dependencyScript.Contains('libsodium.dll')) "Pinned private catalog verifier bootstrap"
$coralRoot = Join-Path (Get-RepositoryRoot) "Patches\Coral"
$coralScript = Get-Content (Join-Path $Windows "coral.ps1") -Raw
$unixCoralScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Unix\coral.sh") -Raw
$unixCommonScript = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Unix\common.sh") -Raw
Assert-True ($unixCoralScript.Contains('pinned_dotnet_sdk_root "$dotnet_path" "$dotnet_sdk_version"') -and
             $unixCoralScript.Contains('"-DDOTNET_EXE=$dotnet_executable"') -and
             $unixCoralScript.Contains('"$dotnet_sdk_version" "$dotnet_root" "$macos_deployment_target"') -and
             ([regex]::Matches(
                 $unixCoralScript,
                 [regex]::Escape('DOTNET_ROOT="$dotnet_root" PATH="$dotnet_root:$PATH"')).Count -eq 2) -and
             $unixCommonScript.Contains('selected_version="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_ROOT="$install_root"')) `
    "Unix Coral overrides stale CMake .NET discovery with the exact pinned SDK"
Assert-True ($coralScript.Contains('git -C $TemporarySource config core.autocrlf false')) `
    "Coral source cache uses deterministic LF checkouts"
Assert-True ($coralScript.Contains('Assert-KeireLockedGitSource') -and
             $coralScript.Contains('-LockRelativePath ".locks\coral-$($Lock.CORAL_COMMIT).lock"') -and
             $coralScript.Contains('-LockRelativePath ".locks\coral-$CacheKey.lock"') -and
             $coralScript.Contains('Resolve-KeirePinnedDotnetSdk') -and
             $coralScript.Contains('Resolve-KeirePinnedNetHost') -and
             $coralScript.Contains('Get-WindowsToolchainIdentity') -and
             $coralScript.Contains('Get-KeireCoralBuildVariantKey') -and
             $coralScript.Contains('$WorkspaceIdentity = Get-KeireWorkspaceIdentity $Root') -and
             $coralScript.Contains('$ExpectedStamp = "$($Lock.CORAL_COMMIT)|$PatchDigest|$BuildVariant|')) `
    "Windows Coral source and variant-isolated patched-build caches are validated and serialized"
Assert-True ($dependencyScript.Contains('-Configuration Debug -Architecture $Architecture') -and
             $dependencyScript.Contains('-Configuration Release -Architecture $Architecture')) `
    "Windows dependency bootstrap forwards the selected target architecture to Coral"
Assert-True ($unixCoralScript.Contains('locked_git_source_validate') -and
             $unixCoralScript.Contains('".locks/coral-$coral_commit.lock"') -and
             $unixCoralScript.Contains('".locks/coral-$cache_key.lock"') -and
             $unixCoralScript.Contains('coral_build_variant_key') -and
             $unixCoralScript.Contains('workspace_key="$(workspace_identity "$ROOT")"') -and
             $unixCoralScript.Contains('expected_stamp="$coral_commit|$patch_digest|$variant_key|')) `
    "Unix Coral source and variant-isolated patched-build caches are validated and serialized"
Assert-True ($unixCoralScript.Contains('xcrun --sdk macosx --show-sdk-path') -and
             $unixCoralScript.Contains('xcrun --sdk macosx --show-sdk-version') -and
             $unixCoralScript.Contains('"-DCMAKE_OSX_SYSROOT=$macos_sdk_path"')) `
    "macOS Coral cache identity and configuration use the same selected SDK"
Assert-True ($coralScript.Contains('Get-Command dotnet -CommandType Application') -and
             $coralScript.Contains('$env:DOTNET_ROOT = $DotnetSdk.Root') -and
             $coralScript.Contains('$selectedVersion -eq $Version') -and
             $coralScript.Contains('"-DDOTNET_EXE=$DotnetExecutable"')) `
    "Coral resolves and configures the exact pinned SDK when it is available through PATH"
Assert-True ($coralScript.Contains('Microsoft.NETCoreSdk.BundledVersions.props') -and
             $coralScript.Contains('AppHostPackVersion') -and
             $coralScript.Contains('RuntimeIdentifier = $runtimeIdentifier') -and
             $coralScript.Contains('Get-FileHash -Algorithm SHA256')) `
    "Coral selects and identities the pinned SDK's exact native nethost pack"
Assert-True ($coralScript.Contains('$env:CL') -and $coralScript.Contains('$env:_CL_') -and
             $coralScript.Contains('$env:LINK') -and $coralScript.Contains('$env:_LINK_') -and
             $coralScript.Contains('$env:CFLAGS') -and
             $coralScript.Contains('$env:CXXFLAGS') -and $coralScript.Contains('$env:CPPFLAGS') -and
             $coralScript.Contains('$env:LDFLAGS') -and $unixCoralScript.Contains('CFLAGS=${CFLAGS-}') -and
             $unixCoralScript.Contains('CXXFLAGS=${CXXFLAGS-}') -and
             $unixCoralScript.Contains('CPPFLAGS=${CPPFLAGS-}') -and
             $unixCoralScript.Contains('LDFLAGS=${LDFLAGS-}')) `
    "Coral cache identity includes honored native build flags"
Assert-True ($dependencyScript.Contains('Enter-KeireWorkspaceLock -RepositoryRoot $Root -CommandName "dependencies"') -and
             $dependencyScript.Contains('Coral Debug and Release metadata must resolve to one checkout-isolated build variant')) `
    "Windows dependency consumers retain a checkout lock and validate Coral variant coherence"
$coralPatchPath = "Patches/Coral/0001-keire-net10-nethost-lifetime.patch"
$coralPatchEol = ([string](& git -C (Get-RepositoryRoot) check-attr eol -- $coralPatchPath)).Trim()
Assert-Equal $coralPatchEol "$coralPatchPath`: eol: lf" "Coral patch LF checkout policy"
$coralHostPatch = Get-Content (Join-Path (Get-RepositoryRoot) $coralPatchPath) -Raw
Assert-True (-not $coralHostPatch.Contains('\ No newline at end of file')) `
    "Coral host patch avoids cross-EOL end-of-file context"
$coralBootstrapPatch = Get-Content (Join-Path $coralRoot "0004-keire-apply-host-settings-before-discovery.patch") -Raw
Assert-True ($coralBootstrapPatch.IndexOf('m_Settings = std::move(InSettings);') -lt
             $coralBootstrapPatch.IndexOf('if (!LoadHostFXR())')) "Bundled .NET root is installed before Coral host discovery"
$coralWarningPatch = Get-Content (Join-Path $coralRoot "0005-keire-warning-clean-native-host.patch") -Raw
Assert-True ($coralWarningPatch.Contains('memcpy(buffer, InString.data(), InString.size() * sizeof(UCChar))') -and
             $coralWarningPatch.Contains('buffer[InString.size()] = {};') -and
             $coralWarningPatch.Contains('reinterpret_cast<const UCChar*>(UINTPTR_MAX)') -and
             $coralWarningPatch.Contains('target_compile_options(Coral.Native PRIVATE /wd4996)')) "Warning-clean Coral native host patch"
$premakePolicy = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Common.lua") -Raw
$distributionPublisherProject = Get-Content `
    (Join-Path (Get-RepositoryRoot) `
        "Services\KeireDistributionService\Source\KeireDistributionPublisher\KeireDistributionPublisher.csproj") -Raw
Assert-True ($premakePolicy.Contains('SDL3DebugLibrary') -and $premakePolicy.Contains('SDL3ReleaseLibrary')) "Premake SDL variant selection"
Assert-True ($premakePolicy.Contains('local function LinkCoralNetHost()') -and
             $premakePolicy.Contains('LinkDependency(DependencyManifest.CoralNetHostRuntime)') -and
             $premakePolicy.Contains('filter { "system:windows or linux" }')) `
    "Premake links macOS against the nethost dylib without changing Windows or Linux native host linkage"
Assert-True ($premakePolicy.Contains('filter "configurations:Profile"') -and
             $premakePolicy.Contains('"KEIRE_PROFILE_TELEMETRY"') -and
             $premakePolicy.Contains('"TRACY_ON_DEMAND"') -and
             $premakePolicy.Contains('"TRACY_ONLY_LOCALHOST"')) `
    "Profile builds enable local on-demand Tracy telemetry"
$testHookPolicy = 'filter\s+"configurations:Debug or DebugASan"\s+defines\s+\{\s+"KEIRE_ENABLE_TEST_HOOKS"\s+\}'
Assert-True ([regex]::IsMatch($premakePolicy, $testHookPolicy) -and
             [regex]::Matches($premakePolicy, '"KEIRE_ENABLE_TEST_HOOKS"').Count -eq 1) `
    "Premake exposes fault-injection hooks only in Debug and DebugASan"
$unixDependencies = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Unix\dependencies.sh") -Raw
$windowsDependencies = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Windows\dependencies.ps1") -Raw
Assert-True ($unixDependencies.Contains('CPP_RTTI_ENABLED=ON') -and
             $windowsDependencies.Contains('CPP_RTTI_ENABLED=ON')) "Jolt and engine RTTI ABI compatibility"
$windowsCommon = Get-Content (Join-Path $Windows "common.ps1") -Raw
Assert-True ($windowsCommon.Contains('KEIRE_VSDEV_ENVIRONMENT_KEY')) "Idempotent Visual Studio environment setup"
Assert-True ($windowsCommon.Contains('"KeireManaged"') -and
             $windowsCommon.Contains('".csproj"')) "Managed source project-generation inventory"
$imguiPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\DearImGui.lua") -Raw
Assert-True ($imguiPremake.Contains('project(DearImGuiProject)') -and $imguiPremake.Contains('kind "StaticLib"') -and $imguiPremake.Contains('targetname(DearImGuiLibrary)')) "Dear ImGui static project"
Assert-True ($imguiPremake.Contains('imgui_impl_sdl3.cpp') -and $imguiPremake.Contains('imgui_impl_sdlgpu3.cpp') -and $imguiPremake.Contains('imgui_stdlib.cpp') -and $imguiPremake.Contains('warnings "Off"')) "Premake Dear ImGui source policy"
Assert-True ($imguiPremake.Contains('../../Build/Projects/DearImGui') -and $imguiPremake.Contains('DependencyManifest.SDL3Include')) "Dear ImGui generated project and SDL wiring"
$rootPremake = Get-Content (Join-Path (Get-RepositoryRoot) "premake5.lua") -Raw
Assert-True ($rootPremake.Contains('group "Dependencies"') -and $rootPremake.Contains('Scripts/Premake/DearImGui.lua')) "Dear ImGui solution grouping"
$headerDependencies = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\HeaderDependencies.lua") -Raw
Assert-True ($rootPremake.Contains('Scripts/Premake/HeaderDependencies.lua') -and $headerDependencies.Contains('project(EnTTProject)') -and $headerDependencies.Contains('project(GLMProject)')) "Header-only dependency solution projects"
Assert-True ($headerDependencies.Contains('../../Build/Projects/EnTT') -and $headerDependencies.Contains('../../Build/Projects/GLM') -and $headerDependencies.Contains('warnings "Off"')) "Header-only dependency IDE and warning policy"
$corePremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\premake5.lua") -Raw
$clientPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\premake5.lua") -Raw
$hubPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHub\premake5.lua") -Raw
$testsPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireTests\premake5.lua") -Raw
$assetToolPremake = Get-Content (Join-Path (Get-RepositoryRoot) "AssetTool\premake5.lua") -Raw
$assetToolSource = Get-Content (Join-Path (Get-RepositoryRoot) "AssetTool\Source\Main.cpp") -Raw
$assetWorkerPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireAssetWorker\premake5.lua") -Raw
$ffmpegTextureBackend = Get-Content `
    (Join-Path (Get-RepositoryRoot) "KeireAssetWorker\Source\FfmpegTextureImportBackend.cpp") -Raw
Assert-True ($corePremake.Contains('touch-ninja-stamp.ps1') -and
             -not $assetWorkerPremake.Contains('CopyWindowsRuntime') -and
             -not $assetWorkerPremake.Contains('copy-files-if-changed.ps1') -and
             -not $assetWorkerPremake.Contains('touch-ninja-stamp.ps1') -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'Scripts\Windows\touch-ninja-stamp.ps1'))) `
    "Ninja prebuild rules publish declared outputs without wildcard Windows FFmpeg staging"
$managedPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Managed.lua") -Raw
$unixFfmpegBuild = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Unix\ffmpeg.sh") -Raw
$canonicalFfmpegArchive = 'git -C "$VENDOR_SOURCE" archive --format=tar "$COMMIT"'
Assert-True $unixFfmpegBuild.Contains($canonicalFfmpegArchive) `
    "Unix FFmpeg builds use canonical Git bytes instead of Windows-translated shell files"
$windowsFfmpegBuild = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Windows\ffmpeg.ps1") -Raw
$windowsFfmpegContractBuild = Get-Content `
    (Join-Path (Get-RepositoryRoot) "Scripts\Windows\ffmpeg-runtime-contract.ps1") -Raw
Assert-True $windowsFfmpegBuild.Contains('git -c core.autocrlf=false -C $VendorSource archive --format=tar') `
    "Windows FFmpeg builds disable caller line-ending conversion for canonical source archives"
Assert-True ($windowsFfmpegBuild.Contains('f101fce22d64db10f500242e23e43a251fe14414') -and
             $windowsFfmpegBuild.Contains('$ConfigureText.LastIndexOf($BrokenMsvcProbe')) `
    "Windows FFmpeg builds apply the validated upstream MSVC configure correction exactly once"
Assert-True ($windowsFfmpegBuild.Contains('$FfbuildDirectory = Join-Path $CacheOutput "ffbuild"') -and
             $windowsFfmpegBuild.Contains('-Path $FfbuildDirectory, $ZlibIncludeDirectory, $ZlibLinkDirectory')) `
    "Windows FFmpeg builds create the out-of-tree configure log directory before configuration"
Assert-True ($windowsFfmpegBuild.Contains('foreach ($component in $RuntimeContract.Files)') -and
             $windowsFfmpegBuild.Contains('"bin\$($component.Component).lib"') -and
             $windowsFfmpegBuild.Contains('"bin\$($component.FileName)"') -and
             $windowsFfmpegContractBuild.Contains('FileName = "avformat-63.dll"') -and
             $windowsFfmpegContractBuild.Contains('FileName = "avcodec-63.dll"') -and
             $windowsFfmpegContractBuild.Contains('FileName = "swresample-7.dll"') -and
             $windowsFfmpegContractBuild.Contains('FileName = "avutil-61.dll"') -and
             -not $windowsFfmpegBuild.Contains('lib\avformat.lib')) `
    "Windows FFmpeg cache validation requires every installed runtime and import library"
Assert-True ($windowsFfmpegBuild.Contains('$candidateLicenseRoot') -and
             $windowsFfmpegBuild.Contains('"COPYING.LGPLv2.1", "COPYING.LGPLv3", "SOURCE.txt"') -and
             $unixFfmpegBuild.Contains('install/share/licenses/ffmpeg/COPYING.LGPLv2.1') -and
             $unixFfmpegBuild.Contains('install/share/licenses/ffmpeg/COPYING.LGPLv3') -and
             $unixFfmpegBuild.Contains('install/share/licenses/ffmpeg/SOURCE.txt')) `
    "FFmpeg cache validation requires package-distribution license inputs"
Assert-True ($windowsFfmpegBuild.Contains('$Toolset -eq "gcc"') -and
             $windowsFfmpegBuild.Contains('do not support the gcc toolset') -and
             $windowsFfmpegBuild.Contains('Enter-WindowsToolEnvironment "vs2022" "msc" $Architecture') -and
             $windowsFfmpegBuild.Contains('--toolchain=msvc') -and
             $windowsFfmpegBuild.Contains('--extra-cflags=`"-MD')) `
    "Windows FFmpeg uses an explicit MSVC producer and rejects unsupported GNU import-library consumers"
Assert-True ($windowsFfmpegBuild.Contains('if (Test-FfmpegOutput $AlternateOutput $AlternateExpected)')) `
    "Windows FFmpeg alternate-configuration reuse applies the complete cache validator"
Assert-True ($unixFfmpegBuild.Contains('valid_ffmpeg_component_artifacts()') -and
             $unixFfmpegBuild.Contains('for component in avformat avcodec swresample avutil') -and
             $unixFfmpegBuild.Contains('lib$component.dylib') -and
             $unixFfmpegBuild.Contains('lib$component.so')) `
    "Unix FFmpeg cache validation requires every shared runtime and linker artifact"
Assert-True ($windowsFfmpegBuild.Contains('--enable-zlib') -and
             $windowsFfmpegBuild.Contains('--enable-decoder=exr') -and
             $windowsFfmpegBuild.Contains('#define CONFIG_EXR_DECODER 1') -and
             $unixFfmpegBuild.Contains('--enable-zlib') -and
             $unixFfmpegBuild.Contains('--enable-decoder=exr') -and
             $unixFfmpegBuild.Contains('#define CONFIG_EXR_DECODER 1')) `
    "Private FFmpeg builds require zlib-backed OpenEXR decoding"
Assert-True ($windowsFfmpegBuild.Contains('$AlternateConfiguration = if ($Configuration -eq "Debug")') -and
             $windowsFfmpegBuild.Contains('Copy-Item -LiteralPath $AlternateComponents') -and
             $windowsFfmpegBuild.Contains('Reused the identical private FFmpeg')) `
    "Identical Windows FFmpeg configurations compile once and publish to both roots"
Assert-True ($windowsFfmpegBuild.Contains('ffmpeg-cache\windows-$OutputArchitecture-msc-producer-$Toolset') -and
             $windowsFfmpegBuild.Contains('function Publish-FfmpegOutput') -and
             $windowsFfmpegBuild.Contains('prefix=${pcfiledir}/../..') -and
             $windowsFfmpegBuild.Contains('Adopted private FFmpeg') -and
             $unixFfmpegBuild.Contains('ffmpeg-cache/$SYSTEM-$OUTPUT_ARCHITECTURE-$TOOLSET') -and
             $unixFfmpegBuild.Contains('publish_ffmpeg_output()') -and
             $unixFfmpegBuild.Contains('prefix=${pcfiledir}/../..') -and
             $unixFfmpegBuild.Contains('Adopted private FFmpeg')) `
    "FFmpeg intermediates are host-specific while active-host publication remains compatible"
Assert-True ($dependencyScript.Contains('$forceFfmpegSourceBuild = $Force') -and
             $dependencyScript.Contains('$forceFfmpegSourceBuild = $false') -and
             $dependencyScript.Contains('-Architecture $Architecture') -and
             $dependencyScript.Contains('-Toolset $Toolset')) `
    "Forced Windows dependency repair compiles the shared FFmpeg source only once"
Assert-True ($ffmpegTextureBackend.Contains('receiveStatus == AVERROR(EAGAIN)') -and
             $ffmpegTextureBackend.Contains('avcodec_send_packet(decoder.get(), nullptr)') -and
             $ffmpegTextureBackend.Contains('frame.format == AV_PIX_FMT_GRAYF16')) `
    "OpenEXR decoding flushes delayed still frames and accepts half-float grayscale textures"
$windowsBuild = Get-Content (Join-Path $Windows "build.ps1") -Raw
$windowsManagedBuild = Get-Content (Join-Path $Windows "build-managed.ps1") -Raw
$windowsManagedHostStage = Get-Content (Join-Path $Windows "stage-managed-host.ps1") -Raw
$windowsRun = Get-Content (Join-Path $Windows "run.ps1") -Raw
$windowsRenderBenchmark = Get-Content (Join-Path $Windows "render-benchmark.ps1") -Raw
$windowsDeviceLoss = Get-Content (Join-Path $Windows "test-render-device-loss.ps1") -Raw
$runtimeAdditiveValidation = Get-Content `
    (Join-Path (Get-RepositoryRoot) "KeireRuntime\Source\RuntimeAdditiveValidation.cpp") -Raw
$runtimeCommandLine = Get-Content `
    (Join-Path (Get-RepositoryRoot) "KeireRuntime\Source\RuntimeCommandLine.cpp") -Raw
$runtimeApplication = Get-Content `
    (Join-Path (Get-RepositoryRoot) "KeireRuntime\Source\RuntimeApplication.cpp") -Raw
$editorPlayValidation = Get-Content `
    (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\EditorSmokePlayValidation.cpp") -Raw
Assert-True ($windowsRenderBenchmark.Contains('@("vsync", "immediate")') -and
             $windowsRenderBenchmark.Contains('warmupFrames -ne 300') -and
             $windowsRenderBenchmark.Contains('measuredFrames -ne 2000') -and
              $windowsRenderBenchmark.Contains('timelines.Count -ne 2000') -and
              $windowsRenderBenchmark.Contains('published non-monotonic frame IDs') -and
              $windowsRenderBenchmark.Contains('build.gitCommit -ne $expectedCommit') -and
              $windowsRenderBenchmark.Contains('[bool]$report.build.dirty -ne $expectedDirty') -and
              $windowsRenderBenchmark.Contains('Assert-MetricSummary $report.summary.$metricName') -and
              $windowsRenderBenchmark.Contains('Assert-RequiredProperties $timeline $timelineFields') -and
              $windowsRenderBenchmark.Contains('framesInFlightHighWaterMark -gt') -and
              $windowsRenderBenchmark.Contains('Remove-Item -LiteralPath $matrixPath -Force') -and
              $windowsRenderBenchmark.Contains('KEIRE_WORKSPACE_CACHE_ROOT') -and
              $windowsRenderBenchmark.Contains('Get-KeireWorkspaceIdentity $Root') -and
              -not $windowsRenderBenchmark.Contains('Join-Path $benchmarkRoot "Temp"') -and
              $windowsRenderBenchmark.Contains('Build\Benchmarks')) `
    "Release render benchmark enforces the fixed VSync matrix and durable result contract"
Assert-True ($windowsDeviceLoss.Contains('--validate-device-loss') -and
             $windowsDeviceLoss.Contains('[string]$CacheRoot = ""') -and
             $windowsDeviceLoss.Contains('Join-Path $Root "Build\Cache\DeviceLoss"') -and
             -not $windowsDeviceLoss.Contains('KeireEngine-pre-demo-hardening-cache') -and
              $windowsDeviceLoss.Contains('--hidden-validation-window') -and
              -not $windowsDeviceLoss.Contains('--content $contentStage --headless') -and
             -not $windowsDeviceLoss.Contains('--frames 6000') -and
             $windowsDeviceLoss.Contains('-SmokePlayDeviceLoss') -and
             $windowsDeviceLoss.Contains('Copy-Item -Path (Join-Path $runtimeSource "*")') -and
             $windowsDeviceLoss.Contains('assetTool cook --project $sampleProject') -and
             $windowsDeviceLoss.Contains('LastWriteTimeUtc -lt $StartedAt') -and
             $windowsDeviceLoss.Contains('build.gitCommit -ne $expectedCommit') -and
             $windowsDeviceLoss.Contains('operation -ne "test frame injection"') -and
              $windowsDeviceLoss.Contains('retryCount -ne 1') -and
              $windowsDeviceLoss.Contains('lostGenerationGpuCleanupCalls -ne 0') -and
              $windowsDeviceLoss.Contains('twoSceneUiCommands -lt 2') -and
              $windowsDeviceLoss.Contains('observedRenderedFrames -lt 2') -and
              $windowsDeviceLoss.Contains('-SmokeTimeoutSeconds $EditorSmokeTimeoutSeconds') -and
              $windowsDeviceLoss.Contains('acceptedFrameBlockedBeforeClose') -and
              $windowsDeviceLoss.Contains('recoveryAttempt -ne 0') -and
              $windowsDeviceLoss.Contains('outstandingFrames -ne 0') -and
              $windowsDeviceLoss.Contains('cookedRuntimeShutdownCompleted = $true') -and
              $windowsDeviceLoss.Contains('cookedRuntimeShutdownDeviceLoss = $true') -and
             $windowsDeviceLoss.Contains('editorShutdownCompleted = $true') -and
             $windowsDeviceLoss.Contains('Build\Validation\DeviceLoss')) `
    "Debug device-loss gate runs rendered runtime/Editor loops and proves shutdown loss containment"
Assert-True ($runtimeAdditiveValidation.Contains('Additive runtime validation timed out.') -and
              $runtimeAdditiveValidation.Contains('std::chrono::minutes(5)') -and
              $runtimeAdditiveValidation.Contains('renderer->Flush()') -and
              $runtimeAdditiveValidation.Contains('SourceSurfaceEpoch == surface.Generation()') -and
              $runtimeAdditiveValidation.Contains('LocalLightMaskConsumed') -and
              $runtimeAdditiveValidation.Contains('FreshPoseSkinnedDepthDraws') -and
              $runtimeAdditiveValidation.Contains('VfxMaskConsumed') -and
              $runtimeAdditiveValidation.Contains('PrepareFreshPoseOcclusionFixture') -and
              $runtimeAdditiveValidation.Contains('77c1e51e-6397-5983-b80b-e82587b2edaa') -and
              $runtimeAdditiveValidation.Contains('BlockNextAcceptedFrame(*renderer)') -and
              $runtimeAdditiveValidation.Contains('FinalizeDeviceLossShutdown') -and
              $runtimeCommandLine.Contains('--hidden-validation-window') -and
               $runtimeCommandLine.Contains('--validate-device-loss does not support --headless') -and
               $runtimeApplication.Contains('specification.Render.Mode = RenderMode::Rendered') -and
               -not $runtimeApplication.Contains('Owner().Renderer()->RequestGpuVfxPipelineWarmup()') -and
               $runtimeApplication.Contains('!hiddenValidationWindow') -and
              $editorPlayValidation.Contains('RecoveryAttemptCountForTest(*renderer)') -and
              $editorPlayValidation.Contains('ObserveOcclusionGameView') -and
              $editorPlayValidation.Contains('SourceSurfaceEpoch == surfaceGeneration') -and
              $editorPlayValidation.Contains('LocalLightMaskConsumed') -and
              $editorPlayValidation.Contains('FreshPoseSkinnedDepthDraws') -and
              $editorPlayValidation.Contains('VfxMaskConsumed') -and
              $editorPlayValidation.Contains('PrepareFreshPoseOcclusionFixture') -and
              $editorPlayValidation.Contains('77c1e51e-6397-5983-b80b-e82587b2edaa') -and
             -not $editorPlayValidation.Contains('RetriedAfterDeviceLoss')) `
    "Runtime and Editor end-to-end validation use stable timeout and recovery evidence"
Assert-True ($windowsRun.Contains('[switch]$SmokePlay') -and
             $windowsRun.Contains('[switch]$SmokePlayDeviceLoss') -and
             $windowsRun.Contains('[int]$SmokeTimeoutSeconds = 0') -and
             $windowsRun.Contains('$Configuration -notin @("Debug", "DebugASan")') -and
             $windowsRun.Contains('"--project", $smokeProjectPath, "--smoke-play"') -and
             $windowsRun.Contains('"--smoke-play-output", $SmokeOutput') -and
             $windowsRun.Contains('"--smoke-play-device-loss"') -and
             -not $windowsRun.Contains('SDL_VIDEODRIVER = "dummy"`r`n        & $ClientExe @smokeArguments')) `
    "Rendered Editor Play smoke uses the real window/update/input loop"
Assert-True ($windowsCommon.Contains('[TimeSpan]$Timeout = [TimeSpan]::Zero') -and
             $windowsCommon.Contains('$process.WaitForExit([int]$timeoutMilliseconds)') -and
             $windowsCommon.Contains('$process.Kill($true)') -and
             $windowsCommon.Contains('taskkill.exe')) `
    "Captured Windows validation processes enforce a bounded process-tree watchdog"
$windowsFfmpeg = Get-Content (Join-Path $Windows "ffmpeg.ps1") -Raw
$windowsFfmpegContract = Get-Content (Join-Path $Windows "ffmpeg-runtime-contract.ps1") -Raw
$windowsFfmpegStage = Get-Content (Join-Path $Windows "stage-ffmpeg-runtime.ps1") -Raw
$windowsPackage = Get-Content (Join-Path $Windows "package.ps1") -Raw
Assert-True ($windowsPackage.Contains('"--validate-additive-runtime"') -and
               $windowsPackage.Contains('$runtimeValidationOutput)') -and
              $windowsPackage.Contains('"--content", $runtimeExecutionContent, "--headless"') -and
              $windowsPackage.Contains('"Build\Validation\packaged-runtime-" + [guid]::NewGuid().ToString("N")') -and
              $windowsPackage.Contains('Copy-Item -LiteralPath $runtimeContent -Destination $runtimeExecutionContent') -and
              $windowsPackage.Contains('Remove-KeireGeneratedDirectory -RepositoryRoot $Root') -and
              $windowsPackage.Contains('-Path $runtimeValidationRoot -Description "packaged runtime validation snapshot"') -and
              -not $windowsPackage.Contains('--frames 6000') -and
              $windowsPackage.Contains('$runtimeValidation.renderMode -ne "rendered"') -and
              $windowsPackage.Contains('$runtimeValidation.renderedWindowLoop') -and
              $windowsPackage.Contains('$runtimeValidation.nativeWindowCreated') -and
              $windowsPackage.Contains('$runtimeValidation.validationWindowHidden') -and
              $windowsPackage.Contains('$runtimeValidation.twoSceneUiCommands -lt 2') -and
               $windowsPackage.Contains('$runtimeValidation.threeSceneUiCommands -lt 2') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.fourSceneContributions -ne 4') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.culled -lt 1') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.ownership.sourceSurfaceEpoch -ne') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.ownership.sourceFrameSlot -lt 0') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.ownership.sourceFrameSlot -ge') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.ownership.sourceDeviceGeneration -ne') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.localLightVisibility.maskConsumed') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.freshPoseSkinned.depthDraws -lt 1') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.vfxVisibility.maskEntries -lt 1') -and
               $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.vfxVisibility.maskedDraws -lt 1') -and
                $windowsPackage.Contains('$runtimeValidation.gpuOcclusion.vfxVisibility.maskConsumed') -and
                $windowsPackage.Contains('$runtimeValidationAttempt -le 2') -and
                $windowsPackage.Contains('$null -eq $runtimeValidationResult.ExitCode') -and
                $windowsPackage.Contains('did not report an exit code after two attempts') -and
                $windowsPackage.Contains('$editorPlayValidation.observedRenderedFrames -lt 2') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.threeSceneContributions -ne 3') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.culled -lt 1') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.ownership.sourceSurfaceEpoch -ne') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.ownership.sourceFrameSlot -lt 0') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.ownership.sourceFrameSlot -ge') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.ownership.sourceDeviceGeneration -ne') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.localLightVisibility.maskConsumed') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.freshPoseSkinned.depthDraws -lt 1') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.vfxVisibility.maskEntries -lt 1') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.vfxVisibility.maskedDraws -lt 1') -and
               $windowsPackage.Contains('$editorPlayValidation.gpuOcclusion.vfxVisibility.maskConsumed') -and
               $windowsPackage.Contains('-Timeout ([TimeSpan]::FromMinutes(6))') -and
               $windowsPackage.Contains('-SmokeTimeoutSeconds 300') -and
              $windowsPackage.Contains('$runtimeValidation.build.gitCommit -ne $commit') -and
              $windowsPackage.Contains('$editorPlayValidation.build.gitCommit -ne $expectedHeadCommit') -and
              $windowsPackage.Contains('LastWriteTimeUtc -lt $runtimeValidationStartedAt') -and
              $windowsPackage.Contains('LastWriteTimeUtc -lt $editorPlayValidationStartedAt') -and
              $windowsPackage.Contains('$runtimeValidation.schemaVersion -ne 1') -and
              $windowsPackage.Contains('$editorPlayValidation.schemaVersion -ne 1') -and
              $windowsPackage.Contains('inputHandledByActiveTopmostPresentation') -and
              $windowsPackage.Contains('failedLoadPreservedWorld') -and
              $windowsPackage.Contains('$buildScenesSource = Join-Path $Root "Samples\KeireSandbox\ProjectSettings\BuildScenes.keiresettings"') -and
              $windowsPackage.Contains('Copy-Item -LiteralPath $buildScenesSource -Destination $buildScenesDestination -Force') -and
              $windowsPackage.Contains('Test-Path -LiteralPath $buildScenesDestination -PathType Leaf')) `
    "Packaged cooked runtime exercises additive scenes, runtime UI, rollback, and exit"
$directPackageScripts = @("package.ps1", "package-editor.ps1", "package-hub.ps1", "package-installer.ps1",
    "package-hub-installer.ps1")
foreach ($directPackageScript in $directPackageScripts) {
    $directPackageSource = Get-Content (Join-Path $Windows $directPackageScript) -Raw
    Assert-True ($directPackageSource.Contains('Enter-KeireWorkspaceLock -RepositoryRoot $Root') -and
                 $directPackageSource.Contains('Exit-KeireWorkspaceLock -Lock $WorkspaceLock')) `
        "Direct Windows package entrypoint '$directPackageScript' shares the repository workspace lock"
}
Assert-True ($windowsPackage.Contains('-SmokePlay -SmokeOutput $editorPlayValidationOutput') -and
             $windowsPackage.Contains('twoPresentationTrees') -and
             $windowsPackage.Contains('topmostInputHandled') -and
             $windowsPackage.Contains('nativeWindowInputQueued')) `
    "Package gate runs the rendered additive Editor Play window and input validation"
Assert-True ($windowsBuild.Contains('$assetWorkerConsumers') -and
             $windowsBuild.Contains('"$($Project.PROJECT_NAMESPACE)EditorTests"') -and
             $windowsBuild.Contains('stage-ffmpeg-runtime.ps1') -and
             $windowsFfmpeg.Contains('Get-KeireFfmpegRuntimeContract') -and
             $windowsFfmpegContract.Contains('FileName = "avcodec-63.dll"') -and
             $windowsFfmpegContract.Contains('FileName = "avformat-63.dll"') -and
             $windowsFfmpegContract.Contains('FileName = "avutil-61.dll"') -and
             $windowsFfmpegContract.Contains('FileName = "swresample-7.dll"') -and
             $windowsFfmpegContract.Contains('"avfilter-*.dll"') -and
             $windowsFfmpegContract.Contains('"swscale-*.dll"') -and
             $windowsFfmpegStage.Contains('Assert-KeireContainedWindowsPath') -and
             $windowsFfmpegStage.Contains('Assert-KeireWindowsPeArchitecture') -and
             $windowsFfmpegStage.Contains('Remove-Item -LiteralPath $candidate -Force') -and
             $windowsPackage.Contains('foreach ($runtime in (Get-WindowsFfmpegRuntimeContract).Files)') -and
             -not $windowsPackage.Contains('-Filter "av*.dll"') -and
             -not $windowsPackage.Contains('-Filter "swresample-*.dll"') -and
             -not $windowsBuild.Contains('-DestinationDirectory $assetWorkerDirectory -Filter "*.dll"')) `
    "all generators restore the exact pinned asset-worker FFmpeg runtime contract"
& (Join-Path $PSScriptRoot "test-windows-ffmpeg-runtime.ps1")
Assert-True ($windowsManagedHostStage.Contains('function Copy-FileIfChanged') -and
             $windowsManagedHostStage.Contains('function Copy-TreeIfChanged') -and
             -not $windowsManagedHostStage.Contains('Copy-Item -Path (Join-Path $coreRuntime.FullName "*")')) `
    "Windows managed-host staging skips unchanged bundled runtime files"
Assert-True ($corePremake.Contains('links { DearImGuiProject, ZstdProject }') -and -not $corePremake.Contains('imgui.cpp') -and -not $premakePolicy.Contains('AddDearImGuiSources')) "Private dependency project ownership"
Assert-True ($corePremake.Contains('VendorIncludeDirs.entt') -and $corePremake.Contains('VendorIncludeDirs.glm') -and
             -not $corePremake.Contains('dependson { EnTTProject, GLMProject }')) `
    "Private ECS and math headers do not add no-op build dependencies"
Assert-True ($hubPremake.Contains('links { HubRuntimeTarget }') -and
             -not $hubPremake.Contains('dependson { ProjectConfig.CLIENT_TARGET }')) `
    "Standalone Hub links its private runtime without depending on an editor build"
Assert-True ($assetToolPremake.Contains('dependson { AssetWorkerTarget }')) "AssetTool builds its private importer worker"
Assert-True ($premakePolicy.Contains('function ApplyLargeWindowsStack()') -and
             $premakePolicy.Contains('linkoptions { "-Xlinker", "/STACK:8388608" }') -and
             $premakePolicy.Contains('linkoptions { "-Wl,--stack,8388608" }') -and
             $testsPremake.Contains('ApplyLargeWindowsStack()') -and
             $hubPremake.Contains('ApplyLargeWindowsStack()') -and
             $assetToolPremake.Contains('ApplyLargeWindowsStack()')) "Portable Windows stack linker options"
Assert-True ($premakePolicy.Contains('buildoptions { "-fms-runtime-lib=dll_dbg" }') -and
             $premakePolicy.Contains('linkoptions { "-fms-runtime-lib=dll_dbg" }') -and
             $premakePolicy.Contains('buildoptions { "-fms-runtime-lib=dll" }')) `
    "Clang uses the dependency-compatible Windows C++ runtime"
Assert-True ($premakePolicy.Contains('function GeneratorRootPath(path)') -and
             $premakePolicy.Contains('SelectedToolset ~= "msc"') -and
             $premakePolicy.Contains('os.host() ~= "macosx"') -and
             $premakePolicy.Contains('os.host() == "macosx"') -and
             $premakePolicy.Contains('linkoptions { ''"'' .. resolved .. ''"'' }') -and
             $premakePolicy.Contains('path:gsub("^%.%./", "")') -and
             $premakePolicy.Contains('local directory, library = resolved:match("^(.*)/(lib[^/]+%.a)$")') -and
             $premakePolicy.Contains('return ":" .. library') -and
             $premakePolicy.Contains('LinkDependencies(DependencyManifest.RecastDebugLibraries)') -and
             $premakePolicy.Contains('LinkDependency(DependencyManifest.SDL3DebugLibrary)') -and
             $assetWorkerPremake.Contains('libdirs { GeneratorRootPath(ffmpegDebug .. "/lib") }')) `
    "Toolset-aware root-relative Ninja and GNU Make dependency links"
Assert-True ($distributionPublisherProject.Contains('<RuntimeFrameworkVersion>10.0.10</RuntimeFrameworkVersion>')) `
    "Distribution publisher runtime patch lock"
Assert-True ($assetToolSource.Contains("--worker-timeout-seconds") -and
             $assetToolSource.Contains("commandLine.WorkerTimeout")) "Configurable asset-worker CLI timeout"
Assert-True ($assetToolSource.Contains("extract-asset-package") -and
             $assetToolSource.Contains("ExtractAssetPackageToStaging") -and
             $assetToolSource.Contains('"manifestSha256"')) `
    "Marketplace automation uses the authoritative asset-package extraction boundary"
Assert-True ($assetWorkerPremake.Contains('filter { "system:linux"') -and
             $assetWorkerPremake.Contains('"-Wl,-rpath,''$$ORIGIN''"') -and
             $assetWorkerPremake.Contains('filter { "system:macosx"') -and
             $assetWorkerPremake.Contains('"-Wl,-rpath,@loader_path"') -and
             -not $assetWorkerPremake.Contains('"system:linux or macosx"') -and
             $unixFfmpegBuild.Contains('--install-name-dir=@rpath')) "Relocatable Linux and macOS asset-worker codecs"
Assert-True ($clientPremake.Contains('AddKeireManagedRuntimeDependency()') -and
             $testsPremake.Contains('AddKeireManagedRuntimeDependency()')) "Managed runtime API consumer dependencies"
Assert-True ($managedPremake.Contains('dependson { KeireManagedProject }') -and
             $managedPremake.Contains('links { KeireManagedProject }') -and
             $managedPremake.Contains('kind "StaticLib"') -and
             $managedPremake.Contains('kind "Utility"') -and
             $managedPremake.Contains('ManagedBuildAnchor.cpp')) "Cross-generator managed runtime API project"
Assert-True ($managedPremake.Contains('buildinputs(managedBuildInputs)') -and
             $managedPremake.Contains('buildoutputs { managedOutput }') -and
             $managedPremake.Contains('linkbuildoutputs "Off"')) "Input-aware managed runtime API custom build"
Assert-True ($managedPremake.Contains('ProjectConfig.PROJECT_NAMESPACE .. "ManagedRuntimeApi"') -and
             $managedPremake.Contains('addManagedBuildInput(managedSourceRoot)') -and
             $managedPremake.Contains('os.matchdirs')) "Collision-free managed source inventory dependencies"
Assert-True ($premakePolicy.Contains('externalanglebrackets "On"') -and
             $premakePolicy.Contains('externalwarnings "Off"') -and
             -not $premakePolicy.Contains('"/external:W0"') -and
             $managedPremake.Contains('objdir ("../../Build/Intermediates/"')) "Warning-clean Visual Studio generation"
Assert-True ($managedPremake.Contains('Scripts/Windows/build-managed.ps1') -and
             $managedPremake.Contains('Scripts/Unix/build-managed.sh') -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'Scripts\Premake\ManagedBuildAnchor.cpp'))) "Managed runtime API wrapper integration"
Assert-True (-not $windowsBuild.Contains('Invoke-ManagedBuild') -and
             -not $windowsBuild.Contains('"build-managed.ps1"') -and
             $windowsManagedBuild.Contains('"Keire.Managed.dll"') -and
             $windowsManagedBuild.Contains('LastWriteTimeUtc')) `
    "Generated target dependencies own managed runtime freshness without launcher duplication"
$preparedContent = Get-Content (Join-Path $Windows "prepare-generated-content.ps1") -Raw
Assert-True ($corePremake.Contains('prepare-generated-content.ps1') -and
             -not $windowsBuild.Contains('"build-info.ps1"') -and
             $preparedContent.Contains('"build-info.ps1"') -and
             $preparedContent.Contains('"builtin-vfx.ps1"') -and
             $preparedContent.Contains('"builtin-occlusion.ps1"') -and
             $preparedContent.Contains('"builtin-spatial-selection.ps1"')) `
    "One Core prebuild process owns generated identity and built-in content"
Assert-True ($corePremake.Contains('pchheader "KeireInternal/KeireCorePch.h"') -and
             $corePremake.Contains('buildoptions { "/FIKeireInternal/KeireCorePch.h" }') -and
             $clientPremake.Contains('pchheader "KeireClient/ClientPch.h"') -and
             $clientPremake.Contains('buildoptions { "/FIKeireClient/ClientPch.h" }') -and
             -not $clientPremake.Contains('dependson { AssetWorkerTarget, AssetToolTarget, RuntimeTarget }')) `
    "Private PCHs and the fast editor compile graph"
Assert-True ($corePremake.Contains('CoreGeneratedContentTarget') -and
             $corePremake.Contains('removebuildoptions { "/MP" }') -and
             -not $corePremake.Contains('buildoptions { "/MP1" }') -and
             $premakePolicy.Contains('CoreArchiveTargets') -and
             $premakePolicy.Contains('linkgroups "On"') -and
             $premakePolicy.Contains('premake.override(ninjaCpp, "linkrule"') -and
             $premakePolicy.Contains('return "rm -f $out && " .. command') -and
             $premakePolicy.Contains('del /F /Q \"$out\" & if exist \"$out\" exit /B 1') -and
             $premakePolicy.Contains('filter { "action:ninja", "system:linux or macosx" }') -and
             $premakePolicy.Contains('enablepch "Off"')) `
    "Parallel Core archives retain stable membership, PCH tracking, and cyclic-link closure"
foreach ($coreArchive in @("Assets", "Build", "World", "Rendering", "Scenes", "Scripting", "Ui", "Vfx")) {
    Assert-True (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Pch\KeireCore${coreArchive}Pch.cpp")) `
        "Core $coreArchive archive owns a distinct PCH source"
}
$editorDevPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\EditorDev.lua") -Raw
Assert-True ($editorDevPremake.Contains('ProjectConfig.PROJECT_NAMESPACE .. "EditorDev"') -and
             $editorDevPremake.Contains('dependson { ProjectConfig.CLIENT_TARGET, AssetToolTarget, AssetWorkerTarget, RuntimeTarget }') -and
             $windowsRun.Contains('$editorDevTarget')) "Complete editor development aggregate"
Assert-True ($windowsBuild.Contains('Resolve-CompilerCache') -and
             $windowsBuild.Contains('ProfileBuild') -and
             $windowsBuild.Contains('    $dependencyConfiguration = if ($Configuration -in @("Release", "Profile", "Dist"))') -and
             $windowsBuild.Contains('/p:PreferredToolArchitecture=x64') -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'Scripts\patch-ninja-compiler-cache.py'))) `
    "Optional compiler cache, build profiling, and native x64 MSVC host tools"
Assert-True ($windowsRun.Contains('[Diagnostics.ProcessStartInfo]::new()') -and
             $windowsRun.Contains('$invalid.StandardError.ReadToEnd()') -and
             $windowsRun.Contains('$invalid.WaitForExit()') -and
             $windowsRun.Contains('$invalid.Dispose()') -and
             -not $windowsRun.Contains('Start-Process -FilePath $ClientExe -ArgumentList "--invalid"')) "Race-free client CLI rejection probe"
$managedFixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-managed-build-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $managedFixture "Scripts\Windows"),
        (Join-Path $managedFixture "KeireManaged"), (Join-Path $managedFixture "Build\Managed") | Out-Null
    Copy-Item (Join-Path $Windows "build-managed.ps1") (Join-Path $managedFixture "Scripts\Windows\build-managed.ps1")
    $managedSource = Join-Path $managedFixture "KeireManaged\RuntimeApi.cs"
    $managedAssembly = Join-Path $managedFixture "Build\Managed\Keire.Managed.dll"
    "source" | Set-Content $managedSource -Encoding ASCII
    "assembly" | Set-Content $managedAssembly -Encoding ASCII
    (Get-Item (Join-Path $managedFixture "Scripts\Windows\build-managed.ps1")).LastWriteTimeUtc =
        [DateTime]::UtcNow.AddMinutes(-3)
    (Get-Item $managedSource).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-2)
    (Get-Item (Join-Path $managedFixture "KeireManaged")).LastWriteTimeUtc =
        [DateTime]::UtcNow.AddMinutes(-2)
    (Get-Item $managedAssembly).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-1)
    $global:LASTEXITCODE = 37
    & (Join-Path $managedFixture "Scripts\Windows\build-managed.ps1")
    Assert-Equal $LASTEXITCODE 0 "Current managed runtime API launcher exit code"
    (Get-Item $managedSource).LastWriteTimeUtc = [DateTime]::UtcNow
    Assert-Throws {
        & (Join-Path $managedFixture "Scripts\Windows\build-managed.ps1")
    } "Stale managed runtime API without SDK"
    (Get-Item $managedSource).LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-2)
    (Get-Item (Join-Path $managedFixture "KeireManaged")).LastWriteTimeUtc = [DateTime]::UtcNow
    Assert-Throws {
        & (Join-Path $managedFixture "Scripts\Windows\build-managed.ps1")
    } "Changed managed source inventory without SDK"
}
finally {
    Remove-Item $managedFixture -Recurse -Force -ErrorAction SilentlyContinue
}
Assert-True ($corePremake.Contains('Source/ECS/Components/CameraComponent.cpp') -and $corePremake.Contains('Source/ECS/Components/MeshRendererComponent.cpp')) "Explicit built-in component translation units"
$generatedContentScript = Get-Content (Join-Path $Windows 'prepare-generated-content.ps1') -Raw
Assert-True ($corePremake.Contains('prepare-generated-content.ps1') -and
             $generatedContentScript.Contains('builtin-shaders.ps1') -and
             $generatedContentScript.Contains('builtin-occlusion.ps1') -and
             $generatedContentScript.Contains('builtin-spatial-selection.ps1') -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'Scripts\Windows\builtin-spatial-selection.ps1')) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinOcclusionDepth.hlsl')) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinOcclusionDebugPyramid.hlsl')) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinOcclusionDebugBounds.hlsl')) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinSpatialSelection.hlsl')) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinUnlit.hlsl'))) `
    "First-party built-in shader generation"
$renderSource = (Get-ChildItem (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Rendering') -File |
    Where-Object { $_.Name -like 'Render*.cpp' -or $_.Name -like 'Render*.h' } |
    Get-Content -Raw) -join "`n"
$renderSource += "`n" + ((Get-ChildItem (Join-Path (Get-RepositoryRoot) 'KeireCore\Include\KeireInternal\Rendering') -File |
    Where-Object { $_.Name -like 'Render*.h' } |
    Get-Content -Raw) -join "`n")
Assert-True ($renderSource.Contains('BuiltinUnlitShaders.h') -and $renderSource.Contains('renderer->Tint()') -and -not $renderSource.Contains('Vendor/SDL/test')) "Mesh tint shader ownership and draw wiring"
$builtinShader = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Shaders\BuiltinUnlit.hlsl') -Raw
Assert-True ($renderSource.Contains('ResolveLighting') -and $renderSource.Contains('DirectionalLightComponent') -and $renderSource.Contains('AmbientAndExposure') -and $builtinShader.Contains('LightDirection') -and $builtinShader.Contains('worldNormal')) "Directional and ambient light wiring"
Assert-True ($builtinShader.Contains('InstanceAddressingData') -and
             $builtinShader.Contains('Instances[InstanceParameters.x + instanceId]') -and
             $renderSource.Contains('GpuOcclusionVisibleInstances') -and
             $renderSource.Contains('SDL_BindGPUVertexStorageBuffers')) `
    "Built-in and material-less meshes use the instance-addressed indirect occlusion path"
Assert-True ($renderSource.Contains('ReadbackRGBA8') -and (Test-Path (Join-Path (Get-RepositoryRoot) 'KeireRenderTests\Source\RenderedOutputTests.cpp'))) "Rendered output readback tests"
$testRunner = Get-Content (Join-Path $Windows 'test.ps1') -Raw
Assert-True ($testRunner.Contains('direct3d12') -and $testRunner.Contains('vulkan') -and $testRunner.Contains('KEIRE_REQUIRE_GPU_TESTS')) "Conditional Windows GPU test backends"
Assert-True ($renderSource.Contains('BuiltinShaderUniformBufferCount(vertex)') -and $renderSource.Contains('SDL_PushGPUFragmentUniformData')) "Built-in and asset-backed shader uniform bindings"
$renderFacadeLines = @(Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Rendering\RenderSystem.cpp')).Count
Assert-True ($renderFacadeLines -lt 700) "RenderSystem facade remains below 700 lines ($renderFacadeLines lines)"
$renderSettingsSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Rendering\RenderSettings.cpp') -Raw
Assert-True ($renderSettingsSource.Contains('Rendering.keiresettings') -and (Test-Path (Join-Path (Get-RepositoryRoot) 'Samples\KeireSandbox\ProjectSettings\Rendering.keiresettings'))) "Persistent project rendering settings"
$renderingAssetsSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Assets\RenderingAssets.cpp') -Raw
Assert-True (@(Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Assets\AssetPipeline.cpp')).Count -lt 600) "AssetPipeline facade remains below 600 lines"
Assert-True (-not ((Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Include\KeireInternal\Assets\AssetDatabaseImplementation.h') -Raw).Contains('recursive_mutex'))) "Asset operations use explicit locked and unlocked entry points"
$uiSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Ui.cpp') -Raw
Assert-True ($renderingAssetsSource.Contains('SDL_GetBasePath()') -and $renderingAssetsSource.Contains('maximumAncestorDepth')) "Executable-relative shader compiler discovery"
Assert-True ($uiSource.Contains('ImGuiDragDropFlags_SourceAllowNullID')) "Display-item drag sources remain assertion-safe"
Assert-True ($clientPremake.Contains('LinkKeireCore()') -and $testsPremake.Contains('LinkKeireCore()') -and $premakePolicy.Contains('ProjectConfig.CORE_TARGET') -and $premakePolicy.Contains('DearImGuiProject')) "Static dependency link closure"
$clientSources = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireClient") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True (-not ($clientSources -match '#include\s*[<\"]imgui|ImGui::|ImGui[A-Z]')) "KeireClient Dear ImGui isolation"
$sourceHeaders = Get-ChildItem (Get-RepositoryRoot) -Directory | Where-Object { $_.Name -in @("KeireCore", "KeireClient", "KeireHub", "KeireTests", "AssetTool", "KeireRuntime") } | ForEach-Object { Get-ChildItem (Join-Path $_.FullName "Source") -Filter "*.h" -File -Recurse -ErrorAction SilentlyContinue }
Assert-True (-not $sourceHeaders) "First-party headers live under Include, never Source"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Assets\Asset.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Assets\AssetSystem.cpp"))) "Asset subsystem directory organization"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Input\Input.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Source\Input\InputSystem.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets\Input\DefaultInput.keireinput"))) "Input subsystem and sample project organization"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Project\Project.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Scenes\SceneSystem.h")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireHub\Source\HubApplication.cpp"))) "Project, scene, and hub organization"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\AssetBrowserPanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\ConsolePanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\DiagnosticsPanel.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\ThumbnailService.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\SceneGizmoController.cpp"))) "Focused editor panel, thumbnail, and scene-gizmo classes"
Assert-True ((Test-Path (Join-Path (Get-RepositoryRoot) "KeireClient\Source\Editor\AssetOperationService.cpp")) -and (Test-Path (Join-Path (Get-RepositoryRoot) "KeireAssetWorker\Source\Main.cpp"))) "Process-isolated editor asset operations"
$workspaceSourcePath = Join-Path (Get-RepositoryRoot) "KeireClient\Source\EditorWorkspaceLayer.cpp"
$workspaceLineCount = @(Get-Content $workspaceSourcePath).Count
Assert-True ($workspaceLineCount -lt 1500) "Editor workspace remains composition-only ($workspaceLineCount lines)"
$documentHeaders = (Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\Include\KeireClient\Editor\SceneDocument.h") -Raw) + (Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\Include\KeireClient\Editor\InputActionsDocument.h") -Raw)
Assert-True (-not ($documentHeaders -match 'Storage\(\)|friend\s+class\s+::EditorWorkspaceLayer')) "Editor documents expose commands rather than mutable storage"
$panelSources = @(
    'HierarchyPanel.cpp', 'InspectorPanel.cpp', 'SceneViewportPanel.cpp', 'InputActionsPanel.cpp',
    'ProjectSettingsPanel.cpp', 'AssetBrowserPanel.cpp', 'AssetInspectorPanel.cpp'
) | ForEach-Object { Join-Path (Get-RepositoryRoot) ("KeireClient\Source\Editor\" + $_) }
Assert-True (-not ($panelSources | Where-Object { -not (Test-Path $_) })) "Editor panels have independent implementation units"
$panelText = ($panelSources | ForEach-Object { Get-Content $_ -Raw }) -join "`n"
$panelHeader = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireClient\Include\KeireClient\Editor\EditorPanels.h') -Raw
$workspaceHeader = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireClient\Include\KeireClient\EditorWorkspaceLayer.h') -Raw
Assert-True ($panelHeader.Contains('class AssetInspectorPanel final') -and
             $panelText.Contains('AssetInspectorPanel::Draw')) "Asset Inspector has an independent state owner"
Assert-True (-not ($panelText -match 'if\s*\(auto\s+\w+\s*=\s*ui\.BeginPanel\([^;]+;\s*!\w+\)')) "Panel RAII scopes outlive their visibility checks"
Assert-True (-not ($panelText.Contains('#include "KeireClient/EditorWorkspaceLayer.h"')) -and
             -not ($workspaceHeader -match 'friend\s+class\s+KeireEditor::')) "Panels use narrow controllers without workspace friendship"
Assert-True (-not ($workspaceHeader.Contains('DrawSceneContent')) -and
             -not ($workspaceHeader.Contains('DrawHierarchyContent')) -and
             -not ($workspaceHeader.Contains('DrawInspectorContent'))) "Workspace exposes no whole-panel draw forwarding"
$processSource = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireCore\Source\Process.cpp') -Raw
Assert-True ($processSource.Contains('std::filesystem::weakly_canonical') -and
             $processSource.Contains('GetWindowsDirectoryW') -and
             $processSource.Contains('L"/select,\"" + resolved.native()') -and
             $processSource.Contains('ShellExecuteW')) "Absolute platform file-manager reveal routing"
$hubSource = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHub\Source\HubApplication.cpp") -Raw
Assert-True ($hubSource.Contains('CreateSystemTray') -and $hubSource.Contains('Show Hub') -and $hubSource.Contains('m_Tray->IsAvailable()')) "Project Hub tray backgrounding"
$hubInstanceSource = Get-Content (Join-Path (Get-RepositoryRoot) "KeireHub\Source\HubInstance.cpp") -Raw
$playerSupportSource = Get-Content (Join-Path $Windows "player-support.ps1") -Raw
Assert-True ($hubSource.Contains('PollActivation') -and $hubInstanceSource.Contains('HubInstanceCoordinator')) "Single-instance Project Hub activation"
Assert-True ($playerSupportSource.Contains("kind = 'windows-resource-update'") -and
             (Test-Path (Join-Path (Get-RepositoryRoot) "Config\Branding\Keire.ico")) -and
             (Test-Path (Join-Path (Get-RepositoryRoot) "Config\Branding\Keire.res")) -and
             $premakePolicy.Contains('AddKeireApplicationIcon')) "Windows application and player icon resources"
Assert-True ($playerSupportSource.Contains('create-build-support') -and
             $playerSupportSource.Contains('$SignatureKeyId') -and
             $playerSupportSource.Contains('--manifest-output')) "Windows Build Support generic-package publication"
$sampleScene = Get-Content (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets\Scenes\SampleScene.keirescene") -Raw
$sampleSceneDocument = $sampleScene | ConvertFrom-Json
Assert-True ([int]$sampleSceneDocument.schemaVersion -ge 2 -and $sampleScene.Contains('"components"') -and $sampleScene.Contains('Directional Light')) "Current-schema component sample scene"
$sampleAssets = Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets"
$sampleMeshes = Join-Path $sampleAssets "Meshes"
$sampleAudio = Join-Path $sampleAssets "Audio"
$orphanedMetadata = @(Get-ChildItem (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets") -File -Recurse |
    Where-Object { $_.Name -like "*.tmp.*.keiremeta" -or $_.Name -like "*~.keiremeta" })
Assert-Equal $orphanedMetadata.Count 0 "Temporary and backup asset metadata sidecars"
$sampleAudioSources = @(Get-ChildItem $sampleAudio -File | Where-Object Extension -eq ".wav")
Assert-Equal $sampleAudioSources.Count 2 "Repository-owned sample audio sources"
foreach ($sampleAudioSource in $sampleAudioSources) {
    $header = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($sampleAudioSource.FullName), 0, 12)
    Assert-True ($header.StartsWith("RIFF") -and $header.EndsWith("WAVE")) "Valid generated sample audio"
}
$interfaceAudioMetadata = Get-Content (Join-Path $sampleAudio "InterfaceConfirm.wav.keiremeta") -Raw | ConvertFrom-Json
$spatialAudioMetadata = Get-Content (Join-Path $sampleAudio "SpatialEmitter.wav.keiremeta") -Raw | ConvertFrom-Json
Assert-True ($interfaceAudioMetadata.id -eq "f42ee69b-dc11-4212-ae66-17bff0be7945" -and
             $spatialAudioMetadata.id -eq "d09e3f28-06e6-49eb-a714-0261348f5eee") "Sample audio scene identities"
$sampleMedia = @(Get-ChildItem $sampleAssets -File -Recurse |
    Where-Object Extension -in @(".mp4", ".mkv", ".webm"))
Assert-Equal $sampleMedia.Count 0 "Undocumented third-party sample media"
$humanoidModel = Join-Path $sampleMeshes "T-Pose.fbx"
$humanoidAnimation = Join-Path $sampleMeshes "Idle.fbx"
Assert-True ((Test-Path -LiteralPath $humanoidModel -PathType Leaf) -and
             (Test-Path -LiteralPath $humanoidAnimation -PathType Leaf)) "Humanoid model and animation source organization"
$duplicateMeshContent = @(Get-ChildItem (Join-Path (Get-RepositoryRoot) "Samples\KeireSandbox\Assets") -Filter "*.fbx" -File -Recurse | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash } | Group-Object | Where-Object Count -gt 1)
Assert-Equal $duplicateMeshContent.Count 0 "Duplicate FBX content"
$modelMetadata = Get-Content ($humanoidModel + ".keiremeta") -Raw | ConvertFrom-Json
$animationMetadata = Get-Content ($humanoidAnimation + ".keiremeta") -Raw | ConvertFrom-Json
$animatorControllerPath = Join-Path $sampleAssets "NewAnimatorController.keireanimgraph"
$animatorController = Get-Content $animatorControllerPath -Raw | ConvertFrom-Json
$animatorControllerMetadata = Get-Content ($animatorControllerPath + ".keiremeta") -Raw | ConvertFrom-Json
$humanoidEntity = @($sampleSceneDocument.entities | Where-Object name -eq 'T-Pose')
Assert-Equal $humanoidEntity.Count 1 "Humanoid sample scene entity"
$humanoidMeshRenderer = @($humanoidEntity[0].components | Where-Object type -eq '4b454952-454d-4553-4852-454e44455201')
$humanoidAnimator = @($humanoidEntity[0].components | Where-Object type -eq '4b454952-4541-4e49-4d41-544f52000001')
Assert-Equal $humanoidMeshRenderer.Count 1 "Humanoid sample mesh renderer"
Assert-Equal $humanoidAnimator.Count 1 "Humanoid sample animator"
Assert-True ($modelMetadata.id -eq '51cd8956-a6c4-4d63-b990-7d86829f92ff' -and
             $modelMetadata.importSettings.contentType -eq 'model' -and
             $modelMetadata.type -eq '4b454952-454d-4553-4841-535345540001' -and
             @($modelMetadata.subAssets) -contains 'c8bf2eaf-9146-5b53-85c8-c3e6dc9b8f08' -and
             @($modelMetadata.subAssets) -contains '78c8dbe3-2951-54b9-b34e-9221c49c506b' -and
             $humanoidMeshRenderer[0].data.mesh -eq '51cd8956-a6c4-4d63-b990-7d86829f92ff' -and
             $humanoidAnimator[0].data.skeleton -eq 'c8bf2eaf-9146-5b53-85c8-c3e6dc9b8f08' -and
             $humanoidAnimator[0].data.skinnedMesh -eq '78c8dbe3-2951-54b9-b34e-9221c49c506b') "Humanoid model identity wiring"
Assert-True ($animationMetadata.id -eq '51116f66-15b6-4dee-acdf-653223e2f491' -and
             $animationMetadata.importSettings.contentType -eq 'animation' -and
             $animationMetadata.type -eq '4b454952-4541-4e49-4d53-4f5552434501' -and
             @($animationMetadata.subAssets) -contains '803c0e5b-d937-521c-821e-92de5a986179' -and
             $animatorController.layers[0].states[0].motion.clip -eq '803c0e5b-d937-521c-821e-92de5a986179') "Humanoid animation identity wiring"
Assert-True ($animatorControllerMetadata.id -eq '9ec01b6f-4862-443e-8cfd-efa2f23ef04a' -and
             $humanoidAnimator[0].data.graph -eq $animatorControllerMetadata.id) "Humanoid Animator Controller identity wiring"
Assert-True ($sampleScene.Contains($interfaceAudioMetadata.id) -and
             $sampleScene.Contains($spatialAudioMetadata.id)) "Sample audio scene wiring"
Assert-True (-not ($sampleScene.Contains('c506e2a8-62f9-44f0-8831-b66755cc9b9b') -or
                   $sampleScene.Contains('070fedd0-9e84-435e-83ae-21b4530159f3'))) "Retired monster scene identities"
$publicHeaders = (Get-ChildItem (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire") -File -Recurse | Get-Content -Raw) -join "`n"
Assert-True ($publicHeaders.Contains('class KEIRE_API UndoService')) "Shared undo service"
Assert-True (-not ($publicHeaders -match 'SDL3/|nlohmann/json|imgui|entt/|glm/|assimp/|stb_image')) "Public dependency isolation"
Assert-True ($publicHeaders.Contains('class KEIRE_API UiWorkspace') -and $clientSources.Contains('BuildFactoryLayout')) "Kéire workspace facade and factory layout wiring"
$exportedTypes = @(
    "Application", "ApplicationCommandLineArguments", "CommandLineError", "EventView", "EventSubscription",
    "EventBus", "Layer", "LayerStack", "LoggerHandle", "Log", "Time", "UiError", "UiScope", "UiWindowScope",
    "UiChildScope", "UiMenuBarScope", "UiMenuScope", "UiTabBarScope", "UiTabItemScope", "UiTreeNodeScope",
    "UiDisabledScope", "UiIdScope", "UiMainMenuBarScope", "UiComboScope", "UiPopupScope", "UiTableScope", "UiDragSourceScope", "UiDragTargetScope", "UiPanelScope", "UiFrame",
    "UiLayoutBuilder", "UiPanelRegistration", "UiWorkspace", "WindowError", "Window", "FolderDialogOperation", "WindowSystem", "ConfigurationError",
    "Asset", "BinaryAsset", "TextAsset", "AssetLoadError", "AssetSystem", "AssetDatabase", "AssetCooker", "InputActionAsset", "InputActionSubscription", "InputActionHandle", "InputActionContext", "InteractiveRebindOperation", "InputSystem", "InputCaptureOverride",
    "Project", "ProjectRegistry", "UndoCommand", "UndoTransaction", "UndoContext", "UndoService", "EntityId", "ComponentTypeId", "Component", "ComponentRegistry", "Entity", "TransformComponent", "DirectionalLightComponent", "CameraComponent", "MeshRendererComponent", "SceneAsset", "Scene", "SceneObjectHandle", "SceneRuntimeSession", "SceneLoadOperation", "SceneSystem", "UiImage", "SaveFileDialogOperation", "SystemTray", "RenderSurface", "RenderView", "RenderSystem", "ShaderAsset", "MaterialAsset", "MeshAsset", "Texture2DAsset"
)
foreach ($exportedType in $exportedTypes) {
    Assert-True ($publicHeaders -match "class\s+KEIRE_API\s+$exportedType\b") "KEIRE_API annotation for $exportedType"
}
foreach ($exportedFunction in @("AssertionFailure", "GetName", "GetBuildInfo", "GetVersionString", "LoadWindowSpecification")) {
    Assert-True ($publicHeaders -match "KEIRE_API[^;{}]*\b$exportedFunction\s*\(") "KEIRE_API annotation for $exportedFunction"
}
Assert-True (-not ($publicHeaders -match 'KEIRE_API[^;{}]*\b(?:GetApplicationCommandLineDescription|CreateApplication)\s*\(')) "Managed-client reverse API ownership"
$packageScript = Get-Content (Join-Path $Windows "package.ps1") -Raw
Assert-True ($packageScript.Contains('$sdlMsvcLibraries') -and
             $packageScript.Contains('$sdlGnuLibraries') -and
             $packageScript.Contains('"hid.lib", "mincore.lib"') -and
             $packageScript.Contains('"dinput8.lib"') -and
             $packageScript.Contains('"-lhid", "-lmincore", "-ldinput8"')) `
    "Extracted Windows SDK consumers link the enabled SDL gamepad backends"
Assert-True ($packageScript.Contains('dear-imgui-LICENSE.txt') -and $packageScript.Contains('$Lock.IMGUI_COMMIT') -and $packageScript.Contains('$imguiLibraryName.lib')) "Dear ImGui package metadata and archive"
Assert-True ($packageScript.Contains('zstandard-LICENSE.txt') -and $packageScript.Contains('$Lock.ZSTD_COMMIT') -and $packageScript.Contains('$zstdLibraryName.lib')) "Zstandard package metadata and archive"
Assert-True ($packageScript.Contains('entt-LICENSE.txt') -and $packageScript.Contains('$Lock.ENTT_COMMIT') -and $packageScript.Contains('glm-COPYING.txt') -and $packageScript.Contains('$Lock.GLM_COMMIT')) "ECS and math package metadata and attribution"
Assert-True ($packageScript.Contains('KeireShaderCompiler.exe') -and $packageScript.Contains('SDL-shadercross-LICENSE.txt') -and $packageScript.Contains('$Lock.SDL_SHADERCROSS_COMMIT')) "Shader compiler package metadata and attribution"
Assert-True ($packageScript.Contains('assimp-LICENSE.txt') -and $packageScript.Contains('stb-LICENSE.txt') -and $packageScript.Contains('$Lock.ASSIMP_COMMIT') -and $packageScript.Contains('$Lock.STB_COMMIT')) "Asset importer package metadata and attribution"
Assert-True ($packageScript.Contains('$assetWorkerName') -and $packageScript.Contains('developmentArtifact') -and $packageScript.Contains('AllowDirty') -and $packageScript.Contains('manifest commit does not match')) "Asset worker and clean package policy"
Assert-True ($packageScript.Contains('[IO.Path]::GetTempPath()') -and
             $packageScript.Contains('Refusing to extract SDK validation outside the process temporary root') -and
             -not $packageScript.Contains('$env:LOCALAPPDATA')) `
    "SDK consumer extraction is confined to the process-scoped temporary root"
Assert-True ($packageScript.Contains('"/Fd:$consumerPdb"') -and
             $packageScript.Contains('"/Fd:$managedPdb"')) `
    "Direct SDK consumer compiler databases stay inside the disposable validation root"
Assert-True ($packageScript.Contains('ManagedApiConsumer.csproj') -and
    $packageScript.Contains('KeireManagedAssembly') -and $packageScript.Contains('Managed API SDK consumer compilation failed')) `
    "Packaged managed API consumer compilation"
$editorPackageScript = Get-Content (Join-Path $Windows "package-editor.ps1") -Raw
Assert-True ($editorPackageScript.Contains('-Configuration Dist') -and $editorPackageScript.Contains('-StageOnly') -and
    $editorPackageScript.Contains('Build\Dependencies\dotnet-sdk') -and
    $editorPackageScript.Contains('Build\Distributions') -and
    $editorPackageScript.Contains('Assert-WindowsEditorPackageStage') -and
    $editorPackageScript.Contains('editor-package.json') -and
    $editorPackageScript.Contains('player-support.ps1') -and
    $editorPackageScript.Contains('-InstalledLayoutRoot') -and
    $editorPackageScript.Contains('bin\BuildSupport')) "Windows Dist editor distribution packaging"
Assert-True ($editorPackageScript.Contains('editor=bin/$($Project.CLIENT_TARGET).exe') -and
    -not $editorPackageScript.Contains('"--entrypoint", "hub=') -and
    -not $editorPackageScript.Contains('"--entrypoint", "worker=') -and
    $editorPackageScript.Contains('KeireHubContent\Fonts')) "Windows editor package excludes Hub ownership except shared fonts"
$hubPackageScript = Get-Content (Join-Path $Windows "package-hub.ps1") -Raw
Assert-True ($hubPackageScript.Contains('KEIRE_DISTRIBUTION_TRUSTED_KEYS') -and
    $hubPackageScript.Contains('[IO.Path]::PathSeparator') -and
    $hubPackageScript.Contains('foreach ($trustedKeyPath in $trustedKeyPaths)')) `
    "Windows Hub packaging supports overlapping distribution trust keys"
$packagePublisher = Get-Content (Join-Path (Get-RepositoryRoot) 'KeireHubPackagePublisher\Source\Main.cpp') -Raw
Assert-True ($packagePublisher.Contains('value.at("dirty").get<bool>()') -and
    $packagePublisher.Contains('value.at("developmentArtifact").get<bool>()') -and
    $packagePublisher.Contains('editor product manifest must be a clean schema-2')) `
    "Stable editor publisher rejects dirty development artifacts"
$packageConfig = Get-Content (Join-Path (Get-RepositoryRoot) "Config\PackageConfig.cmake.in") -Raw
Assert-True ($packageConfig.Contains('@PROJECT_NAMESPACE@ImGui.lib') -and
    $packageConfig.Contains('@PROJECT_NAMESPACE@Zstd.a') -and
    $packageConfig.Contains('"${_assimp_sdk_library}" "${_assimp_zlib_sdk_library}"') -and
    $packageConfig.Contains('"${_jolt_sdk_library}" "${_recast_sdk_libraries}" "${_miniaudio_sdk_library}"') -and
    $packageConfig.Contains('SDL3::SDL3-static')) "Private archive CMake transitive link"
Assert-True (-not $packageConfig.Contains('include;${_core_sdk_prefix}/third-party')) "SDK omits general third-party include path"
$publicLogHeader = Get-Content (Join-Path (Get-RepositoryRoot) "KeireCore\Include\Keire\Log.h") -Raw
Assert-True (-not $publicLogHeader.Contains("spdlog/") -and -not $publicLogHeader.Contains("fmt::") -and $publicLogHeader.Contains("KEIRE_COMPILED_LOG_LEVEL")) "Public logging boundary is engine-owned"
$commonPremake = Get-Content (Join-Path (Get-RepositoryRoot) "Scripts\Premake\Common.lua") -Raw
Assert-True ($commonPremake.Contains('_DISABLE_STRING_ANNOTATION') -and $commonPremake.Contains('_DISABLE_VECTOR_ANNOTATION')) "MSVC sanitizer dependency ABI alignment"
$windowsPackage = Get-Content (Join-Path $Windows "package.ps1") -Raw
$windowsArchiveMergerPath = Join-Path $Windows "merge-static-libraries.ps1"
$windowsArchiveMerger = Get-Content $windowsArchiveMergerPath -Raw
Assert-True ($windowsPackage.Contains('merge-static-libraries.ps1') -and
             $windowsPackage.Contains('$coreArchiveTargets') -and
             $windowsArchiveMerger.Contains('Get-Command lib.exe') -and
             $windowsArchiveMerger.Contains('Move-Item -LiteralPath $temporary')) `
    "SDK packaging recombines internal Core archives into one public library"
$missingArchiveOutput = Join-Path ([IO.Path]::GetTempPath()) ("keire-missing-archive-" + [guid]::NewGuid().ToString("N") + ".lib")
Assert-Throws {
    & $windowsArchiveMergerPath -Output $missingArchiveOutput `
        -InputLibraries (Join-Path ([IO.Path]::GetTempPath()) "keire-missing-input.lib")
} "Static-library merger rejects missing inputs before invoking the librarian"

$packageStage = Join-Path ([IO.Path]::GetTempPath()) ("template-package-test-" + [guid]::NewGuid().ToString("N"))
try {
    foreach ($path in (Get-WindowsRequiredPackagePaths Client Hub Core Core)) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    foreach ($path in @(
        "lib\cmake\CrossPlatformCoreClientTemplate\CrossPlatformCoreClientTemplateConfig.cmake",
        "third-party\spdlog\spdlog.h"
    )) {
        $file = Join-Path $packageStage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    Remove-Item (Join-Path $packageStage "third-party\spdlog") -Recurse -Force
    $publicBuildHeader = Join-Path $packageStage "include\Core\Build\PlayerBuild.h"
    New-Item -ItemType Directory -Force (Split-Path $publicBuildHeader) | Out-Null
    New-Item -ItemType File -Force $publicBuildHeader | Out-Null
    Assert-WindowsPackageStage $packageStage Client Hub Core Core
    $unexpectedPackageRuntime = Join-Path $packageStage "bin\avdevice-63.dll"
    New-Item -ItemType File -Force $unexpectedPackageRuntime | Out-Null
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } `
        "Unexpected FFmpeg SDK package component validation"
    Remove-Item -LiteralPath $unexpectedPackageRuntime -Force
    Assert-WindowsPackageGeneratedDataFree $packageStage
    foreach ($generatedPath in @(
        "samples\KeireSandbox\Build\generated.vcxproj",
        "samples\KeireSandbox\Logs\Core.log",
        "samples\KeireSandbox\Library\AssetCatalog.json",
        "samples\KeireSandbox\Temp\editor.tmp",
        "samples\KeireSandbox\Assets\Scenes\SampleScene.recovery.json"
    )) {
        $generatedFile = Join-Path $packageStage $generatedPath
        New-Item -ItemType Directory -Force (Split-Path $generatedFile) | Out-Null
        New-Item -ItemType File -Force $generatedFile | Out-Null
        Assert-Throws { Assert-WindowsPackageGeneratedDataFree $packageStage } "Generated package data rejection: $generatedPath"
        Remove-Item $generatedFile -Force
    }
    $archiveContamination = Join-Path ([IO.Path]::GetTempPath()) ("template-package-contamination-" + [guid]::NewGuid().ToString("N") + ".zip")
    try {
        $generatedFile = Join-Path $packageStage "samples\KeireSandbox\Build\generated.txt"
        New-Item -ItemType Directory -Force (Split-Path $generatedFile) | Out-Null
        New-Item -ItemType File -Force $generatedFile | Out-Null
        Compress-Archive (Join-Path $packageStage "*") $archiveContamination
        Assert-Throws { Assert-WindowsPackageArchiveGeneratedDataFree $archiveContamination } "Generated archive data rejection"
        Remove-Item $generatedFile -Force
    }
    finally {
        Remove-Item $archiveContamination -Force -ErrorAction SilentlyContinue
    }
    Remove-Item (Join-Path $packageStage "lib\CoreImGui.lib")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing Dear ImGui package archive validation"
    New-Item -ItemType File (Join-Path $packageStage "lib\CoreImGui.lib") | Out-Null
    Remove-Item (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing Dear ImGui package license validation"
    New-Item -ItemType File (Join-Path $packageStage "third-party\licenses\dear-imgui-LICENSE.txt") | Out-Null
    Remove-Item (Join-Path $packageStage "third-party\licenses\spdlog-LICENSE.txt")
    Assert-Throws { Assert-WindowsPackageStage $packageStage Client Hub Core Core } "Missing package license validation"
}
finally {
    Remove-Item $packageStage -Recurse -Force -ErrorAction SilentlyContinue
}

$trackedSampleStage = Join-Path ([IO.Path]::GetTempPath()) ("template-tracked-sample-" + [guid]::NewGuid().ToString("N"))
try {
    Copy-WindowsTrackedTree (Get-RepositoryRoot) "Samples/KeireSandbox" $trackedSampleStage
    Assert-True (Test-Path (Join-Path $trackedSampleStage "ProjectSettings\Project.keireproject")) "Tracked sample copy includes project settings"
    Assert-WindowsPackageGeneratedDataFree $trackedSampleStage
}
finally {
    Remove-Item $trackedSampleStage -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host ("Fast Windows script checks completed in {0:N2}s." -f $started.Elapsed.TotalSeconds)
}

if ($runIntegration) {
$integrationStarted = [Diagnostics.Stopwatch]::StartNew()
$identityFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-identity-test-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $identityFixture "Scripts\Windows"), (Join-Path $identityFixture "Config") | Out-Null
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "build-info.ps1") (Join-Path $identityFixture "Scripts\Windows")
    $identityConfig = @(
        "PROJECT_IDENTIFIER=IdentityFixture", 'PROJECT_DISPLAY_NAME=Quoted "Kéire" \\ Client',
        "PROJECT_VERSION=1.2.3-alpha.1+build.5", "PROJECT_NAMESPACE=IdentityFixture", "PROJECT_MACRO_PREFIX=IDENTITY_FIXTURE",
        "CORE_TARGET=IdentityFixtureCore", "CORE_DIRECTORY=IdentityFixtureCore", "CLIENT_TARGET=IdentityFixtureClient", "CLIENT_DIRECTORY=IdentityFixtureClient", "HUB_TARGET=IdentityFixtureHub", "HUB_DIRECTORY=IdentityFixtureHub",
        "TESTS_TARGET=IdentityFixtureTests", "TESTS_DIRECTORY=IdentityFixtureTests", "ARTIFACT_PREFIX=identityfixture", "REPOSITORY_SLUG=example/identity-fixture"
    )
    [IO.File]::WriteAllLines((Join-Path $identityFixture "Config\Project.conf"), $identityConfig, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $identityFixture ".gitignore"), "/Build/`n.ninja_lock`n", [Text.UTF8Encoding]::new($false))
    & git -C $identityFixture init --quiet
    & git -C $identityFixture config user.email "scripts@example.invalid"
    & git -C $identityFixture config user.name "Script Tests"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m first
    $firstCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $identityHeader = Join-Path $identityFixture "Build\Generated\IdentityFixture\BuildInfo.generated.h"
    $firstIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_VERSION "1.2.3-alpha.1+build.5"')) "Semantic Version identity generation"
    Assert-True ($firstIdentity.Contains('#define KEIRE_BUILD_PROJECT_NAME "Quoted \"Kéire\" \\\\ Client"')) "C string identity escaping"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_COMMIT `"$firstCommit`"")) "Clean Git identity"
    Assert-True ($firstIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Clean Git dirty state"
    New-Item -ItemType File -Force (Join-Path $identityFixture ".ninja_lock") | Out-Null
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Ignored Ninja lock dirty state"
    $firstWriteTime = [IO.File]::GetLastWriteTimeUtc($identityHeader)
    Start-Sleep -Milliseconds 1100
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-Equal ([IO.File]::GetLastWriteTimeUtc($identityHeader)) $firstWriteTime "Unchanged identity header timestamp"
    Set-Content -LiteralPath (Join-Path $identityFixture "untracked.txt") -Value untracked
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    Assert-True (([IO.File]::ReadAllText($identityHeader)).Contains("#define KEIRE_BUILD_GIT_DIRTY true")) "Untracked Git dirty state"
    & git -C $identityFixture add .
    & git -C $identityFixture commit --quiet -m second
    $secondCommit = (& git -C $identityFixture rev-parse HEAD) -join ""
    & (Join-Path $identityFixture "Scripts\Windows\build-info.ps1")
    $secondIdentity = [IO.File]::ReadAllText($identityHeader, [Text.Encoding]::UTF8)
    Assert-True ($secondCommit -ne $firstCommit -and $secondIdentity.Contains($secondCommit)) "Identity refresh after commit"
    Assert-True ($secondIdentity.Contains("#define KEIRE_BUILD_GIT_DIRTY false")) "Committed Git clean state"
}
finally {
    Remove-Item -LiteralPath $identityFixture -Recurse -Force -ErrorAction SilentlyContinue
}

$parentFixture = Join-Path ([IO.Path]::GetTempPath()) ("template-script-test-" + [guid]::NewGuid().ToString("N"))
$fixture = Join-Path $parentFixture "Template"
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $coreDirectory = $project.CORE_DIRECTORY
    $clientDirectory = $project.CLIENT_DIRECTORY
    $hubDirectory = $project.HUB_DIRECTORY
    $testsDirectory = $project.TESTS_DIRECTORY
    $projectNamespace = $project.PROJECT_NAMESPACE
    foreach ($directory in @("Scripts\Windows", "Config", "Examples\Consumer\Source", "Examples\ManagedConsumer\Source", "$coreDirectory\Include\$projectNamespace", "$coreDirectory\Source", "$clientDirectory\Source", "$hubDirectory\Source", "$testsDirectory\Source", "Vendor", "Build\Bin")) {
        New-Item -ItemType Directory -Force (Join-Path $fixture $directory) | Out-Null
    }
    Copy-Item (Join-Path $Windows "common.ps1"), (Join-Path $Windows "rename.ps1"), (Join-Path $Windows "clean.ps1"), (Join-Path $Windows "doctor.ps1") (Join-Path $fixture "Scripts\Windows")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Project.conf") (Join-Path $fixture "Config\Project.conf")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\Client.json") (Join-Path $fixture "Config\Client.json")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Config\PackageConfig.cmake.in") (Join-Path $fixture "Config\PackageConfig.cmake.in")
    Copy-Item (Join-Path (Get-RepositoryRoot) "premake5.lua") (Join-Path $fixture "premake5.lua")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\Consumer\CMakeLists.txt") (Join-Path $fixture "Examples\Consumer")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\Consumer\Source\Main.cpp") (Join-Path $fixture "Examples\Consumer\Source")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\CMakeLists.txt") (Join-Path $fixture "Examples\ManagedConsumer")
    Copy-Item (Join-Path (Get-RepositoryRoot) "Examples\ManagedConsumer\Source\ClientApplication.cpp") (Join-Path $fixture "Examples\ManagedConsumer\Source")
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Core.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_CORE_H
namespace $projectNamespace { const char* GetName(); }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Include\$projectNamespace\Log.h") @"
#ifndef $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
#define $($project.PROJECT_MACRO_PREFIX)_CORE_LOG_H
namespace $projectNamespace { class Log; }
#endif
"@
    Set-Content (Join-Path $fixture "$coreDirectory\Source\Library.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$clientDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$hubDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "$testsDirectory\Source\Main.cpp") "#include `"$projectNamespace/Core.h`""
    Set-Content (Join-Path $fixture "README.md") "$($project.PROJECT_IDENTIFIER) $($project.REPOSITORY_SLUG) Scripts/Tests Core.log Client.log"
    Set-Content (Join-Path $fixture "Vendor\keep.txt") 'vendor'
    Set-Content (Join-Path $fixture "Build\Bin\remove.txt") 'build'

    & git -C $parentFixture init --quiet
    & git -C $parentFixture config user.email "scripts@example.invalid"
    & git -C $parentFixture config user.name "Script Tests"
    & git -C $parentFixture add Template
    & git -C $parentFixture commit --quiet -m fixture
    Assert-Equal (Get-GitWorktreeRoot $fixture).Path (Resolve-Path $parentFixture).Path "Parent Git worktree detection"
    Add-Content (Join-Path $fixture "README.md") "dirty"

    Assert-Throws { & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName "Bad`nName" -Repository example/script-fixture } "Rename newline rejection"
    $unicodeDisplayName = 'Script "Fixturé" \\ Name'
    & (Join-Path $fixture "Scripts\Windows\rename.ps1") -Name ScriptFixture -DisplayName $unicodeDisplayName -Repository example/script-fixture
    Assert-True (-not (Test-Path (Join-Path $fixture ".git"))) "Nested Git repository prevention"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Include\ScriptFixture\Core.h")) "Rename structure"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureHub\Source\Main.cpp")) "Rename hub structure"
    $renamed = Get-Content (Join-Path $fixture "Config\Project.conf") -Raw -Encoding UTF8
    Assert-True ($renamed.Contains("CORE_TARGET=ScriptFixtureCore")) "Rename manifest"
    Assert-True ($renamed.Contains("PROJECT_MACRO_PREFIX=SCRIPT_FIXTURE")) "Rename macro manifest"
    Assert-True ($renamed.Contains("PROJECT_VERSION=$($project.PROJECT_VERSION)")) "Rename version preservation"
    $renamedPremake = Get-Content (Join-Path $fixture "premake5.lua") -Raw
    Assert-True ($renamedPremake.Contains("valid Semantic Version 2.0.0")) "Premake Semantic Version validation"
    $renamedConsumer = Get-Content (Join-Path $fixture "Examples\Consumer\CMakeLists.txt") -Raw
    Assert-True ($renamedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed CMake package identity"
    Assert-True ($renamedConsumer.Contains("ScriptFixture::Core")) "Renamed CMake imported target"
    $renamedManagedConsumer = Get-Content (Join-Path $fixture "Examples\ManagedConsumer\CMakeLists.txt") -Raw
    Assert-True ($renamedManagedConsumer.Contains("find_package(ScriptFixture CONFIG REQUIRED)")) "Renamed managed CMake package identity"
    Assert-True ($renamedManagedConsumer.Contains("ScriptFixture::Core")) "Renamed managed CMake imported target"
    Assert-True ((Get-Content (Join-Path $fixture "Config\PackageConfig.cmake.in") -Raw).Contains("@PROJECT_NAMESPACE@::Core")) "Generic package template preservation"
    Assert-True ($renamed.Contains("PROJECT_DISPLAY_NAME=$unicodeDisplayName")) "UTF-8 display name preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("dirty")) "Pre-existing edit preservation"
    Assert-True ((Get-Content (Join-Path $fixture "README.md") -Raw).Contains("Scripts/Tests Core.log Client.log")) "Stable generic path preservation"
    Assert-True (Test-Path (Join-Path $fixture "Config\Client.json")) "Stable client configuration path preservation"
    $renamedHeaders = (Get-ChildItem (Join-Path $fixture "ScriptFixtureCore\Include") -File -Recurse | Get-Content) -join "`n"
    Assert-True (-not $renamedHeaders.Contains($project.PROJECT_MACRO_PREFIX)) "Old include guard removal"
    Assert-True ($renamedHeaders.Contains("SCRIPT_FIXTURE_CORE_CORE_H")) "Renamed include guard"
    & (Join-Path $fixture "Scripts\Windows\doctor.ps1") -Generator ninja -Architecture x86_64 -Toolset clang

    & (Join-Path $fixture "Scripts\Windows\clean.ps1") -Scope full
    Assert-True (-not (Test-Path (Join-Path $fixture "Build\Bin"))) "Full clean build removal"
    Assert-True (Test-Path (Join-Path $fixture "Vendor\keep.txt")) "Full clean vendor preservation"
    Assert-True (Test-Path (Join-Path $fixture "ScriptFixtureCore\Source\Library.cpp")) "Full clean source preservation"
}
finally {
    if ($parentFixture.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path $parentFixture)) {
        Remove-Item -LiteralPath $parentFixture -Recurse -Force
    }
}
Write-Host ("Windows script integration fixtures completed in {0:N2}s." -f $integrationStarted.Elapsed.TotalSeconds)
}

if ($runFast) {
    $searchExitCode = Invoke-RepositoryBatchSearch $false @(
        '\b(CORE|CLIENT)_(API|ASSERT|ASSERTIONS_ENABLED|TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)\b'
    )
    Assert-Equal $searchExitCode 1 "Deprecated public macro batch scan"
    $searchExitCode = Invoke-RepositoryBatchSearch $true @(
        '#include "KeireCore/', 'Scripts/KeireTests', 'Scripts\KeireTests', 'Scripts/Windows/Tests',
        'Scripts/Unix/Tests', 'KeireCore.log', 'KeireClient.log'
    )
    Assert-Equal $searchExitCode 1 "Stale repository identity batch scan"
}
& (Join-Path $PSScriptRoot 'test-player-support-runtime-windows.ps1')
Write-Host ("Windows $Suite script regression tests passed in {0:N2}s." -f $started.Elapsed.TotalSeconds)
$global:LASTEXITCODE = 0
