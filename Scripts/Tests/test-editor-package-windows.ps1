$ErrorActionPreference = "Stop"
$Windows = Resolve-Path (Join-Path $PSScriptRoot "..\Windows")
. (Join-Path $Windows "common.ps1")

function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch { return }
    throw "$Message did not throw."
}

function Write-TestPeExecutable([string]$Path, [uint16]$Subsystem) {
    [byte[]]$bytes = [byte[]]::new(256)
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [BitConverter]::GetBytes([int]128).CopyTo($bytes, 0x3C)
    $bytes[128] = 0x50
    $bytes[129] = 0x45
    $bytes[152] = 0x0B
    $bytes[153] = 0x02
    [BitConverter]::GetBytes($Subsystem).CopyTo($bytes, 220)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

$script:ExecutableVerificationCalls = @()
$script:ExecutableVerificationExitCode = 0
function Invoke-WindowsExecutableCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @()
    )

    $script:ExecutableVerificationCalls += [pscustomobject]@{ Path = $Path; Arguments = @($Arguments) }
    return [pscustomobject]@{
        ExitCode = $script:ExecutableVerificationExitCode
        StandardOutput = ""
        StandardError = if ($script:ExecutableVerificationExitCode -eq 0) { "" } else { "fixture verification failure" }
    }
}

$launcher = Get-Content (Join-Path $PSScriptRoot "..\project.ps1") -Raw
if (-not ($launcher.Contains('"package-editor"') -and $launcher.Contains('$Configuration = "Dist"'))) {
    throw "The Windows launcher does not expose the Dist editor package command."
}
$packager = Get-Content (Join-Path $Windows "package-editor.ps1") -Raw
foreach ($contract in @("-Configuration Dist", "-StageOnly", "Build\Dependencies\dotnet-sdk",
        "Build\Distributions", "Assert-WindowsEditorPackageStage", "write-package-manifest.py",
        "editor-package.json", '$Project.CLIENT_TARGET', '(Join-Path $stage "third-party")')) {
    if (-not $packager.Contains($contract)) { throw "The Windows editor packager is missing '$contract'." }
}
if (-not $packager.Contains('"--project-schema-maximum", "4"')) {
    throw "The Windows editor package must advertise project schema 4 support."
}
if ($packager.Contains('(Join-Path $stage "third-party\licenses") | Out-Null')) {
    throw "The Windows editor packager must not pre-create the copied license directory."
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
    foreach ($runtime in (Get-WindowsFfmpegRuntimeContract).Files) {
        New-Item -ItemType File -Force (Join-Path $stage "bin\$($runtime.FileName)") | Out-Null
    }
    Write-TestPeExecutable (Join-Path $stage "bin\Client.exe") 2
    [IO.File]::WriteAllText((Join-Path $stage "Launch-KeireEditor.cmd"),
        "@echo off`r`nstart `"`" `"%~dp0bin\Client.exe`" %*`r`n", [Text.ASCIIEncoding]::new())
    $dotnetSdkBuild = Join-Path $stage "bin\Managed\Dotnet\sdk\10.0.100\Sdks\Fixture\build"
    New-Item -ItemType Directory -Force $dotnetSdkBuild | Out-Null
    New-Item -ItemType File -Force (Join-Path $dotnetSdkBuild "Fixture.targets") | Out-Null

    $python = Get-PythonInvocation
    $pythonPrefix = @($python.PrefixArguments)
    $manifestWriter = Join-Path (Get-RepositoryRoot) "Scripts\Packaging\write-package-manifest.py"
    $manifestArguments = @(
        $manifestWriter, "write", "--stage", $stage, "--output", "editor-package.json",
        "--artifact", "editor", "--package-prefix", "fixture", "--project", "Fixture",
        "--version", "1.2.3", "--channel", "Stable", "--commit", "fixture", "--dirty", "false",
        "--development-artifact", "false", "--platform", "Windows", "--architecture", "x86_64",
        "--configuration", "Dist", "--launcher", "Launch-KeireEditor.cmd",
        "--build-manifest", "build-manifest.json", "--bundled-dotnet-sdk", "10.0.100",
        "--module-definition", "Config/SourceModules.premake.lua", "--entrypoint", "editor=bin/Client.exe",
        "--entrypoint", "assetTool=bin/CoreAssetTool.exe",
        "--entrypoint", "assetWorker=bin/CoreAssetWorker.exe", "--entrypoint", "runtime=bin/CoreRuntime.exe",
        "--entrypoint", "shaderCompiler=bin/KeireShaderCompiler.exe",
        "--toolchain", "dotnet-sdk|10.0.100|bin/Managed/Dotnet", "--release-notes", "CHANGELOG.md"
    )
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture editor manifest generation"
    $manifest = Get-Content -LiteralPath (Join-Path $stage "editor-package.json") -Raw | ConvertFrom-Json
    $entrypointNames = @($manifest.entrypoints.PSObject.Properties.Name)
    if ($manifest.schemaVersion -ne 2 -or $manifest.entrypoints.editor -ne "bin/Client.exe" -or
        $entrypointNames -contains "hub" -or $entrypointNames -contains "worker" -or
        $manifest.projectSchema.maximum -ne 4 -or $manifest.packagedTemplates.Count -ne 0 -or
        $manifest.bundledToolchains[0].id -ne "dotnet-sdk" -or $manifest.files.Count -lt 1 -or
        $manifest.installedSizeBytes -le 0 -or $manifest.manifestFingerprint -notmatch '^[0-9a-f]{64}$' -or
        $manifest.launcher -ne "Launch-KeireEditor.cmd" -or $manifest.bundledDotnetSdk -ne "10.0.100" -or
        $manifest.buildManifest -ne "build-manifest.json" -or
        $manifest.compatibility.legacySchemaVersion -ne 1 -or
        $manifest.compatibility.legacyTopLevelFields -notcontains "launcher" -or
        $manifest.compatibility.legacyTopLevelFields -notcontains "bundledDotnetSdk" -or
        $manifest.compatibility.legacyTopLevelFields -notcontains "buildManifest" -or
        $manifest.files.path -notcontains "Config/Marketplace/trusted-marketplace-key.json" -or
        $manifest.files.path -notcontains "Config/Marketplace/trusted-marketplace-keys.json" -or
        $manifest.files.path -contains "content/Content/en-US.json" -or
        $manifest.files.path -contains "content/Licenses/catalog.json" -or
        $manifest.licenseReferences -notcontains "content/Fonts/Inter-OFL.txt" -or
        $manifest.licenseReferences -notcontains "content/Fonts/Material-Symbols-Apache-2.0.txt") {
        throw "The Windows editor package schema-2 fixture is incomplete."
    }

    Assert-WindowsEditorPackageStage $stage Client Hub Core Core
    if ($script:ExecutableVerificationCalls.Count -ne 1 -or
        $script:ExecutableVerificationCalls[0].Path -ne (Join-Path $stage "bin\Client.exe") -or
        @($script:ExecutableVerificationCalls[0].Arguments).Count -ne 1 -or
        $script:ExecutableVerificationCalls[0].Arguments[0] -ne "--verify-installation") {
        throw "Editor package validation did not invoke the real executable's hidden installation verifier."
    }
    $script:ExecutableVerificationExitCode = 23
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package executable verification failure"
    $script:ExecutableVerificationExitCode = 0
    $unexpectedFfmpegRuntime = Join-Path $stage "bin\avfilter-12.dll"
    New-Item -ItemType File -Force $unexpectedFfmpegRuntime | Out-Null
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package unexpected FFmpeg component rejection"
    Remove-Item -LiteralPath $unexpectedFfmpegRuntime -Force
    [IO.File]::WriteAllText((Join-Path $stage "Launch-KeireEditor.cmd"),
        "@echo off`r`nstart `"`" `"%~dp0bin\Hub.exe`"`r`n", [Text.ASCIIEncoding]::new())
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package Hub launcher rejection"
    [IO.File]::WriteAllText((Join-Path $stage "Launch-KeireEditor.cmd"),
        "@echo off`r`nstart `"`" `"%~dp0bin\Client.exe`" %*`r`n", [Text.ASCIIEncoding]::new())
    $manifest.entrypoints | Add-Member -NotePropertyName hub -NotePropertyValue "bin/Client.exe"
    $manifest.entrypoints | Add-Member -NotePropertyName worker -NotePropertyValue "bin/Client.exe"
    $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $stage "editor-package.json") `
        -Encoding UTF8
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package Hub manifest entrypoint rejection"
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture editor manifest regeneration"
    $manifest = Get-Content -LiteralPath (Join-Path $stage "editor-package.json") -Raw | ConvertFrom-Json
    $manifest.compatibility.legacyTopLevelFields = @(
        $manifest.compatibility.legacyTopLevelFields | Where-Object { $_ -ne "bundledDotnetSdk" }
    )
    $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $stage "editor-package.json") `
        -Encoding UTF8
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package legacy manifest compatibility rejection"
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture editor manifest regeneration"
    Write-TestPeExecutable (Join-Path $stage "bin\Hub.exe") 2
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package Hub executable rejection"
    Remove-Item -LiteralPath (Join-Path $stage "bin\Hub.exe") -Force
    Write-TestPeExecutable (Join-Path $stage "bin\CoreHubWorker.exe") 3
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Editor package Hub worker rejection"
    Remove-Item -LiteralPath (Join-Path $stage "bin\CoreHubWorker.exe") -Force
    New-Item -ItemType Directory -Force (Join-Path $stage "content\Templates") | Out-Null
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
    "Editor package Hub content rejection"
    Remove-Item -LiteralPath (Join-Path $stage "content\Templates") -Recurse -Force
    Compress-WindowsArchive (Join-Path $stage "*") $archive
    Assert-WindowsPackageArchiveGeneratedDataFree $archive

    Add-Content -LiteralPath (Join-Path $stage "README.md") -Value "tampered" -Encoding ASCII
    Assert-Throws {
        Invoke-CheckedWindowsCommand {
            & $python.Executable @pythonPrefix $manifestWriter validate --stage $stage `
                --manifest editor-package.json --artifact editor
        } "Tampered editor manifest validation"
    } "Tampered editor inventory rejection"
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture editor manifest regeneration"

    Write-TestPeExecutable (Join-Path $stage "bin\Client.exe") 3
    Assert-Throws { Assert-WindowsEditorPackageStage $stage Client Hub Core Core } `
        "Console-subsystem editor executable rejection"
    Write-TestPeExecutable (Join-Path $stage "bin\Client.exe") 2

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
