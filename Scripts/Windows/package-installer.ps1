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
    & (Join-Path $PSScriptRoot "package-editor.ps1") -Generator $Generator -Architecture $Architecture `
        -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate -AllowDirty:$AllowDirty
} "Dist editor distribution gate"

$distributionName = "$($Project.ARTIFACT_PREFIX)-editor-windows-$Architecture-Dist"
$distribution = Join-Path $Root "Build\Distributions\$distributionName"
Assert-WindowsEditorPackageStage $distribution $Project.CLIENT_TARGET $Project.HUB_TARGET `
    $Project.CORE_TARGET $Project.PROJECT_NAMESPACE

$makensisCommand = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
$makensisPath = if ($makensisCommand) { $makensisCommand.Source } else { "" }
if (-not $makensisPath) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"),
        (Join-Path $env:ProgramFiles "NSIS\makensis.exe")
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    if ($candidates) { $makensisPath = @($candidates)[0] }
}
if (-not $makensisPath) {
    throw "NSIS 3 was not found. Install it with 'winget install NSIS.NSIS', then rerun package-installer."
}

$semanticCore = ($Project.PROJECT_VERSION -split '[-+]')[0]
$numericParts = @($semanticCore -split '\.')
if ($numericParts.Count -ne 3) { throw "The project version cannot be converted to a Windows file version." }
$fileVersion = "$($numericParts[0]).$($numericParts[1]).$($numericParts[2]).0"
$artifactBase = "$($Project.ARTIFACT_PREFIX)-editor-windows-$Architecture-$($Project.PROJECT_VERSION)-setup"
$artifact = Join-Path $Root "Artifacts\$artifactBase.exe"
$checksum = "$artifact.sha256"
$template = Join-Path $Root "Installer\Windows\KeireEditor.nsi"
$icon = Join-Path $Root "Config\Branding\Keire.ico"
$license = Join-Path $Root "LICENSE.txt"
Remove-Item -LiteralPath $artifact, $checksum -Force -ErrorAction SilentlyContinue

$arguments = @(
    "/DPRODUCT_IDENTIFIER=$($Project.PROJECT_IDENTIFIER)",
    "/DPRODUCT_DISPLAY_NAME=$($Project.PROJECT_DISPLAY_NAME)",
    "/DPRODUCT_VERSION=$($Project.PROJECT_VERSION)",
    "/DPRODUCT_FILE_VERSION=$fileVersion",
    "/DPRODUCT_ARCHITECTURE=$Architecture",
    "/DHUB_TARGET=$($Project.HUB_TARGET)",
    "/DSOURCE_DIRECTORY=$distribution",
    "/DOUTPUT_PATH=$artifact",
    "/DLICENSE_PATH=$license",
    "/DSETUP_ICON_PATH=$icon",
    $template
)
& $makensisPath @arguments
if ($LASTEXITCODE -ne 0) { throw "NSIS failed with exit code $LASTEXITCODE." }
if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
    throw "NSIS did not produce the expected installer: $artifact"
}

$signingThumbprint = $env:KEIRE_WINDOWS_SIGNING_CERT_SHA1
if ($signingThumbprint) {
    $signTool = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if (-not $signTool) { throw "KEIRE_WINDOWS_SIGNING_CERT_SHA1 is set, but signtool.exe was not found." }
    $timestampUrl = if ($env:KEIRE_WINDOWS_TIMESTAMP_URL) {
        $env:KEIRE_WINDOWS_TIMESTAMP_URL
    }
    else {
        "http://timestamp.digicert.com"
    }
    & $signTool.Source sign /sha1 $signingThumbprint /fd SHA256 /tr $timestampUrl /td SHA256 $artifact
    if ($LASTEXITCODE -ne 0) { throw "Authenticode signing failed with exit code $LASTEXITCODE." }
    & $signTool.Source verify /pa $artifact
    if ($LASTEXITCODE -ne 0) { throw "Authenticode verification failed with exit code $LASTEXITCODE." }
}

(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant() + "  $artifactBase.exe" |
    Set-Content -LiteralPath $checksum -Encoding ASCII
Write-Host "==> Windows editor installer created: $artifact"
Write-Host "==> Installer checksum created: $checksum"
