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

$launcher = Get-Content (Join-Path $PSScriptRoot "..\project.ps1") -Raw
if (-not ($launcher.Contains('"package-hub"') -and $launcher.Contains('$Configuration = "Dist"'))) {
    throw "The Windows launcher does not expose the Dist standalone Hub package command."
}
$packager = Get-Content (Join-Path $Windows "package-hub.ps1") -Raw
foreach ($contract in @("Standalone Hub build", "Build\Distributions", "Assert-WindowsHubPackageStage",
        "write-package-manifest.py", "write-distribution-config.py", "validate-supabase-config.py",
        "hub-package.json", "libsodium.dll", "libsodium-LICENSE.txt")) {
    if (-not $packager.Contains($contract)) { throw "The Windows Hub packager is missing '$contract'." }
}
if (-not $packager.Contains('"--project-schema-maximum", "4"')) {
    throw "The Windows Hub package must advertise project schema 4 support."
}
if ($packager.Contains('package-editor.ps1') -or $packager.Contains('package.ps1')) {
    throw "The standalone Windows Hub package must not stage through the editor or SDK package."
}

$stage = Join-Path ([IO.Path]::GetTempPath()) ("keire-hub-package-test-" + [guid]::NewGuid().ToString("N"))
$archive = "$stage.zip"
try {
    foreach ($path in (Get-WindowsRequiredHubPackagePaths Hub Core)) {
        $file = Join-Path $stage $path
        New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
        New-Item -ItemType File -Force $file | Out-Null
    }
    Remove-Item -LiteralPath (Join-Path $stage "content") -Recurse -Force
    New-Item -ItemType Directory -Force (Join-Path $stage "content") | Out-Null
    Get-ChildItem -LiteralPath (Join-Path (Get-RepositoryRoot) "KeireHubContent") -Force |
        Copy-Item -Destination (Join-Path $stage "content") -Recurse
    Remove-Item -LiteralPath (Join-Path $stage "Docs"), (Join-Path $stage "Samples") `
        -Recurse -Force -ErrorAction SilentlyContinue
    Copy-WindowsTrackedTree (Get-RepositoryRoot) "Docs" (Join-Path $stage "Docs")
    Copy-WindowsTrackedTree (Get-RepositoryRoot) "Samples/KeireSandbox" `
        (Join-Path $stage "Samples\KeireSandbox")
    $contentCatalog = Get-Content -LiteralPath (Join-Path $stage "content\Content\en-US.json") -Raw
    if (-not $contentCatalog.Contains('"localPath": "Samples/KeireSandbox/README.md"') -or
        -not (Test-Path -LiteralPath (Join-Path $stage "Samples\KeireSandbox\README.md") -PathType Leaf)) {
        throw "The packaged Sandbox learning target does not preserve its case-sensitive path."
    }
    Write-TestPeExecutable (Join-Path $stage "bin\Hub.exe") 2
    Write-TestPeExecutable (Join-Path $stage "bin\CoreHubWorker.exe") 3
    New-Item -ItemType File -Force (Join-Path $stage "bin\libsodium.dll"), `
        (Join-Path $stage "third-party\licenses\libsodium-LICENSE.txt") | Out-Null

    $python = Get-PythonInvocation
    $pythonPrefix = @($python.PrefixArguments)
    Copy-Item -LiteralPath (Join-Path (Get-RepositoryRoot) "Config\Supabase.json") `
        -Destination (Join-Path $stage "Config\Supabase.json") -Force
    $distributionWriter = Join-Path (Get-RepositoryRoot) "Scripts\Packaging\write-distribution-config.py"
    $distributionSource = Join-Path (Get-RepositoryRoot) "Config\Distribution.json"
    Invoke-CheckedWindowsCommand {
        & $python.Executable @pythonPrefix $distributionWriter `
            --output (Join-Path $stage "Config\Distribution.json") `
            --source-config $distributionSource
    } "Fixture distribution configuration generation"
    $distribution = Get-Content -LiteralPath (Join-Path $stage "Config\Distribution.json") -Raw |
        ConvertFrom-Json
    $distributionAuthority = Get-Content -LiteralPath $distributionSource -Raw | ConvertFrom-Json
    $packagedKeyIds = @($distribution.trustedKeys | ForEach-Object { $_.keyId })
    $authorityKeyIds = @($distributionAuthority.trustedKeys | ForEach-Object { $_.keyId })
    if (-not $distribution.onlineDiscoveryEnabled -or
        $distribution.serviceBaseUrl -ne $distributionAuthority.serviceBaseUrl -or
        $packagedKeyIds.Count -lt 1 -or
        (Compare-Object $packagedKeyIds $authorityKeyIds)) {
        throw "The Windows Hub package fixture did not preserve online distribution trust."
    }
    $manifestWriter = Join-Path (Get-RepositoryRoot) "Scripts\Packaging\write-package-manifest.py"
    $manifestArguments = @(
        $manifestWriter, "write", "--stage", $stage, "--output", "hub-package.json",
        "--artifact", "hub", "--package-prefix", "fixture", "--project", "Fixture",
        "--version", "1.2.3", "--channel", "Stable", "--commit", "fixture", "--dirty", "false",
        "--development-artifact", "false", "--platform", "Windows", "--architecture", "x86_64",
        "--configuration", "Dist", "--launcher", "Launch-KeireHub.cmd",
        "--module-definition", "Config/SourceModules.premake.lua", "--entrypoint", "hub=bin/Hub.exe",
        "--entrypoint", "worker=bin/CoreHubWorker.exe",
        "--template-catalog", "content/Templates/catalog.json", "--release-notes", "CHANGELOG.md"
    )
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture Hub manifest generation"
    $manifest = Get-Content -LiteralPath (Join-Path $stage "hub-package.json") -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 2 -or $manifest.artifact -ne "hub" -or
        $manifest.entrypoints.hub -ne "bin/Hub.exe" -or
        $manifest.entrypoints.worker -ne "bin/CoreHubWorker.exe" -or
        $manifest.projectSchema.minimum -ne 1 -or $manifest.projectSchema.maximum -ne 4 -or
        $manifest.packagedTemplates.Count -ne 3 -or
        $manifest.files.path -notcontains "content/Content/en-US.json" -or
        $manifest.files.path -notcontains "content/Licenses/catalog.json" -or
        $manifest.files.path -notcontains "Config/Distribution.json" -or
        $manifest.files.path -notcontains "Config/Supabase.json" -or
        $manifest.files.path -notcontains "Config/Marketplace/trusted-marketplace-key.json" -or
        $manifest.files.path -notcontains "Config/Marketplace/trusted-marketplace-keys.json" -or
        $manifest.licenseReferences -notcontains "content/Fonts/Inter-OFL.txt" -or
        $manifest.files.Count -lt 1) {
        throw "The standalone Windows Hub schema-2 fixture is incomplete."
    }

    Assert-WindowsHubPackageStage $stage Hub Client Core
    Compress-WindowsArchive (Join-Path $stage "*") $archive
    Assert-WindowsPackageArchiveGeneratedDataFree $archive

    Remove-Item -LiteralPath (Join-Path $stage "content\Fonts\Inter-Variable.ttf")
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Missing Hub font rejection"
    Copy-Item -LiteralPath (Join-Path (Get-RepositoryRoot) "KeireHubContent\Fonts\Inter-Variable.ttf") `
        -Destination (Join-Path $stage "content\Fonts")
    Remove-Item -LiteralPath (Join-Path $stage "content\Templates\catalog.json")
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Missing template catalog rejection"
    Copy-Item -LiteralPath (Join-Path (Get-RepositoryRoot) "KeireHubContent\Templates\catalog.json") `
        -Destination (Join-Path $stage "content\Templates")
    Remove-Item -LiteralPath (Join-Path $stage "content\Content\en-US.json")
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Missing content catalog rejection"
    Copy-Item -LiteralPath (Join-Path (Get-RepositoryRoot) "KeireHubContent\Content\en-US.json") `
        -Destination (Join-Path $stage "content\Content")
    Remove-Item -LiteralPath (Join-Path $stage "content\Licenses\catalog.json")
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Missing license catalog rejection"
    Copy-Item -LiteralPath (Join-Path (Get-RepositoryRoot) "KeireHubContent\Licenses\catalog.json") `
        -Destination (Join-Path $stage "content\Licenses")

    Add-Content -LiteralPath (Join-Path $stage "README.md") -Value "tampered" -Encoding ASCII
    Assert-Throws {
        Invoke-CheckedWindowsCommand {
            & $python.Executable @pythonPrefix $manifestWriter validate --stage $stage `
                --manifest hub-package.json --artifact hub
        } "Tampered Hub manifest validation"
    } "Tampered Hub inventory rejection"
    Invoke-CheckedWindowsCommand { & $python.Executable @pythonPrefix @manifestArguments } `
        "Fixture Hub manifest regeneration"

    Write-TestPeExecutable (Join-Path $stage "bin\Client.exe") 2
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Bundled editor rejection"
    Remove-Item -LiteralPath (Join-Path $stage "bin\Client.exe")

    Write-TestPeExecutable (Join-Path $stage "bin\CoreAssetWorker.exe") 3
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Editor Asset Worker rejection"
    Remove-Item -LiteralPath (Join-Path $stage "bin\CoreAssetWorker.exe")

    Write-TestPeExecutable (Join-Path $stage "bin\CoreHubWorker.exe") 2
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "GUI-subsystem Hub worker rejection"
    Write-TestPeExecutable (Join-Path $stage "bin\CoreHubWorker.exe") 3

    Write-TestPeExecutable (Join-Path $stage "bin\Hub.exe") 3
    Assert-Throws { Assert-WindowsHubPackageStage $stage Hub Client Core } "Console-subsystem Hub rejection"
}
finally {
    Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows standalone Hub package checks passed."
