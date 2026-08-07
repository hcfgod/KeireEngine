[CmdletBinding()]
param(
    [string]$Generator = "vs2022",
    [string]$Architecture = "",
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$Update,
    [switch]$Generate,
    [switch]$AllowDirty
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset

Invoke-CheckedWindowsCommand {
    & (Join-Path $PSScriptRoot "package.ps1") -Generator $Generator -Configuration Dist `
        -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate `
        -AllowDirty:$AllowDirty -StageOnly
} "Dist editor package gate"

$sdkName = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-Dist"
$sdkStage = Join-Path $Root "Artifacts\$sdkName"
if (-not (Test-Path -LiteralPath $sdkStage -PathType Container)) {
    throw "The Dist package gate did not produce its staging directory: $sdkStage"
}

$name = "$($Project.ARTIFACT_PREFIX)-editor-windows-$Architecture-Dist"
$distributionRoot = Join-Path $Root "Build\Distributions"
$stage = Join-Path $distributionRoot $name
$legacyStage = Join-Path $Root "Artifacts\$name"
$archive = Join-Path $Root "Artifacts\$name.zip"
$validationRoot = Join-Path $Root "Artifacts\$name-validation"
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $legacyStage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $archive, "$archive.sha256" -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $distributionRoot | Out-Null
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($directory in @("bin", "samples", "docs")) {
    Copy-Item -LiteralPath (Join-Path $sdkStage $directory) -Destination $stage -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $stage "Config"), (Join-Path $stage "third-party") | Out-Null
Copy-Item -LiteralPath (Join-Path $sdkStage "Config\Client.json") -Destination (Join-Path $stage "Config")
Copy-Item -LiteralPath (Join-Path $sdkStage "third-party\licenses") `
    -Destination (Join-Path $stage "third-party") -Recurse
foreach ($file in @("README.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md", "build-manifest.json")) {
    Copy-Item -LiteralPath (Join-Path $sdkStage $file) -Destination $stage
}

$dotnetSource = Join-Path $Root "Build\Dependencies\dotnet-sdk"
$dotnetDestination = Join-Path $stage "bin\Managed\Dotnet"
if (-not (Test-Path -LiteralPath (Join-Path $dotnetSource "dotnet.exe") -PathType Leaf)) {
    throw "The bundled .NET SDK is missing: $dotnetSource"
}
Remove-Item -LiteralPath $dotnetDestination -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dotnetDestination | Out-Null
Get-ChildItem -LiteralPath $dotnetSource -Force | Copy-Item -Destination $dotnetDestination -Recurse -Force

$sdkManifest = Get-Content -LiteralPath (Join-Path $stage "build-manifest.json") -Raw | ConvertFrom-Json
if ($sdkManifest.configuration -ne "Dist" -or $sdkManifest.platform -ne "Windows") {
    throw "The editor package source manifest is not a Windows Dist build."
}
$dotnetSdk = Get-ChildItem -LiteralPath (Join-Path $dotnetDestination "sdk") -Directory |
    Where-Object { $_.Name -match '^10\.' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $dotnetSdk) { throw "The bundled editor runtime does not contain the .NET 10 SDK." }
$editorManifest = [ordered]@{
    schemaVersion = 1
    artifact = "editor"
    project = $Project.PROJECT_IDENTIFIER
    version = $Project.PROJECT_VERSION
    commit = $sdkManifest.commit
    dirty = [bool]$sdkManifest.dirty
    developmentArtifact = [bool]$sdkManifest.developmentArtifact
    platform = "Windows"
    architecture = $sdkManifest.architecture
    configuration = "Dist"
    launcher = "Launch-KeireEditor.cmd"
    bundledDotnetSdk = $dotnetSdk.Name
    buildManifest = "build-manifest.json"
}
$editorManifestJson = $editorManifest | ConvertTo-Json
[IO.File]::WriteAllText((Join-Path $stage "editor-package.json"), $editorManifestJson + "`n",
    [Text.UTF8Encoding]::new($false))
$launcher = "@echo off`r`nstart `"`" `"%~dp0bin\$($Project.HUB_TARGET).exe`"`r`n"
[IO.File]::WriteAllText((Join-Path $stage "Launch-KeireEditor.cmd"), $launcher,
    [Text.ASCIIEncoding]::new())

Assert-WindowsEditorPackageStage $stage $Project.CLIENT_TARGET $Project.HUB_TARGET $Project.CORE_TARGET `
    $Project.PROJECT_NAMESPACE
$hubVersion = Invoke-WindowsExecutableCapture `
    (Join-Path $stage "bin\$($Project.HUB_TARGET).exe") @("--version")
if ($hubVersion.ExitCode -ne 0 -or -not $hubVersion.StandardOutput.Contains($Project.PROJECT_VERSION)) {
    throw "Packaged Project Hub version validation failed."
}
$sdkList = (& (Join-Path $dotnetDestination "dotnet.exe") --list-sdks) -join "`n"
if ($LASTEXITCODE -ne 0 -or -not $sdkList.Contains($dotnetSdk.Name)) {
    throw "Packaged .NET SDK validation failed."
}

Compress-WindowsArchive (Join-Path $stage "*") $archive
Assert-WindowsPackageArchiveGeneratedDataFree $archive
(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" |
    Set-Content "$archive.sha256" -Encoding ASCII

try {
    Remove-Item -LiteralPath $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $validationRoot | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $validationRoot -Force
    Assert-WindowsEditorPackageStage $validationRoot $Project.CLIENT_TARGET $Project.HUB_TARGET `
        $Project.CORE_TARGET $Project.PROJECT_NAMESPACE
    $extractedSdkList = (& (Join-Path $validationRoot "bin\Managed\Dotnet\dotnet.exe") --list-sdks) -join "`n"
    if ($LASTEXITCODE -ne 0 -or -not $extractedSdkList.Contains($dotnetSdk.Name)) {
        throw "Extracted editor package .NET SDK validation failed."
    }
}
finally {
    Remove-Item -LiteralPath $validationRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Remove-Item -LiteralPath $sdkStage -Recurse -Force
Write-Host "==> Ready-to-run editor distribution: $stage"
Write-Host "==> Launch with: $(Join-Path $stage 'Launch-KeireEditor.cmd')"
Write-Host "==> Editor package archive created: $archive"
