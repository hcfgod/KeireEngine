Unicode True

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
!ifndef CLIENT_TARGET
    !error "CLIENT_TARGET is required."
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
    !error "KeireInstallWorker is the required Editor install and uninstall authority."
!endif

!define PRODUCT_NAME "${PRODUCT_DISPLAY_NAME} Editor"
!define INSTALL_FOLDER_NAME "${PRODUCT_IDENTIFIER} Editor"
!define PRODUCT_REGISTRY_KEY "Software\${PRODUCT_IDENTIFIER}\Editor"
!ifndef INSTALL_OWNERSHIP_IDENTIFIER
    !define INSTALL_OWNERSHIP_IDENTIFIER "${PRODUCT_IDENTIFIER}"
!endif
!define INSTALL_MARKER "{1D37B84D-13B7-4C73-96BD-6D23AD40757A}"
!define INSTALL_MARKER_CONTENT "${INSTALL_MARKER}|${INSTALL_OWNERSHIP_IDENTIFIER}"
!define EDITOR_INSTALLER_MUTEX_NAME "Local\${PRODUCT_IDENTIFIER}.KeireEditorInstaller.Transaction"

Var KeireEditorInstallerMutex
Var KeireInstallWorkerPending
Var KeireEditorStartMenuSelected
Var KeireEditorDesktopSelected

!macro KeireEditorTestTrace MESSAGE
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
InstallDirRegKey HKCU "${PRODUCT_REGISTRY_KEY}" "InstallDirectory"
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
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${CLIENT_TARGET}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_DISPLAY_NAME} Editor"
!define MUI_CUSTOMFUNCTION_ABORT EditorInstallerAbort

Function AcquireEditorInstallerMutex
    StrCpy $KeireEditorInstallerMutex "0"
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "${EDITOR_INSTALLER_MUTEX_NAME}")p.r0 ?e'
    Pop $1
    StrCpy $KeireEditorInstallerMutex $0
    StrCmp $KeireEditorInstallerMutex "0" EditorInstallerMutexFailed
    StrCmp $1 "183" EditorInstallerMutexBusy
    Return
EditorInstallerMutexBusy:
    System::Call 'kernel32::CloseHandle(p r0)'
    StrCpy $KeireEditorInstallerMutex "0"
    MessageBox MB_ICONSTOP|MB_OK \
        "Another ${PRODUCT_NAME} installer is already running. No application files were changed." /SD IDOK
    Abort
EditorInstallerMutexFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The ${PRODUCT_NAME} installer could not acquire its transaction lock. No application files were changed." \
        /SD IDOK
    Abort
FunctionEnd

Function ReleaseEditorInstallerMutex
    StrCmp $KeireEditorInstallerMutex "0" EditorInstallerMutexReleaseDone
    StrCpy $0 $KeireEditorInstallerMutex
    System::Call 'kernel32::CloseHandle(p r0)'
    StrCpy $KeireEditorInstallerMutex "0"
EditorInstallerMutexReleaseDone:
FunctionEnd

Function RecoverPendingEditorInstall
    StrCmp $KeireInstallWorkerPending "1" 0 RecoverPendingEditorInstallDone
    SetOutPath "$PLUGINSDIR"
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" recover --product editor --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireEditorTestTrace "recover exit=$0 output=$1"
    StrCmp $0 "0" 0 RecoverPendingEditorInstallFailed
    StrCpy $KeireInstallWorkerPending "0"
    Return
RecoverPendingEditorInstallFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Editor installer could not restore the previous payload automatically. Transaction state was preserved." \
        /SD IDOK
RecoverPendingEditorInstallDone:
FunctionEnd

Function EditorInstallerAbort
    Call RecoverPendingEditorInstall
FunctionEnd

Function .onInit
    Call AcquireEditorInstallerMutex
    !insertmacro KeireEditorTestTrace "onInit mutex acquired"
    StrCpy $KeireInstallWorkerPending "0"
    StrCpy $KeireEditorStartMenuSelected "0"
    StrCpy $KeireEditorDesktopSelected "0"
FunctionEnd

Function .onGUIEnd
    Call RecoverPendingEditorInstall
    Call ReleaseEditorInstallerMutex
FunctionEnd

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_PATH}"
!insertmacro MUI_PAGE_COMPONENTS
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
    !insertmacro KeireEditorTestTrace "main begin root=$INSTDIR"
    InitPluginsDir
    SetOverwrite try
    SetOutPath "$PLUGINSDIR"
    File /oname=KeireInstallWorker.exe "${SOURCE_DIRECTORY}\bin\KeireInstallWorker.exe"
    !insertmacro KeireEditorTestTrace "embedded worker extracted"
    SetOutPath "$PLUGINSDIR\payload"
    File /r "${SOURCE_DIRECTORY}\*"
    !insertmacro KeireEditorTestTrace "payload extracted"
    FileOpen $0 "$PLUGINSDIR\payload\.keire-editor-install" w
    IfErrors EditorInstallWorkerFailed
    FileWrite $0 "${INSTALL_MARKER_CONTENT}$\r$\n"
    FileClose $0
    !insertmacro KeireEditorTestTrace "legacy marker generated"
    WriteUninstaller "$PLUGINSDIR\payload\Uninstall.exe"
    IfErrors EditorInstallWorkerFailed
    !insertmacro KeireEditorTestTrace "uninstaller generated"
    ; Release NSIS's current-directory handle before the worker pins the staged source without following links.
    SetOutPath "$PLUGINSDIR"
!ifdef KEIRE_EDITOR_TEST_FAIL_DURING_STAGE
    Goto EditorInstallWorkerFailed
!endif
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" install-deferred --product editor --source "$PLUGINSDIR\payload" --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireEditorTestTrace "install-deferred exit=$0 output=$1"
    StrCmp $0 "0" 0 EditorInstallWorkerRecover
    StrCpy $KeireInstallWorkerPending "1"
    nsExec::ExecToStack /TIMEOUT=30000 '"$INSTDIR\bin\${CLIENT_TARGET}.exe" --verify-installation'
    Pop $0
    Pop $1
    !insertmacro KeireEditorTestTrace "binary verification exit=$0 output=$1"
    StrCmp $0 "0" EditorInstallWorkerPrepared
EditorInstallWorkerRecover:
    Call RecoverPendingEditorInstall
EditorInstallWorkerFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Editor install worker rejected or rolled back the transaction. Existing and unrelated files were preserved." \
        /SD IDOK
    Abort
EditorInstallWorkerPrepared:
    SetOutPath "$PLUGINSDIR"
SectionEnd

Section "Start Menu shortcuts" StartMenuSection
    StrCpy $KeireEditorStartMenuSelected "1"
SectionEnd

Section /o "Desktop shortcut" DesktopSection
    StrCpy $KeireEditorDesktopSelected "1"
SectionEnd

Section "-Finalize Editor installation" EditorInstallWorkerCommitSection
    SectionIn RO
    StrCmp $KeireInstallWorkerPending "1" 0 EditorInstallWorkerCommitDone
    SetOutPath "$PLUGINSDIR"
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" integrate --product editor --root "$INSTDIR" --start-menu $KeireEditorStartMenuSelected --desktop $KeireEditorDesktopSelected'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireEditorTestTrace "integrate exit=$0 output=$1"
    StrCmp $0 "0" 0 EditorInstallWorkerShellRollback
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" commit --product editor --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireEditorTestTrace "commit exit=$0 output=$1"
    StrCmp $0 "0" 0 EditorInstallWorkerShellRollback
    StrCpy $KeireInstallWorkerPending "0"
    Goto EditorInstallWorkerCommitDone
EditorInstallWorkerShellRollback:
    Call RecoverPendingEditorInstall
    MessageBox MB_ICONSTOP|MB_OK \
        "Editor shortcut creation or final commit failed. The previous installation was restored when safe." /SD IDOK
    Abort
EditorInstallWorkerCommitDone:
SectionEnd

LangString DESC_MainSection ${LANG_ENGLISH} "Installs the editor, build tools, samples, and bundled managed SDK."
LangString DESC_StartMenuSection ${LANG_ENGLISH} "Creates shortcuts in the current user's Start Menu."
LangString DESC_DesktopSection ${LANG_ENGLISH} "Creates an editor shortcut on the current user's desktop."

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
    !insertmacro KeireEditorTestTrace "uninstall embedded worker extracted"
    nsExec::ExecToStack '"$PLUGINSDIR\KeireInstallWorker.exe" uninstall --product editor --root "$INSTDIR"'
    Pop $0
    Pop $1
    DetailPrint "$1"
    !insertmacro KeireEditorTestTrace "uninstall exit=$0 output=$1"
    StrCmp $0 "0" EditorWorkerUninstalled UnsafeUninstall
EditorWorkerUninstalled:
    Return
UnsafeUninstall:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Editor installation directory could not be verified. No application files were removed." /SD IDOK
    SetErrorLevel 1
    Quit
SectionEnd
