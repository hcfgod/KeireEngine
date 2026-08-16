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

!define PRODUCT_NAME "${PRODUCT_DISPLAY_NAME} Hub"
!define INSTALL_FOLDER_NAME "${PRODUCT_IDENTIFIER} Hub"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_IDENTIFIER}Hub"
!define HUB_PROTOCOL_KEY "Software\Classes\keirehub"
!define INSTALL_MARKER "{B2499023-1E3C-4F87-A8D5-E8DFA0470B97}"

Var KeireHubUpdateMode
Var KeireHubUpdateRoot
Var KeireHubUpdateResumeToken
Var KeireHubUpdateWaitProcess
Var KeireHubUpdateFromVersion
Var KeireHubUpdateToVersion

!macro RemoveHubPayload
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\Config"
    RMDir /r "$INSTDIR\content"
    RMDir /r "$INSTDIR\Docs"
    RMDir /r "$INSTDIR\samples"
    RMDir /r "$INSTDIR\third-party"
    Delete "$INSTDIR\CHANGELOG.md"
    Delete "$INSTDIR\hub-package.json"
    Delete "$INSTDIR\Launch-KeireHub.cmd"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
    Delete "$INSTDIR\.keire-hub-install"
    Delete "$INSTDIR\Uninstall.exe"
!macroend

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_PATH}"
InstallDir "$LOCALAPPDATA\Programs\${INSTALL_FOLDER_NAME}"
InstallDirRegKey HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
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

Function .onInit
    StrCpy $KeireHubUpdateMode "0"
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
    IfFileExists "$KeireHubUpdateResumeToken" 0 HubUpdateInitInvalid
    StrCpy $INSTDIR $KeireHubUpdateRoot
    StrCpy $KeireHubUpdateMode "1"

    ; Wait for the verified running Hub process before replacing any payload file.
    System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $KeireHubUpdateWaitProcess)i.r2'
    StrCmp $2 "0" HubUpdateInitDone
    System::Call 'kernel32::WaitForSingleObject(p r2, i 120000)i.r3'
    System::Call 'kernel32::CloseHandle(p r2)'
    StrCmp $3 "0" HubUpdateInitDone
    MessageBox MB_ICONSTOP|MB_OK "The running Hub did not close in time. No application files were changed."
    Abort

HubUpdateInitInvalid:
    MessageBox MB_ICONSTOP|MB_OK "The Hub update request is incomplete. No application files were changed."
    Abort
HubUpdateInitDone:
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
    SetOverwrite on
    SetOutPath "$INSTDIR"
    StrCmp $KeireHubUpdateMode "1" 0 NormalInstallGuard
    ReadRegStr $0 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    StrCmp $0 "$INSTDIR" 0 UnsafeUpdate
    IfFileExists "$INSTDIR\.keire-hub-install" 0 UnsafeUpdate
    Goto RemoveExistingPayload
NormalInstallGuard:
    IfFileExists "$INSTDIR\.keire-hub-install" 0 InstallPayload
    ReadRegStr $0 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    StrCmp $0 "$INSTDIR" 0 InstallPayload
RemoveExistingPayload:
    !insertmacro RemoveHubPayload
    Goto InstallPayload
UnsafeUpdate:
    MessageBox MB_ICONSTOP|MB_OK "The existing Hub installation could not be verified. No application files were changed."
    Abort
InstallPayload:
    File /r "${SOURCE_DIRECTORY}\*"

    FileOpen $0 "$INSTDIR\.keire-hub-install" w
    FileWrite $0 "${INSTALL_MARKER}$\r$\n"
    FileClose $0

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bin\${HUB_TARGET}.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_DISPLAY_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}" "" "URL:${PRODUCT_NAME} Protocol"
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}" "URL Protocol" ""
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}\DefaultIcon" "" "$INSTDIR\bin\${HUB_TARGET}.exe,0"
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" "" \
        '"$INSTDIR\bin\${HUB_TARGET}.exe" "%1"'
SectionEnd

Section "Start Menu shortcuts" StartMenuSection
    SetShellVarContext current
    CreateDirectory "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Hub"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Hub\${PRODUCT_DISPLAY_NAME} Hub.lnk" \
        "$INSTDIR\bin\${HUB_TARGET}.exe" "" "$INSTDIR\bin\${HUB_TARGET}.exe" 0 SW_SHOWNORMAL "" \
        "Open ${PRODUCT_DISPLAY_NAME} Hub"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Hub\Uninstall ${PRODUCT_DISPLAY_NAME} Hub.lnk" \
        "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" DesktopSection
    SetShellVarContext current
    CreateShortcut "$DESKTOP\${PRODUCT_DISPLAY_NAME} Hub.lnk" "$INSTDIR\bin\${HUB_TARGET}.exe" "" \
        "$INSTDIR\bin\${HUB_TARGET}.exe" 0 SW_SHOWNORMAL "" "Open ${PRODUCT_DISPLAY_NAME} Hub"
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
    ReadRegStr $0 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    StrCmp $0 "$INSTDIR" 0 UnsafeUninstall
    IfFileExists "$INSTDIR\.keire-hub-install" 0 UnsafeUninstall
    ${GetFileName} "$INSTDIR" $1
    StrCmp $1 "${INSTALL_FOLDER_NAME}" 0 UnsafeUninstall

    ReadRegStr $2 HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" ""
    StrCmp $2 '"$INSTDIR\bin\${HUB_TARGET}.exe" "%1"' 0 KeepHubProtocolRegistration
    DeleteRegKey HKCU "${HUB_PROTOCOL_KEY}"
KeepHubProtocolRegistration:

    !insertmacro RemoveHubPayload
    Delete "$DESKTOP\${PRODUCT_DISPLAY_NAME} Hub.lnk"
    RMDir /r "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Hub"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller"
    ; Unknown files and user preferences, caches, projects, and editor roots are intentionally preserved.
    RMDir "$INSTDIR"
    Return

UnsafeUninstall:
    MessageBox MB_ICONSTOP|MB_OK "The Hub installation directory could not be verified. No application files were removed."
    Abort
SectionEnd
