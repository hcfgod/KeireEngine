[CmdletBinding()]
param(
    [string]$Generator = "vs2022",
    [string]$Architecture = "",
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate,
    [switch]$AllowDirty,
    [switch]$StageOnly
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot
$WorkspaceLock = Enter-KeireWorkspaceLock -RepositoryRoot $Root -CommandName "package-hub"
try {
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$hubWorkerTarget = "$($Project.PROJECT_NAMESPACE)HubWorker"
$installWorkerTarget = "$($Project.PROJECT_NAMESPACE)InstallWorker"
$worktreePolicy = Get-WindowsPackageWorktreePolicy -Root $Root -AllowDirty:$AllowDirty -CI:$CI

Invoke-CheckedWindowsCommand { & (Join-Path $PSScriptRoot "build-info.ps1") } "Build metadata generation"
Invoke-CheckedWindowsCommand {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration Dist `
        -Architecture $Architecture -Toolset $Toolset -Target $Project.HUB_TARGET -CI:$CI `
        -Update:$Update -Generate:$Generate
} "Standalone Hub build"
Invoke-CheckedWindowsCommand {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration Dist `
        -Architecture $Architecture -Toolset $Toolset -Target $hubWorkerTarget -CI:$CI `
        -Update:$Update -Generate:$Generate
} "Hub package worker build"
Invoke-CheckedWindowsCommand {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration Dist `
        -Architecture $Architecture -Toolset $Toolset -Target $installWorkerTarget -CI:$CI `
        -Update:$Update -Generate:$Generate
} "Install transaction worker build"

$source = Join-Path $Root "Build\Bin\Dist-windows-$outputArchitecture\$($Project.HUB_TARGET)"
if (-not (Test-Path -LiteralPath (Join-Path $source "$($Project.HUB_TARGET).exe") -PathType Leaf)) {
    throw "The standalone Hub build output is missing: $source"
}
$hubWorkerSource = Join-Path $Root `
    "Build\Bin\Dist-windows-$outputArchitecture\$hubWorkerTarget\$hubWorkerTarget.exe"
if (-not (Test-Path -LiteralPath $hubWorkerSource -PathType Leaf)) {
    throw "The Hub package worker build output is missing: $hubWorkerSource"
}
$installWorkerSource = Join-Path $Root `
    "Build\Bin\Dist-windows-$outputArchitecture\$installWorkerTarget\$installWorkerTarget.exe"
if (-not (Test-Path -LiteralPath $installWorkerSource -PathType Leaf)) {
    throw "The install transaction worker build output is missing: $installWorkerSource"
}

$name = "$($Project.ARTIFACT_PREFIX)-hub-windows-$Architecture-Dist"
$distributionRoot = Join-Path $Root "Build\Distributions"
$stage = Join-Path $distributionRoot $name
$archive = Join-Path $Root "Artifacts\$name.zip"
$validationRoot = Join-Path $Root "Artifacts\$name-validation"
foreach ($path in @($stage, $validationRoot)) {
    $parent = if ($path -eq $stage) { $distributionRoot } else { Join-Path $Root "Artifacts" }
    $resolvedParent = [IO.Path]::GetFullPath($parent).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $resolvedPath = [IO.Path]::GetFullPath($path)
    if (-not $resolvedPath.StartsWith($resolvedParent + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a package path outside its output root: $resolvedPath"
    }
}
Remove-Item -LiteralPath $stage, $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archive, "$archive.sha256" -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $distributionRoot, (Join-Path $Root "Artifacts"), `
    (Join-Path $stage "bin"), (Join-Path $stage "Config\Branding"), `
    (Join-Path $stage "Config\Marketplace"), `
    (Join-Path $stage "content"), (Join-Path $stage "third-party\licenses") | Out-Null
Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination (Join-Path $stage "bin") -Recurse -Force
Copy-Item -LiteralPath $hubWorkerSource -Destination (Join-Path $stage "bin")
Copy-Item -LiteralPath $installWorkerSource -Destination (Join-Path $stage "bin")
$installWorkerText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes(
        (Join-Path $stage "bin\$installWorkerTarget.exe")))
if ($installWorkerText.Contains("KEIRE_INSTALL_WORKER_INTERRUPT_AFTER")) {
    throw "The Dist install worker contains test-only fault injection."
}
Get-ChildItem -LiteralPath (Join-Path $Root "KeireHubContent") -Force |
    Copy-Item -Destination (Join-Path $stage "content") -Recurse
Copy-WindowsTrackedTree $Root "Docs" (Join-Path $stage "Docs")
Copy-WindowsTrackedTree $Root "Samples/KeireSandbox" (Join-Path $stage "Samples\KeireSandbox") `
    -AdditionalRelativeFiles (Get-WindowsKeireSandboxUiPackageFiles)
Copy-Item -LiteralPath (Join-Path $Root "Config\Branding\Keire.png") `
    -Destination (Join-Path $stage "Config\Branding")
Copy-Item -LiteralPath (Join-Path $Root "Config\Marketplace\trusted-marketplace-key.json") `
    -Destination (Join-Path $stage "Config\Marketplace")
Copy-Item -LiteralPath (Join-Path $Root "Config\Marketplace\trusted-marketplace-keys.json") `
    -Destination (Join-Path $stage "Config\Marketplace")
Copy-Item -LiteralPath (Join-Path $Root "Config\SourceModules.premake.lua") `
    -Destination (Join-Path $stage "Config")
$python = Get-PythonInvocation
$pythonPrefix = @($python.PrefixArguments)
$supabaseConfiguration = Join-Path $Root "Config\Supabase.json"
Invoke-CheckedWindowsCommand {
    & $python.Executable @pythonPrefix (Join-Path $Root "Scripts\Packaging\validate-supabase-config.py") `
        --config $supabaseConfiguration
} "Supabase desktop configuration validation"
Copy-Item -LiteralPath (Join-Path $Root "Config\Supabase.json") `
    -Destination (Join-Path $stage "Config")
foreach ($file in @("README.md", "CHANGELOG.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md")) {
    Copy-Item -LiteralPath (Join-Path $Root $file) -Destination $stage
}

$dependencyInstall = Join-Path $Root "Build\Dependencies\windows-$outputArchitecture-$Toolset\Release\install"
$sodiumRuntime = Join-Path $dependencyInstall "bin\libsodium.dll"
$sodiumLicense = Join-Path $dependencyInstall "share\licenses\libsodium\LICENSE"
if (-not (Test-Path -LiteralPath $sodiumRuntime -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sodiumLicense -PathType Leaf)) {
    throw "The standalone Hub pinned libsodium runtime or license is missing. Run bootstrap first."
}
Copy-Item -LiteralPath $sodiumRuntime -Destination (Join-Path $stage "bin\libsodium.dll")
Copy-Item -LiteralPath $sodiumLicense `
    -Destination (Join-Path $stage "third-party\licenses\libsodium-LICENSE.txt")
$licenseSources = @(
    @("Vendor\spdlog\LICENSE", "spdlog-LICENSE.txt"),
    @("Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst", "fmt-LICENSE.rst"),
    @("Vendor\json\LICENSE.MIT", "nlohmann-json-LICENSE.MIT.txt"),
    @("Vendor\imgui\LICENSE.txt", "dear-imgui-LICENSE.txt"),
    @("Vendor\zstd\LICENSE", "zstandard-LICENSE.txt"),
    @("Vendor\entt\LICENSE", "entt-LICENSE.txt"),
    @("Vendor\glm\copying.txt", "glm-COPYING.txt"),
    @("Vendor\SDL\LICENSE.txt", "SDL3-LICENSE.txt"),
    @("Vendor\assimp\LICENSE", "assimp-LICENSE.txt"),
    @("Vendor\assimp\contrib\zlib\LICENSE", "assimp-zlib-LICENSE.txt"),
    @("Vendor\stb\LICENSE", "stb-LICENSE.txt"),
    @("Build\Dependencies\coral-patched\LICENSE", "Coral-LICENSE.txt")
)
foreach ($license in $licenseSources) {
    $licenseSource = Join-Path $Root $license[0]
    if (-not (Test-Path -LiteralPath $licenseSource -PathType Leaf)) {
        throw "The standalone Hub license source is missing: $licenseSource"
    }
    Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $stage "third-party\licenses\$($license[1])")
}
foreach ($licenseName in @("Jolt-LICENSE.txt", "Recast-LICENSE.txt", "miniaudio-LICENSE.txt")) {
    $licenseSource = Join-Path $dependencyInstall "share\licenses\keire\$licenseName"
    if (-not (Test-Path -LiteralPath $licenseSource -PathType Leaf)) {
        throw "The standalone Hub dependency license is missing: $licenseSource"
    }
    Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $stage "third-party\licenses")
}
$dotnetLicenseRoot = Join-Path $stage "bin\Managed\Dotnet"
if (Test-Path -LiteralPath (Join-Path $dotnetLicenseRoot "LICENSE.txt") -PathType Leaf) {
    Copy-Item -LiteralPath (Join-Path $dotnetLicenseRoot "LICENSE.txt") `
        -Destination (Join-Path $stage "third-party\licenses\dotnet-LICENSE.txt")
    Copy-Item -LiteralPath (Join-Path $dotnetLicenseRoot "ThirdPartyNotices.txt") `
        -Destination (Join-Path $stage "third-party\licenses\dotnet-ThirdPartyNotices.txt")
}

$launcher = "@echo off`r`nstart `"`" `"%~dp0bin\$($Project.HUB_TARGET).exe`" %*`r`n"
[IO.File]::WriteAllText((Join-Path $stage "Launch-KeireHub.cmd"), $launcher, [Text.ASCIIEncoding]::new())
$commit = Get-GitHeadCommit $Root "unknown"
$distributionWriter = Join-Path $Root "Scripts\Packaging\write-distribution-config.py"
$distributionArguments = @(
    $distributionWriter, "--output", (Join-Path $stage "Config\Distribution.json")
)
$distributionSource = Join-Path $Root "Config\Distribution.json"
$distributionServiceUrl = [string]$env:KEIRE_DISTRIBUTION_SERVICE_URL
$distributionTrustedKey = [string]$env:KEIRE_DISTRIBUTION_TRUSTED_KEY
$distributionTrustedKeys = [string]$env:KEIRE_DISTRIBUTION_TRUSTED_KEYS
$distributionMinimumSequence = [string]$env:KEIRE_DISTRIBUTION_MINIMUM_SEQUENCE
$trustedKeyPaths = if ($distributionTrustedKeys) {
    @($distributionTrustedKeys.Split([IO.Path]::PathSeparator, [StringSplitOptions]::RemoveEmptyEntries) |
        ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
elseif ($distributionTrustedKey) {
    @($distributionTrustedKey)
}
else {
    @()
}
$hasDistributionOverride = $distributionServiceUrl -or $trustedKeyPaths.Count -gt 0
if ($hasDistributionOverride) {
    $distributionArguments += @("--service-url", $distributionServiceUrl)
    foreach ($trustedKeyPath in $trustedKeyPaths) {
        $distributionArguments += @("--trusted-key", $trustedKeyPath)
    }
}
else {
    $distributionArguments += @("--source-config", $distributionSource)
}
if ($distributionMinimumSequence -and $hasDistributionOverride) {
    $distributionArguments += @("--minimum-sequence", $distributionMinimumSequence)
}
Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @distributionArguments } `
    "Hub distribution configuration generation"
$manifestWriter = Join-Path $Root "Scripts\Packaging\write-package-manifest.py"
$manifestArguments = @(
    $manifestWriter, "write", "--stage", $stage, "--output", "hub-package.json",
    "--artifact", "hub", "--package-prefix", $Project.ARTIFACT_PREFIX,
    "--project", $Project.PROJECT_IDENTIFIER, "--version", $Project.PROJECT_VERSION,
    "--channel", "Stable", "--commit", $commit,
    "--dirty", ([bool]$worktreePolicy.Dirty).ToString().ToLowerInvariant(),
    "--development-artifact", ([bool]$worktreePolicy.DevelopmentArtifact).ToString().ToLowerInvariant(),
    "--platform", "Windows", "--architecture", $outputArchitecture,
    "--configuration", "Dist", "--launcher", "Launch-KeireHub.cmd",
    "--module-definition", "Config/SourceModules.premake.lua", "--project-schema-minimum", "1",
    "--project-schema-maximum", "4", "--entrypoint", "hub=bin/$($Project.HUB_TARGET).exe",
    "--entrypoint", "worker=bin/$hubWorkerTarget.exe",
    "--template-catalog", "content/Templates/catalog.json", "--release-notes", "CHANGELOG.md"
)
$dotnetRuntime = Get-ChildItem -LiteralPath (Join-Path $dotnetLicenseRoot "shared\Microsoft.NETCore.App") `
    -Directory -ErrorAction SilentlyContinue | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if ($dotnetRuntime) {
    $manifestArguments += @("--toolchain", "dotnet-runtime|$($dotnetRuntime.Name)|bin/Managed/Dotnet")
}
Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
    "Standalone Hub package manifest generation"

Assert-WindowsHubPackageStage $stage $Project.HUB_TARGET $Project.CLIENT_TARGET $Project.PROJECT_NAMESPACE
$hubVersion = Invoke-WindowsExecutableCapture (Join-Path $stage "bin\$($Project.HUB_TARGET).exe") @("--version")
if ($hubVersion.ExitCode -ne 0 -or -not $hubVersion.StandardOutput.Contains($Project.PROJECT_VERSION)) {
    throw "Packaged standalone Hub version validation failed."
}
$workerHelp = Invoke-WindowsExecutableCapture (Join-Path $stage "bin\$hubWorkerTarget.exe") @("--help")
if ($workerHelp.ExitCode -ne 0 -or -not $workerHelp.StandardOutput.Contains("--request")) {
    throw "Packaged standalone Hub worker validation failed."
}

if ($StageOnly) {
    Write-Host "==> Standalone Hub package stage created: $stage"
    exit 0
}

Compress-WindowsArchive (Join-Path $stage "*") $archive
Assert-WindowsPackageArchiveGeneratedDataFree $archive
(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" |
    Set-Content "$archive.sha256" -Encoding ASCII
try {
    New-Item -ItemType Directory -Force $validationRoot | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $validationRoot -Force
    Assert-WindowsHubPackageStage $validationRoot $Project.HUB_TARGET $Project.CLIENT_TARGET `
        $Project.PROJECT_NAMESPACE
    Invoke-CheckedWindowsCommand {
        & $python.Executable @pythonPrefix $manifestWriter validate --stage $validationRoot `
            --manifest hub-package.json --artifact hub
    } "Extracted standalone Hub package manifest validation"
    $extractedWorkerHelp = Invoke-WindowsExecutableCapture `
        (Join-Path $validationRoot "bin\$hubWorkerTarget.exe") @("--help")
    if ($extractedWorkerHelp.ExitCode -ne 0 -or -not $extractedWorkerHelp.StandardOutput.Contains("--request")) {
        throw "Extracted standalone Hub worker validation failed."
    }
}
finally {
    Remove-Item -LiteralPath $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "==> Ready-to-run standalone Hub distribution: $stage"
Write-Host "==> Launch with: $(Join-Path $stage 'Launch-KeireHub.cmd')"
Write-Host "==> Standalone Hub package archive created: $archive"
}
finally {
    Exit-KeireWorkspaceLock -Lock $WorkspaceLock
}
