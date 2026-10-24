Unicode True

!include "FileFunc.nsh"
!include "MUI2.nsh"
!include "nsDialogs.nsh"

!ifndef PRODUCT_IDENTIFIER
    !error "PRODUCT_IDENTIFIER is required."
!endif
!ifndef PRODUCT_DISPLAY_NAME
    !error "PRODUCT_DISPLAY_NAME is required."
!endif
!ifndef PRODUCT_VERSION
    !error "PRODUCT_VERSION is required."
!endif
!ifndef PRODUCT_FILE_VERSION
    !error "PRODUCT_FILE_VERSION is required."
!endif
!ifndef PRODUCT_ARCHITECTURE
    !error "PRODUCT_ARCHITECTURE is required."
!endif
!ifndef HUB_TARGET
    !error "HUB_TARGET is required."
!endif
!ifndef SOURCE_DIRECTORY
    !error "SOURCE_DIRECTORY is required."
!endif
!ifndef OUTPUT_PATH
    !error "OUTPUT_PATH is required."
!endif
!ifndef LICENSE_PATH
    !error "LICENSE_PATH is required."
!endif
!ifndef SETUP_ICON_PATH
    !error "SETUP_ICON_PATH is required."
!endif
!ifndef KEIRE_INSTALL_WORKER_AUTHORITY
    !error "KeireInstallWorker is the required Hub install and uninstall authority."
!endif

!define PRODUCT_NAME "${PRODUCT_DISPLAY_NAME} Hub"
!define INSTALL_FOLDER_NAME "${PRODUCT_IDENTIFIER} Hub"
!ifndef HUB_REGISTRY_KEY
    !define HUB_REGISTRY_KEY "Software\${PRODUCT_IDENTIFIER}\HubInstaller"
!endif
!ifndef INSTALL_OWNERSHIP_IDENTIFIER
    !define INSTALL_OWNERSHIP_IDENTIFIER "${PRODUCT_IDENTIFIER}"
!endif
!define INSTALL_MARKER "{B2499023-1E3C-4F87-A8D5-E8DFA0470B97}"
!define INSTALL_MARKER_CONTENT "${INSTALL_MARKER}|${INSTALL_OWNERSHIP_IDENTIFIER}"
!define HUB_INSTALLER_MUTEX_NAME "Local\${PRODUCT_IDENTIFIER}.KeireHubInstaller.Transaction"

Var KeireHubUpdateMode
Var KeireHubUpdateRoot
Var KeireHubUpdateResumeToken
Var KeireHubUpdateWaitProcess
Var KeireHubUpdateFromVersion
Var KeireHubUpdateToVersion
Var KeireHubInstallerMutex
Var KeireInstallWorkerPending
Var KeireHubStartMenuSelected
Var KeireHubDesktopSelected
Var KeireExtractionStarted
Var KeireHubExistingRoot
Var KeireHubMaintenance
Var KeireHubRepairChoice
Var KeireHubRemoveChoice
Var KeireHubRelocateChoice
Var KeireHubPreviousRemoved
Var KeireHubRepairMissing

!macro KeireHubTestTrace MESSAGE
!ifdef KEIRE_INSTALL_TEST_TRACE_PATH
    FileOpen $9 "${KEIRE_INSTALL_TEST_TRACE_PATH}" a
    FileSeek $9 0 END
    FileWrite $9 "${MESSAGE}$\r$\n"
    FileClose $9
!endif
!macroend

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_PATH}"
InstallDir "$LOCALAPPDATA\Programs\${INSTALL_FOLDER_NAME}"
InstallDirRegKey HKCU "${HUB_REGISTRY_KEY}" "InstallDirectory"
RequestExecutionLevel user
SetCompressor zlib
ManifestDPIAware true
Icon "${SETUP_ICON_PATH}"
UninstallIcon "${SETUP_ICON_PATH}"
BrandingText "${PRODUCT_DISPLAY_NAME}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${PRODUCT_FILE_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "CompanyName" "${PRODUCT_DISPLAY_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright Kéire Engine contributors"

!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${HUB_TARGET}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_DISPLAY_NAME} Hub"
!define MUI_CUSTOMFUNCTION_ABORT HubInstallerAbort

Function AcquireHubInstallerMutex
    StrCpy $KeireHubInstallerMutex "0"
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "${HUB_INSTALLER_MUTEX_NAME}")p.r0 ?e'
    Pop $1
    StrCpy $KeireHubInstallerMutex $0
    StrCmp $KeireHubInstallerMutex "0" HubInstallerMutexFailed
    StrCmp $1 "183" HubInstallerMutexBusy
!ifdef KEIRE_HUB_TEST_HOLD_MUTEX_MS
    Sleep ${KEIRE_HUB_TEST_HOLD_MUTEX_MS}
!endif
    Return
HubInstallerMutexBusy:
    System::Call 'kernel32::CloseHandle(p r0)'
    StrCpy $KeireHubInstallerMutex "0"
    MessageBox MB_ICONSTOP|MB_OK \
        "Another ${PRODUCT_NAME} installer is already running. No application files were changed." /SD IDOK
    Abort
HubInstallerMutexFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The ${PRODUCT_NAME} installer could not acquire its update lock. No application files were changed." /SD IDOK
    Abort
FunctionEnd

Function ReleaseHubInstallerMutex
    StrCmp $KeireHubInstallerMutex "0" HubInstallerMutexReleaseDone
    StrCpy $0 $KeireHubInstallerMutex
    System::Call 'kernel32::CloseHandle(p r0)'
    StrCpy $KeireHubInstallerMutex "0"
HubInstallerMutexReleaseDone:
FunctionEnd

Function RecoverPendingHubInstall
    StrCmp $KeireInstallWorkerPending "1" 0 RecoverPendingHubInstallDone
    SetOutPath "$PLUGINSDIR"
    DetailPrint "Restoring the previous Hub installation..."
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" recover --product hub --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "recover exit=$0 output=$1"
    StrCmp $0 "0" 0 RecoverPendingHubInstallFailed
    StrCpy $KeireInstallWorkerPending "0"
    Return
RecoverPendingHubInstallFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub installer could not restore the previous payload automatically. Transaction state was preserved." \
        /SD IDOK
RecoverPendingHubInstallDone:
FunctionEnd

Function HubInstallerAbort
    Call RecoverPendingHubInstall
FunctionEnd

Function .onInit
    Call AcquireHubInstallerMutex
    !insertmacro KeireHubTestTrace "onInit mutex acquired"
    StrCpy $KeireHubUpdateMode "0"
    StrCpy $KeireInstallWorkerPending "0"
    StrCpy $KeireHubStartMenuSelected "0"
    StrCpy $KeireHubDesktopSelected "0"
    ${GetParameters} $0
    ${GetOptions} $0 "/KEIRE_HUB_UPDATE=" $1
    IfErrors HubUpdateInitDone
    StrCmp $1 "1" 0 HubUpdateInitInvalid
    ${GetOptions} $0 "/INSTALL_ROOT=" $KeireHubUpdateRoot
    IfErrors HubUpdateInitInvalid
    ${GetOptions} $0 "/RESUME_TOKEN=" $KeireHubUpdateResumeToken
    IfErrors HubUpdateInitInvalid
    ${GetOptions} $0 "/WAIT_PROCESS=" $KeireHubUpdateWaitProcess
    IfErrors HubUpdateInitInvalid
    ${GetOptions} $0 "/FROM_VERSION=" $KeireHubUpdateFromVersion
    IfErrors HubUpdateInitInvalid
    ${GetOptions} $0 "/TO_VERSION=" $KeireHubUpdateToVersion
    IfErrors HubUpdateInitInvalid
    StrCmp $KeireHubUpdateRoot "" HubUpdateInitInvalid
    StrCmp $KeireHubUpdateResumeToken "" HubUpdateInitInvalid
    StrCmp $KeireHubUpdateWaitProcess "" HubUpdateInitInvalid
    StrCmp $KeireHubUpdateFromVersion "" HubUpdateInitInvalid
    StrCmp $KeireHubUpdateToVersion "" HubUpdateInitInvalid
    StrCmp $KeireHubUpdateToVersion "${PRODUCT_VERSION}" 0 HubUpdateInitInvalid
    IfFileExists "$KeireHubUpdateResumeToken" 0 HubUpdateInitInvalid
    StrCpy $INSTDIR $KeireHubUpdateRoot
    StrCpy $KeireHubUpdateMode "1"

    ; Wait for the verified running Hub process before asking the worker to replace any payload file.
    System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $KeireHubUpdateWaitProcess)p.r2 ?e'
    Pop $3
    StrCmp $2 "0" 0 HubUpdateWaitForProcess
    StrCmp $3 "87" HubUpdateInitDone
    MessageBox MB_ICONSTOP|MB_OK \
        "The running Hub process could not be observed safely. No application files were changed." /SD IDOK
    Call ReleaseHubInstallerMutex
    Abort
HubUpdateWaitForProcess:
    System::Call 'kernel32::WaitForSingleObject(p r2, i 120000)i.r3'
    System::Call 'kernel32::CloseHandle(p r2)'
    StrCmp $3 "0" HubUpdateInitDone
    MessageBox MB_ICONSTOP|MB_OK \
        "The running Hub did not close in time. No application files were changed." /SD IDOK
    Call ReleaseHubInstallerMutex
    Abort
HubUpdateInitInvalid:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub update request is incomplete. No application files were changed." /SD IDOK
    Call ReleaseHubInstallerMutex
    Abort
HubUpdateInitDone:
    StrCpy $KeireHubPreviousRemoved "0"
    StrCpy $KeireHubRepairMissing "0"
    StrCpy $KeireHubMaintenance ""
    StrCmp $KeireHubUpdateMode "1" HubMaintenanceInitDone
    ReadRegStr $KeireHubExistingRoot HKCU "${HUB_REGISTRY_KEY}" "InstallDirectory"
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/MAINTENANCE=" $KeireHubMaintenance
    IfErrors HubMaintenanceInitDone
    StrCmp $KeireHubExistingRoot "" HubMaintenanceInvalid
    StrCmp $KeireHubMaintenance "repair" HubMaintenanceUseExisting
    StrCmp $KeireHubMaintenance "uninstall" HubMaintenanceUseExisting
    StrCmp $KeireHubMaintenance "relocate" HubMaintenanceInitDone
HubMaintenanceInvalid:
    MessageBox MB_ICONSTOP|MB_OK "Maintenance requires an existing Hub and /MAINTENANCE=repair, uninstall, or relocate." /SD IDOK
    SetErrorLevel 1
    Call ReleaseHubInstallerMutex
    Abort
HubMaintenanceUseExisting:
    StrCpy $INSTDIR $KeireHubExistingRoot
HubMaintenanceInitDone:
FunctionEnd

Function .onGUIEnd
    Call RecoverPendingHubInstall
    Call ReleaseHubInstallerMutex
FunctionEnd

Function SkipUpdateDirectoryPage
    StrCmp $KeireHubUpdateMode "1" SkipHubDirectory
    StrCmp $KeireHubMaintenance "repair" SkipHubDirectory
    StrCmp $KeireHubMaintenance "uninstall" SkipHubDirectory SkipHubDirectoryDone
SkipHubDirectory:
    Abort
SkipHubDirectoryDone:
FunctionEnd

Function HubMaintenancePage
    StrCmp $KeireHubUpdateMode "1" HubMaintenancePageSkip
    StrCmp $KeireHubExistingRoot "" HubMaintenancePageSkip
    !insertmacro MUI_HEADER_TEXT "Existing Hub installation" "Choose how to manage the installed Hub."
    nsDialogs::Create 1018
    Pop $0
    StrCmp $0 "error" HubMaintenancePageSkip
    ${NSD_CreateLabel} 0 0 100% 30u "Installed at:$\r$\n$KeireHubExistingRoot"
    Pop $0
    ${NSD_CreateRadioButton} 0 38u 100% 14u "Repair / update this installation"
    Pop $KeireHubRepairChoice
    ${NSD_CreateRadioButton} 0 58u 100% 14u "Uninstall the Hub"
    Pop $KeireHubRemoveChoice
    ${NSD_CreateRadioButton} 0 78u 100% 14u "Install in a new location (remove the current Hub first)"
    Pop $KeireHubRelocateChoice
    ${NSD_CreateLabel} 0 100u 100% 52u "Close the Hub before continuing. Projects and unrelated files are preserved. Moving is uninstall followed by install, not an atomic move. If the new install fails after removal, run setup again."
    Pop $0
    StrCmp $KeireHubMaintenance "uninstall" HubMaintenanceCheckRemove
    StrCmp $KeireHubMaintenance "relocate" HubMaintenanceCheckRelocate
    ${NSD_Check} $KeireHubRepairChoice
    Goto HubMaintenanceShow
HubMaintenanceCheckRemove:
    ${NSD_Check} $KeireHubRemoveChoice
    Goto HubMaintenanceShow
HubMaintenanceCheckRelocate:
    ${NSD_Check} $KeireHubRelocateChoice
HubMaintenanceShow:
    nsDialogs::Show
    Return
HubMaintenancePageSkip:
    Abort
FunctionEnd

Function HubMaintenancePageLeave
    ${NSD_GetState} $KeireHubRelocateChoice $0
    StrCmp $0 ${BST_CHECKED} HubMaintenanceSelectRelocate
    ${NSD_GetState} $KeireHubRemoveChoice $0
    StrCmp $0 ${BST_CHECKED} HubMaintenanceSelectRemove
    StrCpy $KeireHubMaintenance "repair"
    StrCpy $INSTDIR $KeireHubExistingRoot
    Return
HubMaintenanceSelectRemove:
    StrCpy $KeireHubMaintenance "uninstall"
    StrCpy $INSTDIR $KeireHubExistingRoot
    Return
HubMaintenanceSelectRelocate:
    StrCpy $KeireHubMaintenance "relocate"
FunctionEnd

Function SkipUninstallComponents
    StrCmp $KeireHubMaintenance "uninstall" 0 +2
    Abort
FunctionEnd

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_PATH}"
Page custom HubMaintenancePage HubMaintenancePageLeave
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipUninstallComponents
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipUpdateDirectoryPage
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "${PRODUCT_NAME} (required)" MainSection
    SectionIn RO
    SetShellVarContext current
    ; NSIS only gathers the complete payload. The worker classifies the destination and owns every mutation.
    !insertmacro KeireHubTestTrace "main begin root=$INSTDIR"
    InitPluginsDir
    SetOverwrite try
    SetOutPath "$PLUGINSDIR"
    File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"
    !insertmacro KeireHubTestTrace "embedded worker extracted"
    DetailPrint "Checking the installation folder before extracting the Hub..."
    ; Modern uninstall validates ownership itself and preserves modified files. Full repair preparation would
    ; reject that drift before uninstall could preserve it. Legacy roots still need worker-owned migration.
    StrCmp $KeireHubMaintenance "uninstall" HubPrepareRemoval
    StrCmp $KeireHubMaintenance "relocate" HubPrepareRemoval
    Goto HubPrepareDestination
HubPrepareRemoval:
    IfFileExists "$KeireHubExistingRoot\.keire-install-receipt.json" HubDestinationReady
    StrCmp $KeireHubMaintenance "relocate" HubPreparePrevious
    Goto HubPrepareDestination
HubPreparePrevious:
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" prepare --product hub --root "$KeireHubExistingRoot"'
    Goto HubPreparedOutput
HubPrepareDestination:
    StrCmp $KeireHubMaintenance "repair" 0 +2
    StrCpy $KeireHubRepairMissing "1"
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" prepare --product hub --root "$INSTDIR" --repair-missing $KeireHubRepairMissing'
HubPreparedOutput:
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "prepare exit=$0 output=$1"
    StrCmp $0 "0" HubDestinationReady
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub cannot be installed in this folder.$\r$\n$\r$\n$1$\r$\n$\r$\nNo new Hub payload was extracted. Restart setup after correcting the folder or installation." /SD IDOK
    SetErrorLevel 1
    Abort
HubDestinationReady:
    StrCmp $KeireHubMaintenance "uninstall" HubRemoveOnly
    Goto HubExtractPayload
HubRemoveOnly:
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" uninstall --product hub --root "$KeireHubExistingRoot"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "maintenance uninstall exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerFailed
    MessageBox MB_ICONINFORMATION|MB_OK "The Hub was uninstalled. Projects and unrelated files were preserved." /SD IDOK
    SetErrorLevel 0
    Quit
HubExtractPayload:
    System::Call 'kernel32::GetTickCount()i .r0'
    StrCpy $KeireExtractionStarted $0
    DetailPrint "Extracting Hub files..."
    SetDetailsPrint none
    SetOutPath "$PLUGINSDIR\payload"
    File /r "${SOURCE_DIRECTORY}\*"
    SetDetailsPrint both
    System::Call 'kernel32::GetTickCount()i .r0'
    IntOp $0 $0 - $KeireExtractionStarted
    DetailPrint "Hub extraction elapsed_ms=$0"
    !insertmacro KeireHubTestTrace "payload extracted"
    FileOpen $0 "$PLUGINSDIR\payload\.keire-hub-install" w
    IfErrors HubInstallWorkerFailed
    FileWrite $0 "${INSTALL_MARKER_CONTENT}$\r$\n"
    FileClose $0
    !insertmacro KeireHubTestTrace "legacy marker generated"
    WriteUninstaller "$PLUGINSDIR\payload\Uninstall.exe"
    IfErrors HubInstallWorkerFailed
    !insertmacro KeireHubTestTrace "uninstaller generated"
    ; Release NSIS's current-directory handle before the worker pins the staged source without following links.
    SetOutPath "$PLUGINSDIR"
!ifdef KEIRE_HUB_TEST_FAIL_DURING_STAGE
    Goto HubInstallWorkerFailed
!endif
    StrCmp $KeireHubMaintenance "relocate" 0 HubInstallPayload
    DetailPrint "Validating the new location and payload before removing the previous Hub..."
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" validate-relocation --product hub --source "$PLUGINSDIR\payload" --root "$INSTDIR" --previous-root "$KeireHubExistingRoot"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "relocation preflight exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerFailed
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" uninstall --product hub --root "$KeireHubExistingRoot"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "relocation uninstall exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerFailed
    StrCpy $KeireHubPreviousRemoved "1"
HubInstallPayload:
    DetailPrint "Verifying and installing the Hub payload..."
    nsExec::ExecToLog '"$PLUGINSDIR\KeireInstallWorker.exe" install-deferred --product hub --source "$PLUGINSDIR\payload" --root "$INSTDIR" --repair-missing $KeireHubRepairMissing'
    Pop $0
    StrCpy $1 "See the installation details for the completed phases and any file verification errors."
    !insertmacro KeireHubTestTrace "install-deferred exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerRecover
    StrCpy $KeireInstallWorkerPending "1"
    nsExec::ExecToStack /TIMEOUT=30000 '"$INSTDIR\bin\${HUB_TARGET}.exe" --verify-installation'
    Pop $0
    Pop $1
    !insertmacro KeireHubTestTrace "binary verification exit=$0 output=$1"
    StrCmp $0 "0" HubInstallWorkerPrepared
HubInstallWorkerRecover:
    Call RecoverPendingHubInstall
HubInstallWorkerFailed:
    SetErrorLevel 1
    StrCmp $KeireHubPreviousRemoved "1" 0 HubInstallPreviousPreserved
    MessageBox MB_ICONSTOP|MB_OK \
        "The previous Hub was removed, but the new installation did not finish. Projects and unrelated files were preserved. Run setup again to install the Hub." /SD IDOK
    Abort
HubInstallPreviousPreserved:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub operation did not finish. See the installation details for the rejected or recovered step. Unrelated files were preserved." \
        /SD IDOK
    Abort
HubInstallWorkerPrepared:
    SetOutPath "$PLUGINSDIR"
SectionEnd

Section "Start Menu shortcuts" StartMenuSection
    StrCpy $KeireHubStartMenuSelected "1"
SectionEnd

Section /o "Desktop shortcut" DesktopSection
    StrCpy $KeireHubDesktopSelected "1"
SectionEnd

Section "-Finalize Hub installation" HubInstallWorkerCommitSection
    SectionIn RO
    StrCmp $KeireInstallWorkerPending "1" 0 HubInstallWorkerCommitDone
    SetOutPath "$PLUGINSDIR"
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" integrate --product hub --root "$INSTDIR" --start-menu $KeireHubStartMenuSelected --desktop $KeireHubDesktopSelected'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "integrate exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerShellRollback
    DetailPrint "Finalizing the Hub installation..."
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" commit --product hub --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "commit exit=$0 output=$1"
    StrCmp $0 "0" 0 HubInstallWorkerShellRollback
    StrCpy $KeireInstallWorkerPending "0"
    Goto HubInstallWorkerCommitDone
HubInstallWorkerShellRollback:
    Call RecoverPendingHubInstall
    SetErrorLevel 1
    StrCmp $KeireHubPreviousRemoved "1" 0 HubInstallWorkerRepairRollback
    MessageBox MB_ICONSTOP|MB_OK \
        "The new Hub installation could not be finalized and was rolled back when safe. The previous Hub was already removed. Run setup again; projects and unrelated files were preserved." /SD IDOK
    Abort
HubInstallWorkerRepairRollback:
    MessageBox MB_ICONSTOP|MB_OK \
        "Hub shortcut creation or final commit failed. The previous installation was restored when safe." /SD IDOK
    Abort
HubInstallWorkerCommitDone:
SectionEnd

LangString DESC_MainSection ${LANG_ENGLISH} \
    "Installs the standalone Hub, its private task worker, templates, learning content, and notices."
LangString DESC_StartMenuSection ${LANG_ENGLISH} "Creates shortcuts in the current user's Start Menu."
LangString DESC_DesktopSection ${LANG_ENGLISH} "Creates a shortcut to the Hub on the current user's desktop."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${MainSection} $(DESC_MainSection)
    !insertmacro MUI_DESCRIPTION_TEXT ${StartMenuSection} $(DESC_StartMenuSection)
    !insertmacro MUI_DESCRIPTION_TEXT ${DesktopSection} $(DESC_DesktopSection)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    SetShellVarContext current
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    ; Embed the build-time worker. Never execute the mutable copy in the installation being removed.
    File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"
    IfErrors UnsafeUninstall
    !insertmacro KeireHubTestTrace "uninstall embedded worker extracted"
    DetailPrint "Removing verified Hub files and preserving user content..."
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" uninstall --product hub --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireHubTestTrace "uninstall exit=$0 output=$1"
    StrCmp $0 "0" HubWorkerUninstalled UnsafeUninstall
HubWorkerUninstalled:
    Return
UnsafeUninstall:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub installation directory could not be verified. No application files were removed." /SD IDOK
    SetErrorLevel 1
    Quit
SectionEnd
