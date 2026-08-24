$ErrorActionPreference = "Stop"

function Get-KeireWorkspaceLockSetting {
    param([string]$Name, [int]$Default, [int]$Minimum)

    $text = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($text)) { return $Default }
    $value = 0
    if (-not [int]::TryParse($text, [ref]$value) -or $value -lt $Minimum) {
        throw "$Name must be an integer greater than or equal to $Minimum."
    }
    return $value
}

function Get-KeireWorkspaceLockOwner {
    param([Parameter(Mandatory = $true)][string]$LockPath)

    $values = @{}
    $ownerPath = Join-Path $LockPath "owner"
    if (-not (Test-Path -LiteralPath $ownerPath -PathType Leaf)) { return $values }
    foreach ($line in Get-Content -LiteralPath $ownerPath -Encoding UTF8 -ErrorAction SilentlyContinue) {
        if ($line -match '^([a-z]+)=(.*)$') { $values[$Matches[1]] = $Matches[2] }
    }
    return $values
}

function Remove-KeireStaleWorkspaceLock {
    param(
        [Parameter(Mandatory = $true)][string]$LockPath,
        [Parameter(Mandatory = $true)][string]$QuarantinePath
    )

    try {
        Move-Item -LiteralPath $LockPath -Destination $QuarantinePath -ErrorAction Stop
    }
    catch {
        return $false
    }
    foreach ($name in @("owner", "heartbeat")) {
        Remove-Item -LiteralPath (Join-Path $QuarantinePath $name) -Force -ErrorAction SilentlyContinue
    }
    try {
        Remove-Item -LiteralPath $QuarantinePath -Force -ErrorAction Stop
    }
    catch {
        throw "The stale workspace lock contains unexpected files. Inspect and remove '$QuarantinePath' manually."
    }
    return $true
}

function Enter-KeireWorkspaceLock {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$CommandName
    )

    $timeoutSeconds = Get-KeireWorkspaceLockSetting "KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS" 7200 1
    $staleSeconds = Get-KeireWorkspaceLockSetting "KEIRE_WORKSPACE_LOCK_STALE_SECONDS" 300 10
    $heartbeatSeconds = Get-KeireWorkspaceLockSetting "KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS" 5 1
    if ($heartbeatSeconds * 3 -ge $staleSeconds) {
        throw "KEIRE_WORKSPACE_LOCK_STALE_SECONDS must be more than three heartbeat intervals."
    }

    $lockParent = Join-Path $RepositoryRoot "Tools\.locks"
    $lockPath = Join-Path $lockParent "project-command.lock"
    New-Item -ItemType Directory -Force -Path $lockParent | Out-Null

    $inheritedToken = [Environment]::GetEnvironmentVariable("KEIRE_WORKSPACE_LOCK_TOKEN")
    if (Test-Path -LiteralPath $lockPath -PathType Container) {
        $existingOwner = Get-KeireWorkspaceLockOwner -LockPath $lockPath
        if ($inheritedToken -and $existingOwner.token -eq $inheritedToken) {
            return [pscustomobject]@{ Acquired = $false; Path = $lockPath; Token = $inheritedToken; Job = $null; PreviousToken = $inheritedToken }
        }
    }

    $token = [Guid]::NewGuid().ToString("N")
    $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    $reportedWait = $false
    while ($true) {
        try {
            New-Item -ItemType Directory -Path $lockPath -ErrorAction Stop | Out-Null
            break
        }
        catch [System.IO.IOException] {}
        catch [System.UnauthorizedAccessException] {}

        if (-not (Test-Path -LiteralPath $lockPath -PathType Container)) {
            throw "Workspace lock path is not a directory: '$lockPath'. Remove it manually after confirming no project command is running."
        }

        $owner = Get-KeireWorkspaceLockOwner -LockPath $lockPath
        if (-not $reportedWait) {
            $summary = "host=$($owner.host), platform=$($owner.platform), pid=$($owner.pid), command=$($owner.command), started=$($owner.started)"
            Write-Host "==> Waiting for another Kéire project command ($summary)."
            Write-Host "    Shared workspace lock: $lockPath"
            $reportedWait = $true
        }

        $heartbeatPath = Join-Path $lockPath "heartbeat"
        $leasePath = if (Test-Path -LiteralPath $heartbeatPath -PathType Leaf) { $heartbeatPath } else { $lockPath }
        $firstWrite = (Get-Item -LiteralPath $leasePath -Force).LastWriteTimeUtc
        if (([DateTime]::UtcNow - $firstWrite).TotalSeconds -ge $staleSeconds) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $leasePath) {
                $secondWrite = (Get-Item -LiteralPath $leasePath -Force).LastWriteTimeUtc
                if ($secondWrite -eq $firstWrite -and ([DateTime]::UtcNow - $secondWrite).TotalSeconds -ge $staleSeconds) {
                    $quarantinePath = "$lockPath.stale.$token"
                    if (Remove-KeireStaleWorkspaceLock -LockPath $lockPath -QuarantinePath $quarantinePath) {
                        Write-Host "==> Recovered expired Kéire workspace lock."
                        continue
                    }
                }
            }
        }

        if ([DateTime]::UtcNow -ge $deadline) {
            throw "Timed out after $timeoutSeconds seconds waiting for '$lockPath'. Confirm the reported owner is no longer running before removing the lock."
        }
        Start-Sleep -Milliseconds 250
    }

    $safeCommand = ($CommandName -replace '[\r\n=]', '_')
    $safeHost = ([Environment]::MachineName -replace '[\r\n=]', '_')
    $ownerPath = Join-Path $lockPath "owner"
    $ownerTemporary = Join-Path $lockPath "owner.tmp.$token"
    $heartbeatPath = Join-Path $lockPath "heartbeat"
    $heartbeatJob = $null
    $previousToken = $inheritedToken
    try {
        $ownerText = "token=$token`nplatform=windows`npid=$PID`nhost=$safeHost`ncommand=$safeCommand`nstarted=$([DateTime]::UtcNow.ToString('o'))`n"
        [IO.File]::WriteAllText($ownerTemporary, $ownerText, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $ownerTemporary -Destination $ownerPath
        [IO.File]::WriteAllText($heartbeatPath, "", [Text.UTF8Encoding]::new($false))
        [Environment]::SetEnvironmentVariable("KEIRE_WORKSPACE_LOCK_TOKEN", $token)

        $heartbeatJob = Start-Job -ScriptBlock {
            param($LockPath, $Token, $IntervalSeconds)
            $ownerPath = Join-Path $LockPath "owner"
            $heartbeatPath = Join-Path $LockPath "heartbeat"
            while ($true) {
                Start-Sleep -Seconds $IntervalSeconds
                if (-not (Test-Path -LiteralPath $ownerPath -PathType Leaf)) { break }
                $firstLine = Get-Content -LiteralPath $ownerPath -Encoding UTF8 -TotalCount 1 -ErrorAction SilentlyContinue
                if ($firstLine -ne "token=$Token") { break }
                try { [IO.File]::SetLastWriteTimeUtc($heartbeatPath, [DateTime]::UtcNow) } catch { break }
            }
        } -ArgumentList $lockPath, $token, $heartbeatSeconds
    }
    catch {
        if ($heartbeatJob) {
            Stop-Job -Job $heartbeatJob -ErrorAction SilentlyContinue
            Remove-Job -Job $heartbeatJob -Force -ErrorAction SilentlyContinue
        }
        [Environment]::SetEnvironmentVariable("KEIRE_WORKSPACE_LOCK_TOKEN", $previousToken)
        foreach ($path in @($ownerTemporary, $heartbeatPath, $ownerPath)) {
            Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
        throw
    }

    return [pscustomobject]@{ Acquired = $true; Path = $lockPath; Token = $token; Job = $heartbeatJob; PreviousToken = $previousToken }
}

function Exit-KeireWorkspaceLock {
    param([Parameter(Mandatory = $true)]$Lock)

    if (-not $Lock.Acquired) { return }
    if ($Lock.Job) {
        Stop-Job -Job $Lock.Job -ErrorAction SilentlyContinue
        Remove-Job -Job $Lock.Job -Force -ErrorAction SilentlyContinue
    }
    $owner = Get-KeireWorkspaceLockOwner -LockPath $Lock.Path
    if ($owner.token -eq $Lock.Token) {
        Remove-Item -LiteralPath (Join-Path $Lock.Path "heartbeat") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $Lock.Path "owner") -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $Lock.Path -Force -ErrorAction SilentlyContinue
    }
    [Environment]::SetEnvironmentVariable("KEIRE_WORKSPACE_LOCK_TOKEN", $Lock.PreviousToken)
}

function Read-KeyValueFile {
    param([string]$Path)
    $values = @{}
    # Project files are UTF-8 without a BOM. Windows PowerShell 5 otherwise
    # decodes them using the legacy system code page.
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        if ($line -match '^([A-Z0-9_]+)=(.*)$') {
            if ($values.ContainsKey($Matches[1])) { throw "Duplicate key '$($Matches[1])' in $Path." }
            $values[$Matches[1]] = $Matches[2]
        }
        elseif ($line.Length -ne 0) { throw "Malformed configuration line in $Path`: $line" }
    }
    return $values
}

function Test-SemanticVersion {
    param([string]$Version)
    return $Version -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$'
}

function Get-RepositoryRoot { return (Resolve-Path (Join-Path $PSScriptRoot "..\..")) }
function Get-ProjectConfig { return Read-KeyValueFile (Join-Path (Get-RepositoryRoot) "Config\Project.conf") }
function Get-DependencyLock { return Read-KeyValueFile (Join-Path (Get-RepositoryRoot) "Config\Dependencies.lock") }

function Invoke-CheckedWindowsCommand {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Command,
        [Parameter(Mandatory = $true)][string]$Description
    )
    $global:LASTEXITCODE = 0
    & $Command
    $exitCode = $global:LASTEXITCODE
    if ($exitCode -ne 0) { throw "$Description failed with exit code $exitCode." }
}

function Get-ProjectGenerationFingerprint {
    param([string]$Root = (Get-RepositoryRoot))

    $project = Read-KeyValueFile (Join-Path $Root "Config\Project.conf")
    $sourceRoots = @(
        $project.CORE_DIRECTORY,
        $project.CLIENT_DIRECTORY,
        $project.HUB_DIRECTORY,
        $project.TESTS_DIRECTORY,
        "AssetTool",
        "KeireAssetWorker",
        "KeireEditorTests",
        "KeireHubRuntime",
        "KeireHubTests",
        "KeireHubWorker",
        "KeireRenderTests",
        "KeireRuntime",
        "KeireManaged",
        "KeireManaged.Tests",
        "SourceModules",
        "Scripts\Premake"
    )
    $inventory = foreach ($sourceRoot in $sourceRoots) {
        $absoluteRoot = Join-Path $Root $sourceRoot
        if (-not (Test-Path -LiteralPath $absoluteRoot -PathType Container)) { continue }
        Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File | Where-Object {
            $_.Extension -in @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".lua", ".cs", ".csproj") -and
                $_.FullName -notmatch '[\\/](?:bin|obj)[\\/]'
        } | ForEach-Object {
            $_.FullName.Substring($Root.ToString().Length).TrimStart("\", "/").Replace("\", "/")
        }
    }
    $premakeSearchRoots = Get-ChildItem -LiteralPath $Root -Directory | Where-Object {
        $_.Name -notin @(".git", "Build", "Vendor", "Tools")
    }
    $premakeSupportInputs = Get-ChildItem -LiteralPath (Join-Path $Root "Scripts\Premake") `
        -Recurse -Filter "*.lua" -File | ForEach-Object FullName
    $generationInfrastructureInputs = @(
        "Scripts\Unix\common.sh", "Scripts\Windows\common.ps1",
        "Scripts\Linux\bootstrap.sh", "Scripts\Linux\generate.sh",
        "Scripts\Mac\bootstrap.sh", "Scripts\Mac\generate.sh",
        "Scripts\Windows\bootstrap.ps1", "Scripts\Windows\generate.ps1",
        "Scripts\Unix\dependencies.sh", "Scripts\Windows\dependencies.ps1",
        "Scripts\Unix\shader-compiler.sh", "Scripts\Windows\shader-compiler.ps1",
        "Scripts\Unix\coral.sh", "Scripts\Windows\coral.ps1",
        "Scripts\Unix\ffmpeg.sh", "Scripts\Windows\ffmpeg.ps1",
        "Scripts\Unix\vendor.sh", "Scripts\Linux\vendor.sh", "Scripts\Mac\vendor.sh",
        "Scripts\Windows\vendor.ps1", "Scripts\patch-ninja-depfiles.py",
        "Scripts\patch-ninja-compiler-cache.py"
    ) | ForEach-Object { Join-Path $Root $_ }
    $generationInfrastructureInputs += Get-ChildItem -LiteralPath (Join-Path $Root "Scripts\Dependencies") `
        -Recurse -File | ForEach-Object FullName
    $premakeInputs = @(
        (Join-Path $Root "premake5.lua"),
        (Join-Path $Root "Config\Project.conf"),
        (Join-Path $Root "Config\Dependencies.lock")
    ) + @($premakeSearchRoots | ForEach-Object {
        Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter "premake5.lua" -File | ForEach-Object FullName
    }) + @($premakeSupportInputs) + @($generationInfrastructureInputs)

    $lines = @($inventory | Sort-Object -Unique)
    foreach ($path in $premakeInputs | Sort-Object -Unique) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $relative = $path.Substring($Root.ToString().Length).TrimStart("\", "/").Replace("\", "/")
            $lines += "$relative|$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)"
        }
    }

    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-CMakeExecutable {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $installed = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $installed) { return $installed }
    return $null
}

function Get-PythonInvocation {
    # Prefer the Windows launcher. The Microsoft Store installs an executable
    # alias named python.exe that Get-Command can resolve even when Python is
    # not installed; invoking that alias only prints a Store prompt and exits.
    foreach ($candidate in @(@{ Name = "py"; PrefixArguments = @("-3") },
            @{ Name = "python"; PrefixArguments = @() })) {
        $command = Get-Command $candidate.Name -ErrorAction SilentlyContinue
        if (-not $command) { continue }
        if ($candidate.Name -eq "python" -and
            $command.Source -like "*\Microsoft\WindowsApps\python.exe") { continue }
        $prefixArguments = @($candidate.PrefixArguments)
        & $command.Source @prefixArguments -c "import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)" `
            2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            return [PSCustomObject]@{
                Executable = $command.Source
                PrefixArguments = $prefixArguments
            }
        }
    }
    throw "Python 3 is required. Install it or make the Windows py launcher available."
}

function Get-GitWorktreeRoot {
    param([string]$Path)
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return $null }
    try {
        $topLevel = (& git -C $Path rev-parse --show-toplevel 2>$null) -join ""
    }
    catch {
        return $null
    }
    if ($LASTEXITCODE -ne 0 -or -not $topLevel) { return $null }
    return (Resolve-Path $topLevel)
}

function Get-GitHeadCommit {
    param([string]$Path, [string]$Fallback = "uncommitted")
    try {
        $commit = (& git -C $Path rev-parse --verify HEAD 2>$null) -join ""
    }
    catch {
        return $Fallback
    }
    if ($LASTEXITCODE -ne 0 -or -not $commit) { return $Fallback }
    return $commit.Trim()
}

function Test-GitRepository {
    param([string]$Path)
    try {
        $inside = (& git -C $Path rev-parse --is-inside-work-tree 2>$null) -join ""
    }
    catch {
        return $false
    }
    return $LASTEXITCODE -eq 0 -and $inside.Trim() -eq "true"
}

function Get-WindowsPackageWorktreePolicy {
    param([string]$Root, [switch]$AllowDirty, [switch]$CI)

    if ($CI -and $AllowDirty) { throw "-AllowDirty cannot be used in CI." }
    if (-not (Test-GitRepository $Root)) { throw "Release packaging requires a Git working tree." }

    $status = (& git -C $Root status --porcelain --untracked-files=normal 2>$null) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "Unable to inspect the package worktree at '$Root'." }
    $dirty = -not [string]::IsNullOrWhiteSpace($status)
    if ($dirty -and -not $AllowDirty) {
        throw "Release packaging requires a clean worktree. Use -AllowDirty only for a local development artifact."
    }

    return [pscustomobject]@{
        Dirty = $dirty
        DevelopmentArtifact = ($dirty -and [bool]$AllowDirty)
    }
}

function ConvertTo-MacroPrefix {
    param([string]$Identifier)
    $value = [regex]::Replace($Identifier, '([A-Z]+)([A-Z][a-z])', '$1_$2')
    $value = [regex]::Replace($value, '([a-z0-9])([A-Z])', '$1_$2')
    return $value.ToUpperInvariant()
}

function Copy-WindowsTrackedTree {
    param([string]$RepositoryRoot, [string]$RelativeSource, [string]$Destination)

    if (-not (Test-GitRepository $RepositoryRoot)) {
        throw "Tracked package copies require a Git working tree: $RepositoryRoot"
    }

    $trackedFiles = @(& git -c core.quotepath=false -C $RepositoryRoot ls-files -- $RelativeSource)
    if ($LASTEXITCODE -ne 0 -or $trackedFiles.Count -eq 0) {
        throw "No tracked files were found for package source '$RelativeSource'."
    }

    New-Item -ItemType Directory -Force $Destination | Out-Null
    $prefix = $RelativeSource.TrimEnd('/', '\') + "/"
    $copied = 0
    foreach ($trackedFile in $trackedFiles) {
        if (-not $trackedFile.StartsWith($prefix, [StringComparison]::Ordinal)) {
            throw "Tracked package path escaped '$RelativeSource': $trackedFile"
        }

        $source = Join-Path $RepositoryRoot $trackedFile
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $relativePath = $trackedFile.Substring($prefix.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
        $target = Join-Path $Destination $relativePath
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        ++$copied
    }
    if ($copied -eq 0) { throw "No present tracked files were found for package source '$RelativeSource'." }
}

function Test-WindowsGeneratedPackagePath {
    param([string]$RelativePath)

    $normalized = $RelativePath.Replace('\', '/')
    $segments = $normalized.Split('/', [StringSplitOptions]::RemoveEmptyEntries)
    for ($index = 0; $index -lt $segments.Count; ++$index) {
        $segment = $segments[$index]
        if ($segment -eq "Build" -and $index -eq 2 -and $segments[0] -eq "include") { continue }
        if ($segment -eq "Build" -and $segments.Count -gt 3 -and $segments[0] -eq "bin" -and
            $segments[1] -eq "Managed" -and $segments[2] -eq "Dotnet") { continue }
        if ($segment -in @("Library", "Logs", "Build", "Temp", "SceneRecovery", "Recovery")) { return $true }
    }

    $name = if ($segments.Count -gt 0) { $segments[-1] } else { "" }
    return $name -match '(?i)(^|[._-])recovery([._-]|$)' -or $name -match '(?i)\.tmp$'
}

function Assert-WindowsPackageGeneratedDataFree {
    param([string]$Stage)

    $stageRoot = (Resolve-Path -LiteralPath $Stage).Path.TrimEnd('\') + '\'
    foreach ($entry in Get-ChildItem -LiteralPath $Stage -Force -Recurse -ErrorAction SilentlyContinue) {
        if (-not $entry.FullName.StartsWith($stageRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Package entry escaped its staging root: $($entry.FullName)"
        }
        $relativePath = $entry.FullName.Substring($stageRoot.Length)
        if (Test-WindowsGeneratedPackagePath $relativePath) {
            throw "Package contains generated workspace data: $relativePath"
        }
    }
}

function Assert-WindowsPackageArchiveGeneratedDataFree {
    param([string]$Archive)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (Test-WindowsGeneratedPackagePath $entry.FullName) {
                throw "Package archive contains generated workspace data: $($entry.FullName)"
            }
        }
    }
    finally {
        $zip.Dispose()
    }
}

function Compress-WindowsArchive {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePattern,
        [Parameter(Mandatory = $true)][string]$Destination,
        [int]$MaximumAttempts = 5,
        [int]$RetryDelayMilliseconds = 500
    )

    if ($MaximumAttempts -lt 1) { throw "Archive compression requires at least one attempt." }
    if ($RetryDelayMilliseconds -lt 0) { throw "Archive compression retry delay cannot be negative." }
    for ($attempt = 1; $attempt -le $MaximumAttempts; ++$attempt) {
        Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
        try {
            Compress-Archive -Path $SourcePattern -DestinationPath $Destination -Force -ErrorAction Stop
            return
        }
        catch {
            if ($attempt -eq $MaximumAttempts) { throw }
            Start-Sleep -Milliseconds $RetryDelayMilliseconds
        }
    }
}

function Get-WindowsRequiredPackagePaths {
    param([string]$ClientTarget, [string]$HubTarget, [string]$CoreTarget, [string]$Namespace)
    @(
        "bin\$ClientTarget.exe", "bin\$HubTarget.exe", "bin\$($Namespace)AssetTool.exe", "bin\$($Namespace)AssetWorker.exe", "bin\$($Namespace)Runtime.exe", "bin\KeireShaderCompiler.exe", "bin\dxcompiler.dll", "bin\dxil.dll", "bin\nethost.dll", "bin\Managed\Coral.Managed.dll", "bin\Managed\Keire.Managed.dll", "lib\$CoreTarget.lib", "lib\$($Namespace)ImGui.lib", "lib\$($Namespace)Zstd.lib", "Config\Client.json", "include\$Namespace\Core.h", "include\$Namespace\Log.h",
        "include\$Namespace\Api.h", "include\$Namespace\Application.h", "include\$Namespace\Assert.h", "include\$Namespace\BuildInfo.h",
        "include\$Namespace\EntryPoint.h", "include\$Namespace\Event.h", "include\$Namespace\Layer.h", "include\$Namespace\Ref.h", "include\$Namespace\Undo.h",
        "include\$Namespace\Time.h", "include\$Namespace\Math\Math.h", "include\$Namespace\ECS\Component.h", "include\$Namespace\ECS\Entity.h", "include\$Namespace\ECS\Components\TransformComponent.h", "include\$Namespace\ECS\Components\DirectionalLightComponent.h", "include\$Namespace\ECS\Components\AudioComponents.h", "include\$Namespace\ECS\Components\RuntimeUiComponents.h", "include\$Namespace\ECS\Components\CameraComponent.h", "include\$Namespace\ECS\Components\MeshRendererComponent.h", "include\$Namespace\Rendering\RenderSystem.h", "include\$Namespace\Assets\Asset.h", "include\$Namespace\Assets\AssetSystem.h", "include\$Namespace\Assets\AssetPipeline.h", "include\$Namespace\Assets\InputActionAsset.h", "include\$Namespace\Assets\RenderingAssets.h", "include\$Namespace\Input\Input.h", "include\$Namespace\Project\Project.h", "include\$Namespace\Scenes\Scene.h", "include\$Namespace\Scenes\SceneAsset.h", "include\$Namespace\Scenes\SceneSystem.h", "include\$Namespace\Ui.h", "include\$Namespace\UiWorkspace.h", "include\$Namespace\Window.h", "include\$Namespace\WindowConfig.h", "samples\KeireSandbox\ProjectSettings\Project.keireproject", "samples\KeireSandbox\ProjectSettings\Rendering.keiresettings", "samples\KeireSandbox\Assets\Input\DefaultInput.keireinput", "samples\KeireSandbox\Assets\Scenes\SampleScene.keirescene", "samples\KeireSandbox\Assets\Shaders\DefaultUnlit.keireshader", "samples\KeireSandbox\Assets\Shaders\DefaultUnlit.hlsl", "samples\KeireSandbox\Assets\Materials\DefaultUnlit.keirematerial",
        "third-party\licenses\spdlog-LICENSE.txt",
        "third-party\licenses\fmt-LICENSE.rst", "third-party\licenses\doctest-LICENSE.txt",
        "third-party\licenses\nlohmann-json-LICENSE.MIT.txt", "third-party\licenses\dear-imgui-LICENSE.txt", "third-party\licenses\zstandard-LICENSE.txt", "third-party\licenses\entt-LICENSE.txt", "third-party\licenses\glm-COPYING.txt", "third-party\licenses\SDL-shadercross-LICENSE.txt", "third-party\licenses\DirectXShaderCompiler-LICENSE.txt", "third-party\licenses\DirectXShaderCompiler-ThirdPartyNotices.txt", "third-party\licenses\SPIRV-Cross-LICENSE.txt", "third-party\licenses\SPIRV-Headers-LICENSE.txt", "third-party\licenses\SPIRV-Tools-LICENSE.txt", "third-party\licenses\assimp-LICENSE.txt", "third-party\licenses\assimp-zlib-LICENSE.txt", "third-party\licenses\stb-LICENSE.txt", "third-party\licenses\Jolt-LICENSE.txt", "third-party\licenses\Recast-LICENSE.txt", "third-party\licenses\miniaudio-LICENSE.txt",
        "lib\assimp.lib", "lib\zlibstatic.lib", "lib\Jolt.lib", "lib\Recast.lib", "lib\Detour.lib", "lib\DetourCrowd.lib", "lib\DetourTileCache.lib", "lib\miniaudio.lib", "lib\Coral.Native.lib", "lib\nethost.lib",
        "third-party\licenses\Coral-LICENSE.txt", "third-party\licenses\dotnet-LICENSE.txt", "third-party\licenses\dotnet-ThirdPartyNotices.txt",
        "third-party\SDL3\include\SDL3\SDL.h",
        "third-party\SDL3\lib\SDL3-static.lib", "third-party\SDL3\cmake\SDL3Config.cmake",
        "third-party\SDL3\licenses\SDL3\LICENSE.txt",
        "examples\consumer\Source\Main.cpp", "examples\consumer\Client.json", "examples\consumer\CMakeLists.txt", "examples\consumer\README.md",
        "examples\managed-consumer\Source\ClientApplication.cpp", "examples\managed-consumer\CMakeLists.txt", "examples\managed-consumer\ManagedApiConsumer.csproj", "examples\managed-consumer\ManagedPresentationAssets.cs", "examples\managed-consumer\README.md",
        "examples\source-module\Source\ClientApplication.cpp", "examples\source-module\Source\GameplayModule.cpp", "examples\source-module\Include\GameplayModule.h", "examples\source-module\CMakeLists.txt", "examples\source-module\README.md",
        "Config\SourceModules.premake.lua", "Docs\PlayerBuilds.md", "Docs\Diagnostics\KEIRE-AUDIO-0001.md", "Docs\Diagnostics\KEIRE-REPLAY-0001.md", "Docs\Diagnostics\KEIRE-REPLAY-0002.md",
        "README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json"
    )
}

function Get-WindowsExecutableSubsystem {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 160 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw "Executable does not contain a valid DOS header: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt $stream.Length - 96) {
            throw "Executable contains an invalid PE header offset: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Executable does not contain a valid PE signature: $Path"
        }
        $optionalHeader = $peOffset + 24
        $stream.Position = $optionalHeader
        $magic = $reader.ReadUInt16()
        if ($magic -notin @(0x010B, 0x020B)) {
            throw "Executable contains an unsupported PE optional header: $Path"
        }
        $stream.Position = $optionalHeader + 68
        return $reader.ReadUInt16()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Invoke-WindowsExecutableCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @()
    )

    $standardOutput = [IO.Path]::GetTempFileName()
    $standardError = [IO.Path]::GetTempFileName()
    $process = $null
    try {
        $process = Start-Process -FilePath $Path -ArgumentList $Arguments -Wait -PassThru `
            -RedirectStandardOutput $standardOutput -RedirectStandardError $standardError
        $exitCode = $process.ExitCode
        $process.Dispose()
        $process = $null
        return [pscustomobject]@{
            ExitCode = $exitCode
            StandardOutput = [IO.File]::ReadAllText($standardOutput)
            StandardError = [IO.File]::ReadAllText($standardError)
        }
    }
    finally {
        if ($process) { $process.Dispose() }
        Remove-Item -LiteralPath $standardOutput, $standardError -Force -ErrorAction SilentlyContinue
    }
}

function Assert-WindowsPackageStage {
    param([string]$Stage, [string]$ClientTarget, [string]$HubTarget, [string]$CoreTarget, [string]$Namespace)
    $required = Get-WindowsRequiredPackagePaths $ClientTarget $HubTarget $CoreTarget $Namespace
    foreach ($path in $required) {
        if (-not (Test-Path (Join-Path $Stage $path) -PathType Leaf)) { throw "Package is missing required content: $path" }
    }
    if (Test-Path (Join-Path $Stage "include\KeireInternal")) {
        throw "Package contains private KeireInternal headers."
    }
    if (Test-Path (Join-Path $Stage "third-party\spdlog")) {
        throw "Package contains private spdlog headers."
    }
    if ((Test-Path (Join-Path $Stage "third-party\assimp")) -or
        (Test-Path (Join-Path $Stage "third-party\stb")) -or
        (Test-Path (Join-Path $Stage "third-party\SDL3\include\Jolt")) -or
        (Test-Path (Join-Path $Stage "third-party\SDL3\include\recastnavigation")) -or
        (Test-Path (Join-Path $Stage "third-party\SDL3\include\miniaudio"))) {
        throw "Package contains private implementation headers."
    }
    if (-not (Get-ChildItem (Join-Path $Stage "lib\cmake") -Filter "*Config.cmake" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        throw "Package is missing its CMake package configuration."
    }
    Assert-WindowsPackageGeneratedDataFree $Stage
}

function Get-WindowsRequiredHubContentPaths {
    @(
        "content\Content\en-US.json", "content\Licenses\catalog.json",
        "content\Fonts\Inter-OFL.txt", "content\Fonts\Inter-Variable.ttf",
        "content\Fonts\Material-Symbols-Apache-2.0.txt",
        "content\Fonts\MaterialSymbolsRounded-Subset.ttf", "content\Fonts\SOURCES.md",
        "content\Templates\catalog.json", "content\Templates\Payloads\Empty\README.md",
        "content\Templates\Payloads\Starter3D\README.md",
        "content\Templates\Payloads\Starter3D\Assets\Shaders\StarterUnlit.hlsl",
        "content\Templates\Payloads\Starter3D\ProjectSettings\Rendering.keiresettings",
        "content\Templates\Payloads\Sandbox\README.md",
        "content\Templates\Payloads\Sandbox\Assets\Scripts\Gameplay.keireasm",
        "content\Templates\Payloads\Sandbox\Assets\Scripts\Runtime\FirstPersonCamera.cs",
        "content\Templates\Payloads\Sandbox\ProjectSettings\Scripting.keiresettings",
        "content\Templates\Thumbnails\empty.png", "content\Templates\Thumbnails\starter-3d.png",
        "content\Templates\Thumbnails\sandbox.png"
    )
}

function Get-WindowsRequiredEditorPackagePaths {
    param([string]$ClientTarget, [string]$HubTarget, [string]$CoreTarget, [string]$Namespace)

    $licenses = Get-WindowsRequiredPackagePaths $ClientTarget $HubTarget $CoreTarget $Namespace |
        Where-Object { $_.StartsWith("third-party\licenses\", [StringComparison]::OrdinalIgnoreCase) }
    @(
        "bin\$ClientTarget.exe", "bin\$($Namespace)AssetTool.exe", "bin\$($Namespace)AssetWorker.exe",
        "bin\$($Namespace)Runtime.exe", "bin\KeireShaderCompiler.exe",
        "bin\dxcompiler.dll", "bin\dxil.dll", "bin\nethost.dll", "bin\Managed\Coral.Managed.dll",
        "bin\Managed\Coral.Managed.deps.json", "bin\Managed\Coral.Managed.runtimeconfig.json",
        "bin\Managed\Keire.Managed.dll", "bin\Managed\Dotnet\dotnet.exe", "Config\Client.json",
        "Config\Branding\Keire.png", "Config\Marketplace\trusted-marketplace-key.json",
        "Config\Marketplace\trusted-marketplace-keys.json",
        "content\Fonts\Inter-Variable.ttf", "content\Fonts\MaterialSymbolsRounded-Subset.ttf",
        "content\Fonts\Inter-OFL.txt", "content\Fonts\Material-Symbols-Apache-2.0.txt",
        "content\Fonts\SOURCES.md",
        "bin\libsodium.dll", "third-party\licenses\libsodium-LICENSE.txt",
        "samples\KeireSandbox\ProjectSettings\Project.keireproject",
        "samples\KeireSandbox\Assets\Scenes\SampleScene.keirescene", "Docs\PlayerBuilds.md", "README.md",
        "CHANGELOG.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json",
        "Config\SourceModules.premake.lua", "editor-package.json", "Launch-KeireEditor.cmd"
    ) + @($licenses)
}

function Assert-WindowsEditorPackageStage {
    param([string]$Stage, [string]$ClientTarget, [string]$HubTarget, [string]$CoreTarget, [string]$Namespace)

    foreach ($path in (Get-WindowsRequiredEditorPackagePaths $ClientTarget $HubTarget $CoreTarget $Namespace)) {
        if (-not (Test-Path -LiteralPath (Join-Path $Stage $path) -PathType Leaf)) {
            throw "Editor package is missing required content: $path"
        }
    }
    $editorExecutable = Join-Path $Stage "bin\$ClientTarget.exe"
    if ((Get-WindowsExecutableSubsystem $editorExecutable) -ne 2) {
        throw "Editor package executable must use the Windows GUI subsystem: bin\$ClientTarget.exe"
    }
    foreach ($hubPath in @(
            "bin\$HubTarget.exe", "bin\$($Namespace)HubWorker.exe", "content\Content", "content\Licenses",
            "content\Templates", "Launch-KeireHub.cmd",
            "hub-package.json", "Config\Distribution.json", "Config\Supabase.json", "Docs\ProjectHub.md")) {
        if (Test-Path -LiteralPath (Join-Path $Stage $hubPath)) {
            throw "Editor package contains Hub-only content: $hubPath"
        }
    }
    $launcher = Get-Content -LiteralPath (Join-Path $Stage "Launch-KeireEditor.cmd") -Raw
    if ($launcher.IndexOf("bin\$ClientTarget.exe", [StringComparison]::OrdinalIgnoreCase) -lt 0 -or
        $launcher.IndexOf("bin\$HubTarget.exe", [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Editor package launcher must target only bin\$ClientTarget.exe."
    }
    $manifest = Get-Content -LiteralPath (Join-Path $Stage "editor-package.json") -Raw | ConvertFrom-Json
    $manifestPropertyNames = @($manifest.PSObject.Properties.Name)
    $entrypointNames = @($manifest.entrypoints.PSObject.Properties.Name)
    $legacyFields = @($manifest.compatibility.legacyTopLevelFields)
    $requiredLegacyFields = @(
        "artifact", "project", "version", "commit", "dirty", "developmentArtifact", "platform",
        "architecture", "configuration", "launcher", "bundledDotnetSdk", "buildManifest"
    )
    $missingLegacyFields = @($requiredLegacyFields | Where-Object { $legacyFields -notcontains $_ })
    if ($manifest.schemaVersion -ne 2 -or $manifest.artifact -ne "editor" -or
        $manifest.compatibility.legacySchemaVersion -ne 1 -or
        $missingLegacyFields.Count -ne 0 -or $manifest.launcher -ne "Launch-KeireEditor.cmd" -or
        -not $manifest.bundledDotnetSdk -or $manifest.buildManifest -ne "build-manifest.json" -or
        $manifest.entrypoints.editor -ne "bin/$ClientTarget.exe" -or $entrypointNames -contains "hub" -or
        $entrypointNames -contains "worker" -or $manifestPropertyNames -notcontains "packagedTemplates" -or
        @($manifest.packagedTemplates).Count -ne 0 -or $manifestPropertyNames -contains "templateCatalog") {
        throw "Editor package manifest must preserve schema compatibility without Hub-owned content or entrypoints."
    }
    $dotnetSdk = Get-ChildItem -LiteralPath (Join-Path $Stage "bin\Managed\Dotnet\sdk") -Directory `
        -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^10\.' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $dotnetSdk -or $manifest.bundledDotnetSdk -ne $dotnetSdk.Name) {
        throw "Editor package does not contain its declared .NET 10 SDK."
    }
    foreach ($pattern in @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll")) {
        if (-not (Get-ChildItem -LiteralPath (Join-Path $Stage "bin") -Filter $pattern -File |
            Select-Object -First 1)) {
            throw "Editor package is missing an FFmpeg runtime matching '$pattern'."
        }
    }
    foreach ($developmentDirectory in @("include", "lib", "examples")) {
        if (Test-Path -LiteralPath (Join-Path $Stage $developmentDirectory)) {
            throw "Editor package contains SDK-only content: $developmentDirectory"
        }
    }
    Assert-WindowsPackageGeneratedDataFree $Stage
}

function Get-WindowsRequiredHubPackagePaths {
    param([string]$HubTarget, [string]$Namespace)

    @(
        "bin\$HubTarget.exe", "bin\$($Namespace)HubWorker.exe", "Config\Branding\Keire.png",
        "Config\Marketplace\trusted-marketplace-key.json", "Config\Marketplace\trusted-marketplace-keys.json",
        "Config\SourceModules.premake.lua", "Config\Distribution.json", "Config\Supabase.json",
        "Docs\ProjectHub.md", "Samples\KeireSandbox\ProjectSettings\Project.keireproject", "README.md",
        "CHANGELOG.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "hub-package.json", "Launch-KeireHub.cmd",
        "bin\libsodium.dll", "third-party\licenses\libsodium-LICENSE.txt",
        "third-party\licenses\spdlog-LICENSE.txt", "third-party\licenses\fmt-LICENSE.rst",
        "third-party\licenses\nlohmann-json-LICENSE.MIT.txt", "third-party\licenses\dear-imgui-LICENSE.txt",
        "third-party\licenses\zstandard-LICENSE.txt", "third-party\licenses\entt-LICENSE.txt",
        "third-party\licenses\glm-COPYING.txt", "third-party\licenses\SDL3-LICENSE.txt"
    ) + @(Get-WindowsRequiredHubContentPaths)
}

function Assert-WindowsHubPackageStage {
    param([string]$Stage, [string]$HubTarget, [string]$ClientTarget, [string]$Namespace)

    foreach ($path in (Get-WindowsRequiredHubPackagePaths $HubTarget $Namespace)) {
        if (-not (Test-Path -LiteralPath (Join-Path $Stage $path) -PathType Leaf)) {
            throw "Hub package is missing required content: $path"
        }
    }
    if ((Get-WindowsExecutableSubsystem (Join-Path $Stage "bin\$HubTarget.exe")) -ne 2) {
        throw "Hub package executable must use the Windows GUI subsystem: bin\$HubTarget.exe"
    }
    if ((Get-WindowsExecutableSubsystem (Join-Path $Stage "bin\$($Namespace)HubWorker.exe")) -ne 3) {
        throw "Hub package worker must use the Windows console subsystem: bin\$($Namespace)HubWorker.exe"
    }
    foreach ($editorPath in @(
            "bin\$ClientTarget.exe", "bin\$($Namespace)AssetTool.exe", "bin\$($Namespace)AssetWorker.exe",
            "bin\$($Namespace)Runtime.exe", "bin\KeireShaderCompiler.exe", "bin\Managed\Dotnet\sdk")) {
        if (Test-Path -LiteralPath (Join-Path $Stage $editorPath)) {
            throw "Hub package contains editor-only content: $editorPath"
        }
    }
    foreach ($developmentDirectory in @("include", "lib", "examples")) {
        if (Test-Path -LiteralPath (Join-Path $Stage $developmentDirectory)) {
            throw "Hub package contains SDK-only content: $developmentDirectory"
        }
    }
    Assert-WindowsPackageGeneratedDataFree $Stage
}

function Resolve-WindowsToolset {
    param([string]$Generator, [string]$Toolset)
    if ($Toolset -ne "default") { return $Toolset }
    if ($Generator -eq "gmake") { return "gcc" }
    return "msc"
}

function Get-NativeArchitecture {
    if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq
        [System.Runtime.InteropServices.Architecture]::Arm64) {
        return "ARM64"
    }
    return "x86_64"
}

function Normalize-Architecture {
    param([string]$Architecture)

    switch ($Architecture.ToLowerInvariant()) {
        "x64" { return "x86_64" }
        "amd64" { return "x86_64" }
        "x86_64" { return "x86_64" }
        "arm64" { return "ARM64" }
        "aarch64" { return "ARM64" }
        default { throw "Unsupported architecture '$Architecture'. Expected x86_64 or ARM64." }
    }
}

function Get-MSBuildPlatform {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "ARM64" }
    return "x64"
}

function Get-PremakeArchitecture {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "aarch64" }
    return "x86_64"
}

function Get-ArchitectureOutputName {
    param([string]$Architecture)
    if ((Normalize-Architecture $Architecture) -eq "ARM64") { return "AARCH64" }
    return "x86_64"
}

function Remove-GeneratedBinaryDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $pathSeparators = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd($pathSeparators)
    $resolvedBase = [IO.Path]::GetFullPath((Join-Path $resolvedRoot "Build\Bin")).TrimEnd($pathSeparators)
    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd($pathSeparators)
    $separator = [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedBase.StartsWith("$resolvedRoot$separator", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a generated binary root outside the repository: $resolvedBase."
    }
    if (-not $resolvedPath.StartsWith("$resolvedBase$separator", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove generated binaries outside $resolvedBase`: $resolvedPath."
    }

    $relativePath = $resolvedPath.Substring($resolvedRoot.Length).TrimStart($pathSeparators)
    $currentPath = $resolvedRoot
    foreach ($component in @($relativePath -split '[\\/]' | Where-Object { $_ })) {
        $currentPath = Join-Path $currentPath $component
        $item = Get-Item -LiteralPath $currentPath -Force -ErrorAction SilentlyContinue
        if ($item -and (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw "Refusing to remove a reparse-point generated binary path: $currentPath."
        }
    }

    $target = Get-Item -LiteralPath $resolvedPath -Force -ErrorAction SilentlyContinue
    if (-not $target) { return }
    if (-not $target.PSIsContainer) {
        throw "Generated binary output is not a directory: $resolvedPath."
    }
    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

function Remove-IncompatibleBuildBinaries {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [Parameter(Mandatory = $true)][ValidateSet("msc", "gcc", "clang")][string]$Toolset,
        [Parameter(Mandatory = $true)][string]$IdentityStamp
    )

    $normalizedArchitecture = Normalize-Architecture $Architecture
    $preserveOutputs = $false
    if (Test-Path -LiteralPath $IdentityStamp -PathType Leaf) {
        $parts = (Get-Content -LiteralPath $IdentityStamp -Raw).Trim() -split '\|'
        if ($parts.Count -eq 6 -and -not [string]::IsNullOrWhiteSpace($parts[0]) -and
            -not [string]::IsNullOrWhiteSpace($parts[5])) {
            try {
                $priorArchitecture = Normalize-Architecture $parts[1]
                $preserveOutputs = $priorArchitecture -eq $normalizedArchitecture -and $parts[2] -eq $Toolset
            }
            catch {
                $preserveOutputs = $false
            }
        }
    }
    if ($preserveOutputs) { return }

    $outputArchitecture = Get-ArchitectureOutputName $normalizedArchitecture
    $targets = @("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage") |
        ForEach-Object { Join-Path $Root "Build\Bin\$_-windows-$outputArchitecture" } |
        Where-Object { Get-Item -LiteralPath $_ -Force -ErrorAction SilentlyContinue }
    if ($targets.Count -eq 0) { return }
    Write-Host "==> Removing binaries with unknown or incompatible toolset provenance for windows-$outputArchitecture"
    foreach ($target in $targets) {
        Remove-GeneratedBinaryDirectory -Root $Root -Path $target
    }
}

function Get-VisualStudioMajorVersion {
    param([string]$Generator)
    switch ($Generator) {
        "vs2019" { return 16 }
        "vs2022" { return 17 }
        "vs2026" { return 18 }
        default { return $null }
    }
}

function Get-VSInstallation {
    param([int]$MajorVersion)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $json = (& $vswhere -latest -products * -version $range -format json) -join "`n"
    if (-not $json) { return $null }
    return @($json | ConvertFrom-Json)[0]
}

function Get-VSBuildEnvironment {
    param([int]$MajorVersion)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found. Bootstrap the requested Visual Studio generator first."
    }

    $range = "[$MajorVersion.0,$($MajorVersion + 1).0)"
    $installationJson = (& $vswhere -products * -version $range `
        -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Workload.NativeDesktop -format json) -join "`n"
    $installations = if ($installationJson) { @($installationJson | ConvertFrom-Json) } else { @() }

    foreach ($installation in $installations) {
        $installationPath = $installation.installationPath
        $msbuild = @(
            (Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path $installationPath "MSBuild\15.0\Bin\MSBuild.exe")
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1

        $vcTargetsPath = Join-Path $installationPath "MSBuild\Microsoft\VC\v$($MajorVersion)0"
        if ($msbuild -and (Test-Path (Join-Path $vcTargetsPath "Microsoft.Cpp.Default.props"))) {
            return [pscustomobject]@{
                InstallationPath = $installationPath
                InstallationVersion = $installation.installationVersion
                ProductId = $installation.productId
                DisplayName = $installation.displayName
                MSBuild = $msbuild
                VCTargetsPath = $vcTargetsPath.Replace("\", "/") + "/"
                VsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
            }
        }
    }

    throw "A complete Visual Studio $MajorVersion C++ build environment was not found."
}

function Enter-VSDeveloperEnvironment {
    param(
        [int]$MajorVersion,
        [string]$Architecture
    )

    $environment = Get-VSBuildEnvironment $MajorVersion
    $targetArchitecture = if ((Normalize-Architecture $Architecture) -eq "ARM64") { "arm64" } else { "amd64" }
    $environmentKey = "$MajorVersion-$targetArchitecture"
    if ($env:KEIRE_VSDEV_ENVIRONMENT_KEY -eq $environmentKey -and $env:VCToolsInstallDir -and
        $env:WindowsSdkDir -and $env:INCLUDE) {
        return $environment
    }

    $output = & $env:ComSpec /s /c "`"$($environment.VsDevCmd)`" -no_logo -arch=$targetArchitecture -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio developer environment setup failed with exit code $LASTEXITCODE."
    }
    foreach ($line in $output) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            [System.Environment]::SetEnvironmentVariable(
                $line.Substring(0, $separator),
                $line.Substring($separator + 1),
                "Process")
        }
    }
    [System.Environment]::SetEnvironmentVariable("KEIRE_VSDEV_ENVIRONMENT_KEY", $environmentKey, "Process")
    return $environment
}

function Get-MSVCASanRuntimeDirectory {
    param(
        [int]$MajorVersion,
        [string]$Architecture
    )

    $environment = Get-VSBuildEnvironment $MajorVersion
    $targetDirectory = if ((Normalize-Architecture $Architecture) -eq "ARM64") { "arm64" } else { "x64" }
    $runtimePattern = if ($targetDirectory -eq "arm64") { "clang_rt.asan_dynamic-aarch64.dll" } else { "clang_rt.asan_dynamic-x86_64.dll" }
    $toolsets = Get-ChildItem (Join-Path $environment.InstallationPath "VC\Tools\MSVC") -Directory |
        Sort-Object { [version]$_.Name } -Descending

    foreach ($toolset in $toolsets) {
        foreach ($hostArchitecture in @("Hostx64", "Hostx86")) {
            $runtimeDirectory = Join-Path $toolset.FullName "bin\$hostArchitecture\$targetDirectory"
            if (Test-Path (Join-Path $runtimeDirectory $runtimePattern)) {
                return $runtimeDirectory
            }
        }
    }
    throw "The MSVC AddressSanitizer runtime was not found for $Architecture."
}

function Assert-SupportedBuildCombination {
    param(
        [string]$Generator,
        [string]$Configuration,
        [string]$Architecture,
        [string]$Toolset
    )

    $Architecture = Normalize-Architecture $Architecture
    if ($Generator -like "vs*" -and $Toolset -eq "gcc") {
        throw "Visual Studio generators do not support the GCC toolset."
    }
    if ($Generator -eq "gmake" -and $Toolset -notin @("default", "gcc")) {
        throw "Windows GNU Make supports only the default or GCC toolset."
    }
    if ($Generator -eq "gmake" -and $Architecture -eq "ARM64") {
        throw "Windows GNU Make ARM64 is not supported by this template."
    }
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -in @("default", "msc"))
    if ($usesMSVC -and $Configuration -in @("DebugUBSan", "DebugTSan")) {
        throw "$Configuration is not supported by MSVC. Use Linux or macOS with GCC/Clang."
    }
    if ($Configuration -eq "Coverage" -and ($Generator -ne "ninja" -or $Toolset -ne "clang")) {
        throw "Coverage requires the Ninja generator and Clang toolset."
    }
}

function Get-NinjaExecutable {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) { return $link }
    throw "Ninja was not found. Run bootstrap for the Ninja generator."
}

function Resolve-CompilerCache {
    param(
        [Parameter(Mandatory = $true)][string]$Generator,
        [ValidateSet("auto", "off", "sccache")][string]$CompilerCache = "auto"
    )

    if ($Generator -notin @("ninja", "compilecommands") -or $CompilerCache -eq "off") {
        return "off"
    }
    $available = Get-Command sccache -ErrorAction SilentlyContinue
    if ($CompilerCache -eq "sccache" -and -not $available) {
        throw "sccache was requested but is not available in PATH."
    }
    return $(if ($available) { "sccache" } else { "off" })
}

function Add-LLVMToPath {
    $bin = "C:\Program Files\LLVM\bin"
    if (-not (Test-Path (Join-Path $bin "clang.exe"))) { throw "LLVM was not found under $bin." }
    $shimDirectory = Join-Path (Get-RepositoryRoot) "Tools\Windows\llvm-bin"
    New-Item -ItemType Directory -Force $shimDirectory | Out-Null
    $llvmAr = Join-Path $bin "llvm-ar.exe"
    if (Test-Path $llvmAr) { Copy-Item $llvmAr (Join-Path $shimDirectory "ar.exe") -Force }
    $env:PATH = "$shimDirectory;$bin;$env:PATH"
    return $bin
}

function Add-MSYS2ToPath {
    $bin = @("C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin") |
        Where-Object { Test-Path (Join-Path $_ "g++.exe") } | Select-Object -First 1
    if (-not $bin) { throw "An MSYS2 GCC environment was not found under C:\msys64." }
    $env:PATH = "$bin;$env:PATH"
    return $bin
}

function Enter-WindowsToolEnvironment {
    param([string]$Generator, [string]$Toolset, [string]$Architecture)
    $resolved = Resolve-WindowsToolset $Generator $Toolset
    switch ($resolved) {
        "msc" {
            $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
            Enter-VSDeveloperEnvironment $majorVersion $Architecture | Out-Null
        }
        "clang" { Add-LLVMToPath | Out-Null }
        "gcc" { Add-MSYS2ToPath | Out-Null }
    }
    return $resolved
}

function Get-ManagedHostStagingTargets {
    param(
        [Parameter(Mandatory = $true)]$Project,
        [Parameter(Mandatory = $true)][string]$Target
    )

    $targets = [System.Collections.Generic.List[string]]::new()
    $editorDevTarget = "$($Project.PROJECT_NAMESPACE)EditorDev"
    if ($Target -in @($Project.CLIENT_TARGET, $Project.HUB_TARGET, $editorDevTarget)) {
        $targets.Add("$($Project.PROJECT_NAMESPACE)AssetTool")
        $targets.Add("$($Project.PROJECT_NAMESPACE)Runtime")
        $targets.Add($Project.CLIENT_TARGET)
    }
    if ($Target -ne $editorDevTarget) {
        $targets.Add($Target)
    }
    return @($targets | Select-Object -Unique)
}
