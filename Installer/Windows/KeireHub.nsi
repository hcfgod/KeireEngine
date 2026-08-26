Unicode True

!include "FileFunc.nsh"
!include "MUI2.nsh"

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
!define HUB_REGISTRY_KEY "Software\${PRODUCT_IDENTIFIER}\HubInstaller"
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
SetCompressor /SOLID lzma
SetCompressorDictSize 64
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
FunctionEnd

Function .onGUIEnd
    Call RecoverPendingHubInstall
    Call ReleaseHubInstallerMutex
FunctionEnd

Function SkipUpdateDirectoryPage
    StrCmp $KeireHubUpdateMode "1" 0 +2
    Abort
FunctionEnd

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_PATH}"
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
    SetOutPath "$PLUGINSDIR\payload"
    File /r "${SOURCE_DIRECTORY}\*"
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
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" install-deferred --product hub --source "$PLUGINSDIR\payload" --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
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
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub install worker rejected or rolled back the transaction. Existing and unrelated files were preserved." \
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
