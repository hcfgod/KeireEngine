$ErrorActionPreference = "Stop"

function Assert-StagingState {
    param([bool]$Condition, [string]$Message)

    if (-not $Condition) {
        throw $Message
    }
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
. (Join-Path $root "Scripts\Windows\common.ps1")
$premakePolicy = Get-Content (Join-Path $root "Scripts\Premake\Common.lua") -Raw
$managedPremake = Get-Content (Join-Path $root "Scripts\Premake\Managed.lua") -Raw
$clientPremake = Get-Content (Join-Path $root "KeireClient\premake5.lua") -Raw
$assetToolPremake = Get-Content (Join-Path $root "AssetTool\premake5.lua") -Raw
$runtimePremake = Get-Content (Join-Path $root "KeireRuntime\premake5.lua") -Raw
$assetToolSource = Get-Content (Join-Path $root "AssetTool\Source\Main.cpp") -Raw
Assert-StagingState ($premakePolicy.Contains("DependencyManifest.CoralNetHostRuntime") -and
                     $premakePolicy.Contains("postbuildcommands")) `
    "KeireCore consumers do not stage the Windows nethost runtime."
Assert-StagingState ($managedPremake.Contains("stage-managed-host.ps1") -and
                     $managedPremake.Contains('if _ACTION == "ninja"')) `
    "Generated IDE projects do not use the shared managed-host staging script."
Assert-StagingState ($clientPremake.Contains("AddKeireManagedHostStaging()")) `
    "The editor project does not stage its managed host."
Assert-StagingState ($assetToolPremake.Contains("AddKeireManagedRuntimeDependency()") -and
                     $assetToolPremake.Contains("AddKeireManagedHostStaging()")) `
    "The Asset Tool project does not build and stage its managed host."
Assert-StagingState ($runtimePremake.Contains("AddKeireManagedRuntimeDependency()") -and
                     $runtimePremake.Contains("AddKeireManagedHostStaging()")) `
    "The packaged player template does not build and stage its managed host."
Assert-StagingState ($assetToolSource.Contains("specification.RuntimeHostDirectory = managedHost;") -and
                     $assetToolSource.Contains('specification.RuntimeRootDirectory = managedHost / "Dotnet";')) `
    "The Asset Tool does not initialize managed type discovery from its staged host."

$stagingProject = [pscustomobject]@{
    PROJECT_NAMESPACE = "Keire"
    CLIENT_TARGET = "KeireClient"
    HUB_TARGET = "KeireHub"
}
$clientTargets = @(Get-ManagedHostStagingTargets -Project $stagingProject -Target "KeireClient")
Assert-StagingState (($clientTargets -join ",") -eq "KeireAssetTool,KeireRuntime,KeireClient") `
    "Editor builds do not refresh managed hosts for their executable dependencies."
$hubTargets = @(Get-ManagedHostStagingTargets -Project $stagingProject -Target "KeireHub")
Assert-StagingState (($hubTargets -join ",") -eq "KeireAssetTool,KeireRuntime,KeireClient,KeireHub") `
    "Hub builds do not refresh the editor dependency managed hosts."
$toolTargets = @(Get-ManagedHostStagingTargets -Project $stagingProject -Target "KeireAssetTool")
Assert-StagingState (($toolTargets -join ",") -eq "KeireAssetTool") `
    "Direct managed-host target staging changed unexpectedly."

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-managed-host-" + [guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force (Join-Path $fixture "Scripts\Windows"),
        (Join-Path $fixture "Build\Bin\Release-windows-x86_64\KeireClient"),
        (Join-Path $fixture "Build\Dependencies\coral-patched\Build\Release"),
        (Join-Path $fixture "Build\Dependencies\coral-nethost"),
        (Join-Path $fixture "Build\Dependencies\dotnet-sdk\host\fxr\10.0.1"),
        (Join-Path $fixture "Build\Dependencies\dotnet-sdk\shared\Microsoft.NETCore.App\10.0.1"),
        (Join-Path $fixture "Build\Managed") | Out-Null
    Copy-Item (Join-Path $root "Scripts\Windows\common.ps1") (Join-Path $fixture "Scripts\Windows\common.ps1")
    Copy-Item (Join-Path $root "Scripts\Windows\stage-managed-host.ps1") `
        (Join-Path $fixture "Scripts\Windows\stage-managed-host.ps1")

    foreach ($file in @("Coral.Managed.dll", "Coral.Managed.deps.json", "Coral.Managed.runtimeconfig.json")) {
        $file | Set-Content (Join-Path $fixture "Build\Dependencies\coral-patched\Build\Release\$file") -Encoding ASCII
    }
    "managed-api" | Set-Content (Join-Path $fixture "Build\Managed\Keire.Managed.dll") -Encoding ASCII
    "hostfxr" | Set-Content `
        (Join-Path $fixture "Build\Dependencies\dotnet-sdk\host\fxr\10.0.1\hostfxr.dll") -Encoding ASCII
    "coreclr" | Set-Content `
        (Join-Path $fixture "Build\Dependencies\dotnet-sdk\shared\Microsoft.NETCore.App\10.0.1\coreclr.dll") `
        -Encoding ASCII

    $failedWithoutNetHost = $false
    try {
        & (Join-Path $fixture "Scripts\Windows\stage-managed-host.ps1") -Root $fixture `
            -Configuration Release -Architecture x86_64 -Target KeireClient
    }
    catch {
        $failedWithoutNetHost = $_.Exception.Message.Contains("nethost runtime is missing")
    }
    Assert-StagingState $failedWithoutNetHost "Missing nethost was not rejected."
    Assert-StagingState (-not (Test-Path (Join-Path $fixture "Build\Bin\Release-windows-x86_64\KeireClient\Managed"))) `
        "Failed managed-host validation modified the target directory."

    "nethost" | Set-Content (Join-Path $fixture "Build\Dependencies\coral-nethost\nethost.dll") -Encoding ASCII
    & (Join-Path $fixture "Scripts\Windows\stage-managed-host.ps1") -Root $fixture `
        -Configuration Release -Architecture x86_64 -Target KeireClient

    $target = Join-Path $fixture "Build\Bin\Release-windows-x86_64\KeireClient"
    foreach ($relativePath in @("nethost.dll", "Managed\Coral.Managed.dll", "Managed\Keire.Managed.dll",
            "Managed\Dotnet\host\fxr\10.0.1\hostfxr.dll",
            "Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.1\coreclr.dll")) {
        Assert-StagingState (Test-Path (Join-Path $target $relativePath)) `
            "Managed-host staging omitted $relativePath."
    }
}
finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows managed-host staging regression tests passed."
