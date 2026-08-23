[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration,
    [Parameter(Mandatory = $true)][string]$Architecture,
    [Parameter(Mandatory = $true)][string]$Target,
    [switch]$IfPresent
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$resolvedRoot = Resolve-Path -LiteralPath $Root
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$targetDirectory = Join-Path $resolvedRoot "Build\Bin\$Configuration-windows-$outputArchitecture\$Target"
if (-not (Test-Path -LiteralPath $targetDirectory)) {
    if ($IfPresent) {
        return
    }
    throw "The managed host target directory does not exist: $targetDirectory"
}

$coralConfiguration = if ($Configuration -in @("Release", "Dist")) { "Release" } else { "Debug" }
$coralDirectory = Join-Path $resolvedRoot "Build\Dependencies\coral-patched\Build\$coralConfiguration"
$netHost = Join-Path $resolvedRoot "Build\Dependencies\coral-nethost\nethost.dll"
$managedAssembly = Join-Path $resolvedRoot "Build\Managed\Keire.Managed.dll"
$dotnetRoot = Join-Path $resolvedRoot "Build\Dependencies\dotnet-sdk"
$coralFiles = @("Coral.Managed.dll", "Coral.Managed.deps.json", "Coral.Managed.runtimeconfig.json")

function Copy-FileIfChanged {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $sourceFile = Get-Item -LiteralPath $Source
    $destinationFile = Get-Item -LiteralPath $Destination -ErrorAction SilentlyContinue
    if ($destinationFile -and -not $destinationFile.PSIsContainer -and
        $destinationFile.Length -eq $sourceFile.Length -and
        $destinationFile.LastWriteTimeUtc -eq $sourceFile.LastWriteTimeUtc) {
        return
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-TreeIfChanged {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $sourceRoot = (Resolve-Path -LiteralPath $Source).Path
    foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse) {
        $relativePath = $sourceFile.FullName.Substring($sourceRoot.Length).TrimStart("\", "/")
        Copy-FileIfChanged -Source $sourceFile.FullName -Destination (Join-Path $Destination $relativePath)
    }
}

foreach ($file in $coralFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $coralDirectory $file))) {
        throw "The patched Coral runtime output is missing: $file"
    }
}
if (-not (Test-Path -LiteralPath $netHost)) {
    throw "The .NET nethost runtime is missing."
}
if (-not (Test-Path -LiteralPath $managedAssembly)) {
    throw "The Keire.Managed runtime API is missing."
}

$hostFxr = Get-ChildItem (Join-Path $dotnetRoot "host\fxr") -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$coreRuntime = Get-ChildItem (Join-Path $dotnetRoot "shared\Microsoft.NETCore.App") -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $hostFxr -or -not $coreRuntime) {
    throw "The bundled .NET hostfxr or CoreCLR runtime is missing."
}

$managedDirectory = Join-Path $targetDirectory "Managed"
$bundledRoot = Join-Path $managedDirectory "Dotnet"
$bundledHost = Join-Path $bundledRoot "host\fxr\$($hostFxr.Name)"
$bundledRuntime = Join-Path $bundledRoot "shared\Microsoft.NETCore.App\$($coreRuntime.Name)"
New-Item -ItemType Directory -Force -Path $managedDirectory, $bundledHost, $bundledRuntime | Out-Null
foreach ($file in $coralFiles) {
    Copy-FileIfChanged -Source (Join-Path $coralDirectory $file) -Destination (Join-Path $managedDirectory $file)
}
Copy-FileIfChanged -Source $managedAssembly -Destination (Join-Path $managedDirectory "Keire.Managed.dll")
Copy-FileIfChanged -Source $netHost -Destination (Join-Path $targetDirectory "nethost.dll")
Copy-TreeIfChanged -Source $hostFxr.FullName -Destination $bundledHost
Copy-TreeIfChanged -Source $coreRuntime.FullName -Destination $bundledRuntime
foreach ($notice in @("LICENSE.txt", "ThirdPartyNotices.txt")) {
    $noticePath = Join-Path $dotnetRoot $notice
    if (Test-Path -LiteralPath $noticePath) {
        Copy-FileIfChanged -Source $noticePath -Destination (Join-Path $bundledRoot $notice)
    }
}

Write-Host "==> Staged managed host runtime for $Target"
