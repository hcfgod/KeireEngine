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

!define PRODUCT_NAME "${PRODUCT_DISPLAY_NAME} Editor"
!define INSTALL_FOLDER_NAME "${PRODUCT_IDENTIFIER} Editor"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_IDENTIFIER}Editor"
!define INSTALL_MARKER "{1D37B84D-13B7-4C73-96BD-6D23AD40757A}"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_PATH}"
InstallDir "$LOCALAPPDATA\Programs\${INSTALL_FOLDER_NAME}"
InstallDirRegKey HKCU "Software\${PRODUCT_IDENTIFIER}\Editor" "InstallDirectory"
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

!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${CLIENT_TARGET}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_DISPLAY_NAME} Editor"

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
    SetOutPath "$INSTDIR"
    File /r "${SOURCE_DIRECTORY}\*"

    FileOpen $0 "$INSTDIR\.keire-editor-install" w
    FileWrite $0 "${INSTALL_MARKER}$\r$\n"
    FileClose $0

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\${PRODUCT_IDENTIFIER}\Editor" "InstallDirectory" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bin\${CLIENT_TARGET}.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_DISPLAY_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Start Menu shortcuts" StartMenuSection
    SetShellVarContext current
    CreateDirectory "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Editor"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Editor\${PRODUCT_DISPLAY_NAME} Editor.lnk" \
        "$INSTDIR\bin\${CLIENT_TARGET}.exe" "" "$INSTDIR\bin\${CLIENT_TARGET}.exe" 0 SW_SHOWNORMAL "" \
        "Open ${PRODUCT_DISPLAY_NAME} Editor"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Editor\Uninstall ${PRODUCT_DISPLAY_NAME} Editor.lnk" \
        "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "Desktop shortcut" DesktopSection
    SetShellVarContext current
    CreateShortcut "$DESKTOP\${PRODUCT_DISPLAY_NAME} Editor.lnk" "$INSTDIR\bin\${CLIENT_TARGET}.exe" "" \
        "$INSTDIR\bin\${CLIENT_TARGET}.exe" 0 SW_SHOWNORMAL "" "Open ${PRODUCT_DISPLAY_NAME} Editor"
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
    ReadRegStr $0 HKCU "Software\${PRODUCT_IDENTIFIER}\Editor" "InstallDirectory"
    StrCmp $0 "$INSTDIR" 0 UnsafeUninstall
    IfFileExists "$INSTDIR\.keire-editor-install" 0 UnsafeUninstall
    ${GetFileName} "$INSTDIR" $1
    StrCmp $1 "${INSTALL_FOLDER_NAME}" 0 UnsafeUninstall

    Delete "$DESKTOP\${PRODUCT_DISPLAY_NAME} Editor.lnk"
    RMDir /r "$SMPROGRAMS\${PRODUCT_DISPLAY_NAME} Editor"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\${PRODUCT_IDENTIFIER}\Editor"
    RMDir /r "$INSTDIR"
    Return

UnsafeUninstall:
    MessageBox MB_ICONSTOP|MB_OK "The installation directory could not be verified. No application files were removed."
    Abort
SectionEnd
