$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"

$launcher = Get-Content -LiteralPath (Join-Path $Root "Scripts\project.ps1") -Raw
if (-not ($launcher.Contains('"package-hub-installer"') -and
        $launcher.Contains('Join-Path $WindowsScripts "package-hub-installer.ps1"'))) {
    throw "The Windows launcher does not expose the standalone Hub installer command."
}

$packager = Get-Content -LiteralPath (Join-Path $Windows "package-hub-installer.ps1") -Raw
foreach ($contract in @("package-hub.ps1", "-StageOnly", "Assert-WindowsHubPackageStage", "KeireHub.nsi",
        "makensis.exe", "NSIS.NSIS", "KEIRE_WINDOWS_SIGNING_CERT_SHA1", "KEIRE_WINDOWS_TIMESTAMP_URL",
        "Get-FileHash", "-hub-windows-")) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows standalone Hub installer packager is missing '$contract'."
    }
}
if ($packager.Contains('package-editor.ps1') -or $packager.Contains('Assert-WindowsEditorPackageStage')) {
    throw "The standalone Hub installer must not stage through the editor package."
}

$template = Get-Content -LiteralPath (Join-Path $Root "Installer\Windows\KeireHub.nsi") -Raw
foreach ($contract in @('RequestExecutionLevel user', 'MUI_PAGE_LICENSE', 'MUI_PAGE_COMPONENTS',
        'MUI_PAGE_DIRECTORY', 'Desktop shortcut', 'Start Menu shortcuts', 'WriteUninstaller',
        '.keire-hub-install', 'Software\${PRODUCT_IDENTIFIER}\HubInstaller', 'UnsafeUninstall',
        'MUI_FINISHPAGE_RUN', 'intentionally preserved', '/KEIRE_HUB_UPDATE=', '/INSTALL_ROOT=',
        '/RESUME_TOKEN=', '/WAIT_PROCESS=', 'WaitForSingleObject', 'SkipUpdateDirectoryPage',
        'UnsafeUpdate', 'No application files were changed')) {
    if (-not $template.Contains($contract)) {
        throw "The standalone Hub NSIS template is missing '$contract'."
    }
}
if ($template.Contains('.keire-editor-install') -or $template.Contains('${PRODUCT_IDENTIFIER}Editor')) {
    throw "The standalone Hub NSIS template must not claim editor-installer ownership."
}
foreach ($unsafeRemoval in @('RMDir /r "$APPDATA', 'RMDir /r "$PROFILE', 'RMDir /r "$DOCUMENTS',
        'RMDir /r "$LOCALAPPDATA\${PRODUCT_IDENTIFIER}')) {
    if ($template.Contains($unsafeRemoval)) {
        throw "The standalone Hub uninstaller contains unsafe user-data removal: $unsafeRemoval"
    }
}
$uninstallVerification = $template.LastIndexOf('IfFileExists "$INSTDIR\.keire-hub-install"',
    [StringComparison]::Ordinal)
$uninstallRemoval = $template.LastIndexOf('!insertmacro RemoveHubPayload', [StringComparison]::Ordinal)
if ($uninstallVerification -lt 0 -or $uninstallRemoval -lt 0 -or $uninstallVerification -gt $uninstallRemoval) {
    throw "The standalone Hub uninstaller removes its directory before verifying the installation marker."
}
if ([regex]::IsMatch($template, '(?m)^\s*RMDir /r "\$INSTDIR"\s*$')) {
    throw "The standalone Hub uninstaller must not recursively remove unknown files or colocated user data."
}

Write-Host "Windows standalone Hub installer checks passed."
