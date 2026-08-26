$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"
$launcher = Get-Content -LiteralPath (Join-Path $Root "Scripts\project.ps1") -Raw
if (-not ($launcher.Contains('"package-installer"') -and
        $launcher.Contains('Join-Path $WindowsScripts "package-installer.ps1"'))) {
    throw "The Windows launcher does not expose the Editor installer command."
}

$packager = Get-Content -LiteralPath (Join-Path $Windows "package-installer.ps1") -Raw
foreach ($contract in @("package-editor.ps1", "Assert-WindowsEditorPackageStage", "makensis.exe", "NSIS.NSIS",
        "Get-FileHash", "/DCLIENT_TARGET=", "/DKEIRE_INSTALL_WORKER_AUTHORITY=1")) {
    if (-not $packager.Contains($contract)) {
        throw "The Windows Editor installer packager is missing '$contract'."
    }
}

$templatePath = Join-Path $Root "Installer\Windows\KeireEditor.nsi"
$template = Get-Content -LiteralPath $templatePath -Raw
foreach ($contract in @('RequestExecutionLevel user', 'MUI_PAGE_LICENSE', 'MUI_PAGE_COMPONENTS',
        'MUI_PAGE_DIRECTORY', 'Desktop shortcut', 'Start Menu shortcuts', 'WriteUninstaller',
        'INSTALL_MARKER_CONTENT', 'INSTALL_OWNERSHIP_IDENTIFIER',
        '!error "KeireInstallWorker is the required Editor install and uninstall authority."',
        'MUI_CUSTOMFUNCTION_ABORT EditorInstallerAbort', 'KEIRE_EDITOR_TEST_FAIL_DURING_STAGE',
        'File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"',
        'install-deferred --product editor', 'integrate --product editor', 'commit --product editor',
        'recover --product editor', 'uninstall --product editor', 'KeireInstallWorkerPending',
        'nsExec::ExecToStack /TIMEOUT=30000', '${CLIENT_TARGET}.exe" --verify-installation',
        'MUI_FINISHPAGE_RUN "$INSTDIR\bin\${CLIENT_TARGET}.exe"')) {
    if (-not $template.Contains($contract)) {
        throw "The worker-authority Editor NSIS template is missing '$contract'."
    }
}

foreach ($forbidden in @('!else', 'EDITOR_TRANSACTION_MARKER', 'ValidateEditorInstallDestination',
        'ValidateExistingEditorInstallation', 'BeginEditorTransaction', 'BackupEditorTransactionEntry',
        'RestoreEditorTransactionEntry', 'ApplyEditorPayloadEntries', 'RemoveEditorPayload', 'CreateShortcut',
        'CopyFiles', 'WriteReg', 'DeleteReg', 'RMDir', 'ExecWait ''"$INSTDIR\bin\KeireInstallWorker.exe"')) {
    if ($template.Contains($forbidden)) {
        throw "The Editor NSIS shell still contains the forbidden legacy authority '$forbidden'."
    }
}

$mainStart = $template.IndexOf('Section "${PRODUCT_NAME} (required)" MainSection',
    [StringComparison]::Ordinal)
$mainEnd = $template.IndexOf('SectionEnd', $mainStart, [StringComparison]::Ordinal)
$finalizeStart = $template.IndexOf('Section "-Finalize Editor installation"',
    [StringComparison]::Ordinal)
$finalizeEnd = $template.IndexOf('SectionEnd', $finalizeStart, [StringComparison]::Ordinal)
$uninstallStart = $template.LastIndexOf('Section "Uninstall"', [StringComparison]::Ordinal)
if ($mainStart -lt 0 -or $mainEnd -lt 0 -or $finalizeStart -lt 0 -or $finalizeEnd -lt 0 -or
    $uninstallStart -lt 0) {
    throw "The Editor NSIS template is missing a required worker-authority section."
}
$main = $template.Substring($mainStart, $mainEnd - $mainStart)
$finalize = $template.Substring($finalizeStart, $finalizeEnd - $finalizeStart)
$uninstall = $template.Substring($uninstallStart)
$extraction = $main.IndexOf('File /r "${SOURCE_DIRECTORY}\*"', [StringComparison]::Ordinal)
$worker = $main.IndexOf('install-deferred --product editor', [StringComparison]::Ordinal)
$verification = $main.IndexOf('--verify-installation', [StringComparison]::Ordinal)
if ($extraction -lt 0 -or $worker -lt $extraction -or $verification -lt $worker) {
    throw "Editor NSIS must gather payload, delegate mutation, then run the real binary verification in order."
}
$outputReset = $main.LastIndexOf('SetOutPath "$PLUGINSDIR"', $worker, [StringComparison]::Ordinal)
if ($outputReset -lt $extraction) {
    throw "Editor NSIS must release its staged-payload directory handle before the worker anchors that source."
}
if ($main.Contains('SetOutPath "$INSTDIR"')) {
    throw "Editor NSIS must not retain a current-directory handle on the installation during finalization or recovery."
}
$integrate = $finalize.IndexOf('integrate --product editor', [StringComparison]::Ordinal)
$commit = $finalize.IndexOf('commit --product editor', [StringComparison]::Ordinal)
$finalizeOutputReset = $finalize.LastIndexOf('SetOutPath "$PLUGINSDIR"', $integrate,
    [StringComparison]::Ordinal)
if ($integrate -lt 0 -or $commit -lt $integrate -or $finalizeOutputReset -lt 0 -or
    -not $finalize.Contains('Call RecoverPendingEditorInstall')) {
    throw "Editor NSIS finalization must journal shell integration before commit and recover on failure."
}
$recoverStart = $template.IndexOf('Function RecoverPendingEditorInstall', [StringComparison]::Ordinal)
$recoverEnd = $template.IndexOf('FunctionEnd', $recoverStart, [StringComparison]::Ordinal)
$recover = $template.Substring($recoverStart, $recoverEnd - $recoverStart)
if (-not $recover.Contains('SetOutPath "$PLUGINSDIR"')) {
    throw "Editor NSIS recovery must release every current-directory handle on the installation root."
}
if (-not $uninstall.Contains(
        'File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"') -or
    -not $uninstall.Contains('uninstall embedded worker extracted') -or
    $uninstall.Contains('CopyFiles') -or $uninstall.Contains('$INSTDIR\bin\KeireInstallWorker.exe')) {
    throw "The Editor uninstaller must execute only its build-time embedded worker."
}

$editorEntryPoint = Get-Content -LiteralPath (Join-Path $Root "KeireClient\Source\ClientApplication.cpp") -Raw
foreach ($contract in @('HandleClientCommandWithoutApplication', '--verify-installation',
        'ReadInstallerPackageManifest', 'InstallProduct::Editor')) {
    if (-not $editorEntryPoint.Contains($contract)) {
        throw "The real Editor executable verification path is missing '$contract'."
    }
}
$windowsCommon = Get-Content -LiteralPath (Join-Path $Windows "common.ps1") -Raw
if (-not ($windowsCommon.Contains('Invoke-WindowsExecutableCapture -Path $editorExecutable') -and
        $windowsCommon.Contains('-Arguments @("--verify-installation")'))) {
    throw "The Editor package gate must execute the real hidden verification command."
}

$workerMain = Get-Content -LiteralPath (Join-Path $Root "KeireInstallWorker\Source\Main.cpp") -Raw
foreach ($contract in @('--verify-installation', 'install-deferred', 'CommitInstallTransaction',
        '#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)', 'KEIRE_INSTALL_WORKER_INTERRUPT_AFTER')) {
    if (-not $workerMain.Contains($contract)) {
        throw "The Editor install worker command boundary is missing '$contract'."
    }
}
$workerPremake = Get-Content -LiteralPath (Join-Path $Root "KeireInstallWorker\premake5.lua") -Raw
if (-not ($workerPremake.Contains('filter "configurations:Debug or DebugASan"') -and
        $workerPremake.Contains('KEIRE_INSTALL_WORKER_FAULT_INJECTION'))) {
    throw "Install-worker fault injection must be restricted to test-capable configurations."
}
$editorPackage = Get-Content -LiteralPath (Join-Path $Windows "package-editor.ps1") -Raw
foreach ($contract in @('$installWorkerTarget', 'KEIRE_INSTALL_WORKER_INTERRUPT_AFTER',
        'contains test-only fault injection')) {
    if (-not $editorPackage.Contains($contract)) {
        throw "The Editor package must enforce the production install-worker boundary '$contract'."
    }
}

$workerRuntime = Join-Path $PSScriptRoot "test-install-worker-runtime-windows.ps1"
& (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $workerRuntime -Product editor
if ($LASTEXITCODE -ne 0) {
    throw "The Editor install-worker process matrix failed."
}
$nsisRuntime = Join-Path $PSScriptRoot "test-nsis-worker-runtime-windows.ps1"
& (Join-Path $PSHOME "pwsh.exe") -NoProfile -File $nsisRuntime -Product editor
if ($LASTEXITCODE -ne 0) {
    throw "The Editor worker-authority NSIS runtime matrix failed."
}

Write-Host "Windows Editor installer checks passed."
