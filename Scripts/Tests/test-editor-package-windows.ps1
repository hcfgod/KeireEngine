$ErrorActionPreference = "Stop"
$Windows = Resolve-Path (Join-Path $PSScriptRoot "..\Windows")
. (Join-Path $Windows "common.ps1")

function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}

$launcher = Get-Content (Join-Path $PSScriptRoot "..\project.ps1") -Raw
if (-not ($launcher.Contains('"package-editor"') -and $launcher.Contains('$Configuration = "Dist"'))) {
    throw "The Windows launcher does not expose the Dist editor package command."
}
$packager = Get-Content (Join-Path $Windows "package-editor.ps1") -Raw
foreach ($contract in @("-Configuration Dist", "-StageOnly", "Build\Dependencies\dotnet-sdk",
        "Assert-WindowsEditorPackageStage", "editor-package.json")) {
    if (-not $packager.Contains($contract)) { throw "The Windows editor packager is missing '$contract'." }
}
$clientPremake = Get-Content (Join-Path (Get-RepositoryRoot) "KeireClient\premake5.lua") -Raw
if (-not $clientPremake.Contains("AddKeireManagedHostStaging()")) {
    throw "The Windows Ninja build does not delegate managed-host staging to the repository launcher."
}

$stage = Join-Path ([IO.Path]::GetTempPath()) ("keire-editor-package-test-" + [guid]::NewGuid().ToString("N"))
$archive = "$stage.zip"
try {
    foreach ($path in (Get-WindowsRequiredEditorPackagePaths Client Hub Core Core)) {
        $file = Join-Path $stage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    foreach ($fileName in @("avcodec-62.dll", "avformat-62.dll", "avutil-60.dll", "swresample-6.dll")) {
        New-Item -ItemType File -Force (Join-Path $stage "bin\$fileName") | Out-Null
    }
    $dotnetSdkBuild = Join-Path $stage "bin\Managed\Dotnet\sdk\10.0.100\Sdks\Fixture\build"
    New-Item -ItemType Directory -Force $dotnetSdkBuild | Out-Null
    New-Item -ItemType File -Force (Join-Path $dotnetSdkBuild "Fixture.targets") | Out-Null

    Assert-WindowsEditorPackageStage $stage Client Hub Core Core
    Compress-WindowsArchive (Join-Path $stage "*") $archive
    Assert-WindowsPackageArchiveGeneratedDataFree $archive

    Remove-Item (Join-Path $stage "bin\Managed\Dotnet\dotnet.exe")
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Missing bundled editor dotnet validation"
    New-Item -ItemType File (Join-Path $stage "bin\Managed\Dotnet\dotnet.exe") | Out-Null

    New-Item -ItemType Directory (Join-Path $stage "include") | Out-Null
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } "Editor SDK content rejection"
    Remove-Item (Join-Path $stage "include") -Recurse -Force

    $generatedEditorFile = Join-Path $stage "samples\KeireSandbox\Build\generated.txt"
    New-Item -ItemType Directory -Force (Split-Path $generatedEditorFile) | Out-Null
    New-Item -ItemType File $generatedEditorFile | Out-Null
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor generated project data rejection"
}
finally {
    Remove-Item $archive -Force -ErrorAction SilentlyContinue
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows editor package checks passed."
