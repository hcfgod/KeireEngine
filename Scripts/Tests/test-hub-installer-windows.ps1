$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"
$launcher = Get-Content -LiteralPath (Join-Path $Root "Scripts\project.ps1") -Raw
if (-not ($launcher.Contains('"package-hub-installer"') -and
        $launcher.Contains('Join-Path $WindowsScripts "package-hub-installer.ps1"'))) {
    throw "The Windows launcher does not expose the Hub installer command."
}

$packager = Get-Content -LiteralPath (Join-Path $Windows "package-hub-installer.ps1") -Raw
foreach ($contract in @("package-hub.ps1", "Assert-WindowsHubPackageStage", "makensis.exe", "NSIS.NSIS",
        "Get-FileHash", "/DHUB_TARGET=", "/DKEIRE_INSTALL_WORKER_AUTHORITY=1")) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows Hub installer packager is missing '$contract'."
    }
}

$templatePath = Join-Path $Root "Installer\Windows\KeireHub.nsi"
$template = Get-Content -LiteralPath $templatePath -Raw
foreach ($contract in @('RequestExecutionLevel user', 'MUI_PAGE_LICENSE', 'MUI_PAGE_COMPONENTS',
        'MUI_PAGE_DIRECTORY', 'Desktop shortcut', 'Start Menu shortcuts', 'WriteUninstaller',
        'INSTALL_MARKER_CONTENT', 'INSTALL_OWNERSHIP_IDENTIFIER',
        '!error "KeireInstallWorker is the required Hub install and uninstall authority."',
        'MUI_CUSTOMFUNCTION_ABORT HubInstallerAbort', 'KEIRE_HUB_TEST_FAIL_DURING_STAGE',
        'File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"',
        'install-deferred --product hub', 'integrate --product hub', 'commit --product hub',
        'recover --product hub', 'uninstall --product hub', 'KeireInstallWorkerPending',
        'nsExec::ExecToStack /TIMEOUT=30000', '${HUB_TARGET}.exe" --verify-installation',
        'MUI_FINISHPAGE_RUN "$INSTDIR\bin\${HUB_TARGET}.exe"', '/KEIRE_HUB_UPDATE=',
        '/INSTALL_ROOT=', '/RESUME_TOKEN=', '/WAIT_PROCESS=', 'WaitForSingleObject')) {
    if (-not $template.Contains($contract)) {
        throw "The worker-authority Hub NSIS template is missing '$contract'."
    }
}

foreach ($forbidden in @('!else', 'HUB_TRANSACTION_MARKER', 'ValidateHubInstallDestination',
        'ValidateExistingHubInstallation', 'BeginHubTransaction', 'BackupHubTransactionEntry',
        'RestoreHubTransactionEntry', 'RecordHubTransactionPayload', 'RemoveHubPayload', 'CreateShortcut',
        'CopyFiles', 'WriteReg', 'DeleteReg', 'RMDir', 'HUB_PROTOCOL_KEY',
        'ExecWait ''"$INSTDIR\bin\KeireInstallWorker.exe"')) {
    if ($template.Contains($forbidden)) {
        throw "The Hub NSIS shell still contains the forbidden legacy authority '$forbidden'."
    }
}

$mainStart = $template.IndexOf('Section "${PRODUCT_NAME} (required)" MainSection',
    [StringComparison]::Ordinal)
$mainEnd = $template.IndexOf('SectionEnd', $mainStart, [StringComparison]::Ordinal)
$finalizeStart = $template.IndexOf('Section "-Finalize Hub installation"',
    [StringComparison]::Ordinal)
$finalizeEnd = $template.IndexOf('SectionEnd', $finalizeStart, [StringComparison]::Ordinal)
$uninstallStart = $template.LastIndexOf('Section "Uninstall"', [StringComparison]::Ordinal)
if ($mainStart -lt 0 -or $mainEnd -lt 0 -or $finalizeStart -lt 0 -or $finalizeEnd -lt 0 -or
    $uninstallStart -lt 0) {
    throw "The Hub NSIS template is missing a required worker-authority section."
}
$main = $template.Substring($mainStart, $mainEnd - $mainStart)
$finalize = $template.Substring($finalizeStart, $finalizeEnd - $finalizeStart)
$uninstall = $template.Substring($uninstallStart)
$extraction = $main.IndexOf('File /r "${SOURCE_DIRECTORY}\*"', [StringComparison]::Ordinal)
$worker = $main.IndexOf('install-deferred --product hub', [StringComparison]::Ordinal)
$verification = $main.IndexOf('--verify-installation', [StringComparison]::Ordinal)
if ($extraction -lt 0 -or $worker -lt $extraction -or $verification -lt $worker) {
    throw "Hub NSIS must gather payload, delegate mutation, then run the real binary verification in order."
}
$outputReset = $main.LastIndexOf('SetOutPath "$PLUGINSDIR"', $worker, [StringComparison]::Ordinal)
if ($outputReset -lt $extraction) {
    throw "Hub NSIS must release its staged-payload directory handle before the worker anchors that source."
}
if ($main.Contains('SetOutPath "$INSTDIR"')) {
    throw "Hub NSIS must not retain a current-directory handle on the installation during finalization or recovery."
}
$integrate = $finalize.IndexOf('integrate --product hub', [StringComparison]::Ordinal)
$commit = $finalize.IndexOf('commit --product hub', [StringComparison]::Ordinal)
$finalizeOutputReset = $finalize.LastIndexOf('SetOutPath "$PLUGINSDIR"', $integrate,
    [StringComparison]::Ordinal)
if ($integrate -lt 0 -or $commit -lt $integrate -or $finalizeOutputReset -lt 0 -or
    -not $finalize.Contains('Call RecoverPendingHubInstall')) {
    throw "Hub NSIS finalization must journal shell integration before commit and recover on failure."
}
$recoverStart = $template.IndexOf('Function RecoverPendingHubInstall', [StringComparison]::Ordinal)
$recoverEnd = $template.IndexOf('FunctionEnd', $recoverStart, [StringComparison]::Ordinal)
$recover = $template.Substring($recoverStart, $recoverEnd - $recoverStart)
if (-not $recover.Contains('SetOutPath "$PLUGINSDIR"')) {
    throw "Hub NSIS recovery must release every current-directory handle on the installation root."
}
if (-not $uninstall.Contains(
        'File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"') -or
    -not $uninstall.Contains('uninstall embedded worker extracted') -or
    $uninstall.Contains('CopyFiles') -or $uninstall.Contains('$INSTDIR\bin\KeireInstallWorker.exe')) {
    throw "The Hub uninstaller must execute only its build-time embedded worker."
}

$hubEntryPoint = Get-Content -LiteralPath (Join-Path $Root "KeireHub\Source\HubEntryPoint.cpp") -Raw
foreach ($contract in @('HandleHubCommandWithoutApplication', '--verify-installation',
        'ReadInstallerPackageManifest', 'InstallProduct::Hub')) {
    if (-not $hubEntryPoint.Contains($contract)) {
        throw "The real Hub executable verification path is missing '$contract'."
    }
}
$windowsCommon = Get-Content -LiteralPath (Join-Path $Windows "common.ps1") -Raw
if (-not ($windowsCommon.Contains('Invoke-WindowsExecutableCapture -Path $hubExecutable') -and
        $windowsCommon.Contains('-Arguments @("--verify-installation")'))) {
    throw "The Hub package gate must execute the real hidden verification command."
}

$workerMain = Get-Content -LiteralPath (Join-Path $Root "KeireInstallWorker\Source\Main.cpp") -Raw
foreach ($contract in @('--verify-installation', 'install-deferred', 'CommitInstallTransaction',
        '#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)', 'KEIRE_INSTALL_WORKER_INTERRUPT_AFTER')) {
    if (-not $workerMain.Contains($contract)) {
        throw "The Hub install worker command boundary is missing '$contract'."
    }
}
$workerPremake = Get-Content -LiteralPath (Join-Path $Root "KeireInstallWorker\premake5.lua") -Raw
if (-not ($workerPremake.Contains('filter "configurations:Debug or DebugASan"') -and
        $workerPremake.Contains('KEIRE_INSTALL_WORKER_FAULT_INJECTION'))) {
    throw "Install-worker fault injection must be restricted to test-capable configurations."
}
$hubPackage = Get-Content -LiteralPath (Join-Path $Windows "package-hub.ps1") -Raw
foreach ($contract in @('$installWorkerTarget', 'KEIRE_INSTALL_WORKER_INTERRUPT_AFTER',
        'contains test-only fault injection')) {
    if (-not $hubPackage.Contains($contract)) {
        throw "The Hub package must enforce the production install-worker boundary '$contract'."
    }
}

& (Join-Path $PSScriptRoot "prepare-install-worker-runtime-windows.ps1")
$workerRuntime = Join-Path $PSScriptRoot "test-install-worker-runtime-windows.ps1"
& (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $workerRuntime -Product hub
if ($LASTEXITCODE -ne 0) {
    throw "The Hub install-worker process matrix failed."
}
$nsisRuntime = Join-Path $PSScriptRoot "test-nsis-worker-runtime-windows.ps1"
& (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $nsisRuntime -Product hub
if ($LASTEXITCODE -ne 0) {
    throw "The Hub worker-authority NSIS runtime matrix failed."
}

Write-Host "Windows Hub installer checks passed."
