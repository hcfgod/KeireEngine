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
!ifndef HUB_PROTOCOL_KEY
    !define HUB_PROTOCOL_KEY "Software\Classes\keirehub"
!endif
!define INSTALL_MARKER "{B2499023-1E3C-4F87-A8D5-E8DFA0470B97}"

Var KeireHubUpdateMode
Var KeireHubUpdateRoot
Var KeireHubUpdateResumeToken
Var KeireHubUpdateWaitProcess
Var KeireHubUpdateFromVersion
Var KeireHubUpdateToVersion
Var KeireHubTransactionRoot
Var KeireHubTransactionStage
Var KeireHubTransactionBackup
Var KeireHubTransactionDiscard
Var KeireHubTransactionCleanup
Var KeireHubTransactionCleanupOwner
Var KeireHubTransactionJournal
Var KeireHubTransactionActive
Var KeireHubTransactionFailure
Var KeireHubInstallerMutex

!define HUB_TRANSACTION_MARKER "{20F0986D-BC31-4319-A004-68C43FD72F5B}"
!define HUB_INSTALLER_MUTEX_NAME "Local\${PRODUCT_IDENTIFIER}.KeireHubInstaller.Transaction"
!define FILE_ATTRIBUTE_DIRECTORY 0x00000010
!define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400

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

!macro RecordHubTransactionPayload
    Push "bin"
    Call RecordHubTransactionEntry
    Push "Config"
    Call RecordHubTransactionEntry
    Push "content"
    Call RecordHubTransactionEntry
    Push "Docs"
    Call RecordHubTransactionEntry
    Push "Samples"
    Call RecordHubTransactionEntry
    Push "third-party"
    Call RecordHubTransactionEntry
    Push "CHANGELOG.md"
    Call RecordHubTransactionEntry
    Push "hub-package.json"
    Call RecordHubTransactionEntry
    Push "Launch-KeireHub.cmd"
    Call RecordHubTransactionEntry
    Push "LICENSE.txt"
    Call RecordHubTransactionEntry
    Push "README.md"
    Call RecordHubTransactionEntry
    Push "THIRD_PARTY_NOTICES.md"
    Call RecordHubTransactionEntry
    Push ".keire-hub-install"
    Call RecordHubTransactionEntry
    Push "Uninstall.exe"
    Call RecordHubTransactionEntry
!macroend

!macro ValidateRequiredHubPayload VALIDATOR
    Push "bin\${HUB_TARGET}.exe"
    Call ${VALIDATOR}
    Push "bin"
    Call ${VALIDATOR}
    Push "Config"
    Call ${VALIDATOR}
    Push "content"
    Call ${VALIDATOR}
    Push "Docs"
    Call ${VALIDATOR}
    Push "Samples"
    Call ${VALIDATOR}
    Push "third-party"
    Call ${VALIDATOR}
    Push "CHANGELOG.md"
    Call ${VALIDATOR}
    Push "hub-package.json"
    Call ${VALIDATOR}
    Push "Launch-KeireHub.cmd"
    Call ${VALIDATOR}
    Push "LICENSE.txt"
    Call ${VALIDATOR}
    Push "README.md"
    Call ${VALIDATOR}
    Push "THIRD_PARTY_NOTICES.md"
    Call ${VALIDATOR}
    Push ".keire-hub-install"
    Call ${VALIDATOR}
    Push "Uninstall.exe"
    Call ${VALIDATOR}
!macroend

!macro ValidateHubTransactionStage
    !insertmacro ValidateRequiredHubPayload ValidateHubTransactionStageEntry
!macroend

!macro BackupHubTransactionPayload
    Push "bin"
    Call BackupHubTransactionEntry
    Push "Config"
    Call BackupHubTransactionEntry
    Push "content"
    Call BackupHubTransactionEntry
    Push "Docs"
    Call BackupHubTransactionEntry
    Push "Samples"
    Call BackupHubTransactionEntry
    Push "third-party"
    Call BackupHubTransactionEntry
    Push "CHANGELOG.md"
    Call BackupHubTransactionEntry
    Push "hub-package.json"
    Call BackupHubTransactionEntry
    Push "Launch-KeireHub.cmd"
    Call BackupHubTransactionEntry
    Push "LICENSE.txt"
    Call BackupHubTransactionEntry
    Push "README.md"
    Call BackupHubTransactionEntry
    Push "THIRD_PARTY_NOTICES.md"
    Call BackupHubTransactionEntry
    Push ".keire-hub-install"
    Call BackupHubTransactionEntry
    Push "Uninstall.exe"
    Call BackupHubTransactionEntry
!macroend

!macro PromoteRemainingHubTransactionPayload
    Push "Config"
    Call PromoteHubTransactionEntry
    Push "content"
    Call PromoteHubTransactionEntry
    Push "Docs"
    Call PromoteHubTransactionEntry
    Push "Samples"
    Call PromoteHubTransactionEntry
    Push "third-party"
    Call PromoteHubTransactionEntry
    Push "CHANGELOG.md"
    Call PromoteHubTransactionEntry
    Push "hub-package.json"
    Call PromoteHubTransactionEntry
    Push "Launch-KeireHub.cmd"
    Call PromoteHubTransactionEntry
    Push "LICENSE.txt"
    Call PromoteHubTransactionEntry
    Push "README.md"
    Call PromoteHubTransactionEntry
    Push "THIRD_PARTY_NOTICES.md"
    Call PromoteHubTransactionEntry
    Push ".keire-hub-install"
    Call PromoteHubTransactionEntry
    Push "Uninstall.exe"
    Call PromoteHubTransactionEntry
!macroend

!macro RestoreHubTransactionPayload
    Push "bin"
    Call RestoreHubTransactionEntry
    Push "Config"
    Call RestoreHubTransactionEntry
    Push "content"
    Call RestoreHubTransactionEntry
    Push "Docs"
    Call RestoreHubTransactionEntry
    Push "Samples"
    Call RestoreHubTransactionEntry
    Push "third-party"
    Call RestoreHubTransactionEntry
    Push "CHANGELOG.md"
    Call RestoreHubTransactionEntry
    Push "hub-package.json"
    Call RestoreHubTransactionEntry
    Push "Launch-KeireHub.cmd"
    Call RestoreHubTransactionEntry
    Push "LICENSE.txt"
    Call RestoreHubTransactionEntry
    Push "README.md"
    Call RestoreHubTransactionEntry
    Push "THIRD_PARTY_NOTICES.md"
    Call RestoreHubTransactionEntry
    Push ".keire-hub-install"
    Call RestoreHubTransactionEntry
    Push "Uninstall.exe"
    Call RestoreHubTransactionEntry
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
!define MUI_CUSTOMFUNCTION_ABORT HubInstallerAbort

Function SetHubTransactionPaths
    StrCpy $KeireHubTransactionRoot "$INSTDIR.__keire-hub-transaction"
    StrCpy $KeireHubTransactionStage "$KeireHubTransactionRoot\stage"
    StrCpy $KeireHubTransactionBackup "$KeireHubTransactionRoot\backup"
    StrCpy $KeireHubTransactionDiscard "$KeireHubTransactionRoot\discard"
    StrCpy $KeireHubTransactionCleanup "$INSTDIR.__keire-hub-transaction-cleanup"
    StrCpy $KeireHubTransactionCleanupOwner "$INSTDIR.__keire-hub-transaction-cleanup-owner.ini"
    StrCpy $KeireHubTransactionJournal "$KeireHubTransactionRoot\journal.ini"
FunctionEnd

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

Function ValidateHubTransactionTree
    Exch $R0
    Push $R1
    Push $R2
    Push $R3
    Push $R4
    StrCpy $R2 ""
    StrCmp $KeireHubTransactionFailure "0" 0 ValidateHubTransactionTreeDone

    System::Call 'kernel32::GetFileAttributesW(w R0)i.R1'
    IntCmp $R1 -1 ValidateHubTransactionTreeFailed
    IntOp $R4 $R1 & ${FILE_ATTRIBUTE_REPARSE_POINT}
    StrCmp $R4 "0" 0 ValidateHubTransactionTreeFailed
    IntOp $R4 $R1 & ${FILE_ATTRIBUTE_DIRECTORY}
    StrCmp $R4 "0" ValidateHubTransactionTreeDone

    ClearErrors
    FindFirst $R2 $R3 "$R0\*"
    IfErrors ValidateHubTransactionTreeFailed
ValidateHubTransactionTreeNext:
    StrCmp $R3 "" ValidateHubTransactionTreeClose
    StrCmp $R3 "." ValidateHubTransactionTreeAdvance
    StrCmp $R3 ".." ValidateHubTransactionTreeAdvance
    Push "$R0\$R3"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 ValidateHubTransactionTreeClose
ValidateHubTransactionTreeAdvance:
    ClearErrors
    FindNext $R2 $R3
    IfErrors ValidateHubTransactionTreeClose
    Goto ValidateHubTransactionTreeNext

ValidateHubTransactionTreeFailed:
    StrCpy $KeireHubTransactionFailure "1"
ValidateHubTransactionTreeClose:
    StrCmp $R2 "" ValidateHubTransactionTreeDone
    FindClose $R2
ValidateHubTransactionTreeDone:
    Pop $R4
    Pop $R3
    Pop $R2
    Pop $R1
    Pop $R0
FunctionEnd

Function RemoveHubTransactionTree
    Exch $R0
    Push $R1
    Push $R2
    StrCpy $R1 "0"
    ${GetParent} "$R0" $R2
    SetOutPath "$R2"

RemoveHubTransactionTreeAttempt:
    IfFileExists "$R0" 0 RemoveHubTransactionTreeDone
    StrCpy $KeireHubTransactionFailure "0"
    Push "$R0"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 RemoveHubTransactionTreeDone
    ClearErrors
    RMDir /r "$R0"
    IfErrors RemoveHubTransactionTreeRetry
    Goto RemoveHubTransactionTreeDone

RemoveHubTransactionTreeRetry:
    IntOp $R1 $R1 + 1
    IntCmp $R1 3 RemoveHubTransactionTreeFailed RemoveHubTransactionTreeWait RemoveHubTransactionTreeFailed
RemoveHubTransactionTreeWait:
    Sleep 200
    Goto RemoveHubTransactionTreeAttempt

RemoveHubTransactionTreeFailed:
    StrCpy $KeireHubTransactionFailure "1"
RemoveHubTransactionTreeDone:
    Pop $R2
    Pop $R1
    Pop $R0
FunctionEnd

Function ValidateHubTransactionCleanupOwner
    StrCpy $KeireHubTransactionFailure "0"
    IfFileExists "$KeireHubTransactionCleanupOwner" 0 HubTransactionCleanupOwnerInvalid
    Push "$KeireHubTransactionCleanupOwner"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCleanupOwnerInvalid
    ClearErrors
    ReadINIStr $R0 "$KeireHubTransactionCleanupOwner" "Cleanup" "Marker"
    IfErrors HubTransactionCleanupOwnerInvalid
    StrCmp $R0 "${HUB_TRANSACTION_MARKER}" 0 HubTransactionCleanupOwnerInvalid
    ReadINIStr $R0 "$KeireHubTransactionCleanupOwner" "Cleanup" "InstallRoot"
    IfErrors HubTransactionCleanupOwnerInvalid
    StrCmp $R0 "$INSTDIR" 0 HubTransactionCleanupOwnerInvalid
    ReadINIStr $R0 "$KeireHubTransactionCleanupOwner" "Cleanup" "ProductMarker"
    IfErrors HubTransactionCleanupOwnerInvalid
    StrCmp $R0 "${INSTALL_MARKER}" 0 HubTransactionCleanupOwnerInvalid
    Return

HubTransactionCleanupOwnerInvalid:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function ValidateHubTransactionOwnership
    StrCpy $KeireHubTransactionFailure "0"
    IfFileExists "$KeireHubTransactionRoot" 0 HubTransactionOwnershipInvalid
    Push "$KeireHubTransactionRoot"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionOwnershipInvalid
    IfFileExists "$KeireHubTransactionRoot\.keire-hub-transaction" 0 HubTransactionOwnershipInvalid
    ClearErrors
    FileOpen $R0 "$KeireHubTransactionRoot\.keire-hub-transaction" r
    IfErrors HubTransactionOwnershipInvalid
    FileRead $R0 $R1
    FileClose $R0
    StrCmp $R1 "${HUB_TRANSACTION_MARKER}$\r$\n" 0 HubTransactionOwnershipInvalid

    ClearErrors
    ReadINIStr $R0 "$KeireHubTransactionJournal" "Transaction" "InstallRoot"
    IfErrors HubTransactionOwnershipInvalid
    StrCmp $R0 "$INSTDIR" 0 HubTransactionOwnershipInvalid
    ReadINIStr $R0 "$KeireHubTransactionJournal" "Transaction" "ProductMarker"
    IfErrors HubTransactionOwnershipInvalid
    StrCmp $R0 "${INSTALL_MARKER}" 0 HubTransactionOwnershipInvalid
    StrCpy $KeireHubTransactionActive "1"
    Return

HubTransactionOwnershipInvalid:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function RemoveOwnedHubTransactionCleanup
    IfFileExists "$KeireHubTransactionCleanupOwner" HubTransactionCleanupValidateOwner 0
    IfFileExists "$KeireHubTransactionCleanup" HubTransactionCleanupFailed HubTransactionCleanupDone

HubTransactionCleanupValidateOwner:
    Call ValidateHubTransactionCleanupOwner
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCleanupFailed
    IfFileExists "$KeireHubTransactionCleanup" HubTransactionCleanupRemove 0
    ; The owner can be moved out immediately before the canonical transaction becomes the cleanup tombstone.
    IfFileExists "$KeireHubTransactionRoot" HubTransactionCleanupDone 0
    ClearErrors
    Delete "$KeireHubTransactionCleanupOwner"
    IfErrors HubTransactionCleanupFailed
    Return

HubTransactionCleanupRemove:
    Push "$KeireHubTransactionCleanup"
    Call RemoveHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCleanupFailed
    ClearErrors
    Delete "$KeireHubTransactionCleanupOwner"
    IfErrors HubTransactionCleanupFailed
HubTransactionCleanupDone:
    Return

HubTransactionCleanupFailed:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function RemoveOwnedHubTransaction
    Call RemoveOwnedHubTransactionCleanup
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionRemovalFailed
    IfFileExists "$KeireHubTransactionCleanupOwner" HubTransactionCleanupOwnerReady 0
    IfFileExists "$KeireHubTransactionRoot\cleanup-owner.ini" 0 HubTransactionRemovalFailed
    Push "$KeireHubTransactionRoot\cleanup-owner.ini"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionRemovalFailed
    ClearErrors
    Rename "$KeireHubTransactionRoot\cleanup-owner.ini" "$KeireHubTransactionCleanupOwner"
    IfErrors HubTransactionRemovalFailed
HubTransactionCleanupOwnerReady:
    Call ValidateHubTransactionCleanupOwner
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionRemovalFailed
    ${GetParent} "$KeireHubTransactionRoot" $R0
    SetOutPath "$R0"
    StrCpy $R1 "0"
HubTransactionCleanupRenameAttempt:
    StrCpy $KeireHubTransactionFailure "0"
    Push "$KeireHubTransactionRoot"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionRemovalFailed
    ClearErrors
    Rename "$KeireHubTransactionRoot" "$KeireHubTransactionCleanup"
    IfErrors HubTransactionCleanupRenameRetry
    Goto HubTransactionCleanupRenamed

HubTransactionCleanupRenameRetry:
    IntOp $R1 $R1 + 1
    IntCmp $R1 5 HubTransactionRemovalFailed HubTransactionCleanupRenameWait HubTransactionRemovalFailed
HubTransactionCleanupRenameWait:
    Sleep 200
    Goto HubTransactionCleanupRenameAttempt

HubTransactionCleanupRenamed:
    StrCpy $KeireHubTransactionActive "0"
!ifdef KEIRE_HUB_TEST_INTERRUPT_DURING_CLEANUP
    ; Test-only hard interruption: the external ownership record must make this tombstone recoverable.
    System::Call 'kernel32::GetCurrentProcess()p.r0'
    System::Call 'kernel32::TerminateProcess(p r0, i 87)'
!endif
    Push "$KeireHubTransactionCleanup"
    Call RemoveHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionRemovalDeferred
    ClearErrors
    Delete "$KeireHubTransactionCleanupOwner"
    IfErrors HubTransactionRemovalDeferred
    Return

HubTransactionRemovalDeferred:
    ; The state-changing rename already completed. Leaving an owned cleanup tombstone is safe and retryable.
    DetailPrint "The Hub installer transaction cleanup will be retried later."
    ClearErrors
    StrCpy $KeireHubTransactionFailure "0"
    Return

HubTransactionRemovalFailed:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function SetHubTransactionPhase
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionPhaseDone
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "Phase" "$R0"
    IfErrors HubTransactionPhaseFailed
    Return

HubTransactionPhaseFailed:
    StrCpy $KeireHubTransactionFailure "1"
HubTransactionPhaseDone:
FunctionEnd

Function RecordHubTransactionEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 RecordHubTransactionEntryDone
    IfFileExists "$INSTDIR\$R0" RecordHubTransactionEntryPresent RecordHubTransactionEntryAbsent

RecordHubTransactionEntryPresent:
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "OriginalPayload" "$R0" "1"
    IfErrors RecordHubTransactionEntryFailed
    Return

RecordHubTransactionEntryAbsent:
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "OriginalPayload" "$R0" "0"
    IfErrors RecordHubTransactionEntryFailed
    Return

RecordHubTransactionEntryFailed:
    StrCpy $KeireHubTransactionFailure "1"
RecordHubTransactionEntryDone:
FunctionEnd

Function ValidateHubTransactionStageEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 ValidateHubTransactionStageEntryDone
    IfFileExists "$KeireHubTransactionStage\$R0" ValidateHubTransactionStageEntryDone 0
    StrCpy $KeireHubTransactionFailure "1"
ValidateHubTransactionStageEntryDone:
FunctionEnd

Function BackupHubTransactionEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 BackupHubTransactionEntryDone
    ClearErrors
    ReadINIStr $R1 "$KeireHubTransactionJournal" "OriginalPayload" "$R0"
    IfErrors BackupHubTransactionEntryFailed
    StrCmp $R1 "1" BackupHubTransactionEntryPresent
    StrCmp $R1 "0" BackupHubTransactionEntryAbsent BackupHubTransactionEntryFailed

BackupHubTransactionEntryPresent:
    IfFileExists "$INSTDIR\$R0" 0 BackupHubTransactionEntryFailed
    IfFileExists "$KeireHubTransactionBackup\$R0" BackupHubTransactionEntryFailed 0
    Push "$INSTDIR\$R0"
    Call ValidateHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 BackupHubTransactionEntryFailed
    ClearErrors
    Rename "$INSTDIR\$R0" "$KeireHubTransactionBackup\$R0"
    IfErrors BackupHubTransactionEntryFailed
    Return

BackupHubTransactionEntryAbsent:
    IfFileExists "$INSTDIR\$R0" BackupHubTransactionEntryFailed 0
    IfFileExists "$KeireHubTransactionBackup\$R0" BackupHubTransactionEntryFailed BackupHubTransactionEntryDone

BackupHubTransactionEntryFailed:
    StrCpy $KeireHubTransactionFailure "1"
BackupHubTransactionEntryDone:
FunctionEnd

Function PromoteHubTransactionEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 PromoteHubTransactionEntryDone
    IfFileExists "$KeireHubTransactionStage\$R0" 0 PromoteHubTransactionEntryFailed
    IfFileExists "$INSTDIR\$R0" PromoteHubTransactionEntryFailed 0
    ClearErrors
    Rename "$KeireHubTransactionStage\$R0" "$INSTDIR\$R0"
    IfErrors PromoteHubTransactionEntryFailed
    Return

PromoteHubTransactionEntryFailed:
    StrCpy $KeireHubTransactionFailure "1"
PromoteHubTransactionEntryDone:
FunctionEnd

Function RestoreHubTransactionEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 RestoreHubTransactionEntryDone
    ClearErrors
    ReadINIStr $R1 "$KeireHubTransactionJournal" "OriginalPayload" "$R0"
    IfErrors RestoreHubTransactionEntryFailed
    StrCmp $R1 "1" RestoreHubTransactionOriginalPresent
    StrCmp $R1 "0" RestoreHubTransactionOriginalAbsent RestoreHubTransactionEntryFailed

RestoreHubTransactionOriginalPresent:
    IfFileExists "$KeireHubTransactionBackup\$R0" RestoreHubTransactionMoveOriginal 0
    IfFileExists "$INSTDIR\$R0" RestoreHubTransactionEntryDone RestoreHubTransactionEntryFailed

RestoreHubTransactionMoveOriginal:
    IfFileExists "$INSTDIR\$R0" 0 RestoreHubTransactionRenameOriginal
    IfFileExists "$KeireHubTransactionDiscard\$R0" RestoreHubTransactionEntryFailed 0
    ClearErrors
    Rename "$INSTDIR\$R0" "$KeireHubTransactionDiscard\$R0"
    IfErrors RestoreHubTransactionEntryFailed
RestoreHubTransactionRenameOriginal:
    ClearErrors
    Rename "$KeireHubTransactionBackup\$R0" "$INSTDIR\$R0"
    IfErrors RestoreHubTransactionEntryFailed
    Return

RestoreHubTransactionOriginalAbsent:
    IfFileExists "$KeireHubTransactionBackup\$R0" RestoreHubTransactionEntryFailed 0
    IfFileExists "$INSTDIR\$R0" 0 RestoreHubTransactionEntryDone
    IfFileExists "$KeireHubTransactionStage\$R0" RestoreHubTransactionEntryFailed 0
    IfFileExists "$KeireHubTransactionDiscard\$R0" RestoreHubTransactionEntryFailed 0
    ClearErrors
    Rename "$INSTDIR\$R0" "$KeireHubTransactionDiscard\$R0"
    IfErrors RestoreHubTransactionEntryFailed
    Return

RestoreHubTransactionEntryFailed:
    StrCpy $KeireHubTransactionFailure "1"
RestoreHubTransactionEntryDone:
FunctionEnd

Function RestoreHubRegistration
    ClearErrors
    ReadINIStr $R0 "$KeireHubTransactionJournal" "Registration" "PreviousInstallRoot"
    ReadINIStr $R1 "$KeireHubTransactionJournal" "Registration" "PreviousDisplayVersion"
    ReadINIStr $R2 "$KeireHubTransactionJournal" "Registration" "PreviousProtocolName"
    ReadINIStr $R3 "$KeireHubTransactionJournal" "Registration" "PreviousProtocolIcon"
    ReadINIStr $R4 "$KeireHubTransactionJournal" "Registration" "PreviousProtocolCommand"
    IfErrors RestoreHubRegistrationFailed
    StrCmp $R0 "" RestoreHubRegistrationAbsent

    ClearErrors
    WriteRegStr HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory" "$R0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "$R1"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$R0\bin\${HUB_TARGET}.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_DISPLAY_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$R0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$R0\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$R0\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    IfErrors RestoreHubRegistrationFailed
    Goto RestoreHubProtocolRegistration

RestoreHubRegistrationAbsent:
    ClearErrors
    ReadRegStr $R5 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    ClearErrors
    StrCmp $R5 "$INSTDIR" 0 RestoreHubProtocolRegistration
    ClearErrors
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller"
    IfErrors RestoreHubRegistrationFailed

RestoreHubProtocolRegistration:
    StrCmp $R4 "" RestoreHubProtocolRegistrationAbsent
    ClearErrors
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}" "" "$R2"
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}" "URL Protocol" ""
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}\DefaultIcon" "" "$R3"
    WriteRegStr HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" "" "$R4"
    IfErrors RestoreHubRegistrationFailed
    Return

RestoreHubProtocolRegistrationAbsent:
    ClearErrors
    ReadRegStr $R5 HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" ""
    ClearErrors
    StrCmp $R5 '"$INSTDIR\bin\${HUB_TARGET}.exe" "%1"' 0 RestoreHubRegistrationDone
    ClearErrors
    DeleteRegKey HKCU "${HUB_PROTOCOL_KEY}"
    IfErrors RestoreHubRegistrationFailed
RestoreHubRegistrationDone:
    Return

RestoreHubRegistrationFailed:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function BeginHubTransaction
    StrCpy $KeireHubTransactionFailure "0"
    Call SetHubTransactionPaths
    Call RemoveOwnedHubTransactionCleanup
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionBeginFailed
    IfFileExists "$KeireHubTransactionRoot" HubTransactionBeginFailed 0
    ${GetParent} "$KeireHubTransactionRoot" $R0
    ClearErrors
    CreateDirectory "$R0"
    IfErrors HubTransactionBeginFailed

    StrCpy $0 "$KeireHubTransactionRoot"
    System::Call 'kernel32::CreateDirectoryW(w r0, p 0)i.r1'
    StrCmp $1 "0" HubTransactionBeginFailed
    StrCpy $KeireHubTransactionActive "1"
    ClearErrors
    CreateDirectory "$KeireHubTransactionStage"
    CreateDirectory "$KeireHubTransactionBackup"
    IfErrors HubTransactionBeginFailedCleanup

    ClearErrors
    FileOpen $R0 "$KeireHubTransactionRoot\.keire-hub-transaction" w
    IfErrors HubTransactionBeginFailedCleanup
    FileWrite $R0 "${HUB_TRANSACTION_MARKER}$\r$\n"
    FileClose $R0
    IfErrors HubTransactionBeginFailedCleanup

    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "InstallRoot" "$INSTDIR"
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "ProductMarker" "${INSTALL_MARKER}"
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "FromVersion" "$KeireHubUpdateFromVersion"
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "ToVersion" "${PRODUCT_VERSION}"
    WriteINIStr "$KeireHubTransactionJournal" "Transaction" "Phase" "initializing"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    WriteINIStr "$KeireHubTransactionRoot\cleanup-owner.ini" "Cleanup" "Marker" "${HUB_TRANSACTION_MARKER}"
    WriteINIStr "$KeireHubTransactionRoot\cleanup-owner.ini" "Cleanup" "InstallRoot" "$INSTDIR"
    WriteINIStr "$KeireHubTransactionRoot\cleanup-owner.ini" "Cleanup" "ProductMarker" "${INSTALL_MARKER}"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    ReadRegStr $R0 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Registration" "PreviousInstallRoot" "$R0"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    ReadRegStr $R0 HKCU "${UNINSTALL_KEY}" "DisplayVersion"
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Registration" "PreviousDisplayVersion" "$R0"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    ReadRegStr $R0 HKCU "${HUB_PROTOCOL_KEY}" ""
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Registration" "PreviousProtocolName" "$R0"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    ReadRegStr $R0 HKCU "${HUB_PROTOCOL_KEY}\DefaultIcon" ""
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Registration" "PreviousProtocolIcon" "$R0"
    IfErrors HubTransactionBeginFailedCleanup
    ClearErrors
    ReadRegStr $R0 HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" ""
    ClearErrors
    WriteINIStr "$KeireHubTransactionJournal" "Registration" "PreviousProtocolCommand" "$R0"
    IfErrors HubTransactionBeginFailedCleanup

    !insertmacro RecordHubTransactionPayload
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionBeginFailedCleanup
    Push "preparing"
    Call SetHubTransactionPhase
    Return

HubTransactionBeginFailedCleanup:
    StrCpy $KeireHubTransactionFailure "0"
    IfFileExists "$KeireHubTransactionRoot" 0 HubTransactionBeginCleanupDone
    Push "$KeireHubTransactionRoot"
    Call RemoveHubTransactionTree
HubTransactionBeginCleanupDone:
    StrCpy $KeireHubTransactionActive "0"
HubTransactionBeginFailed:
    StrCpy $KeireHubTransactionFailure "1"
FunctionEnd

Function ValidatePublishedHubPayloadEntry
    Pop $R0
    StrCmp $KeireHubTransactionFailure "0" 0 ValidatePublishedHubPayloadEntryDone
    IfFileExists "$INSTDIR\$R0" ValidatePublishedHubPayloadEntryDone 0
    StrCpy $KeireHubTransactionFailure "1"
ValidatePublishedHubPayloadEntryDone:
FunctionEnd

Function ValidatePublishedHubPayload
    StrCpy $KeireHubTransactionFailure "0"
    !insertmacro ValidateRequiredHubPayload ValidatePublishedHubPayloadEntry
FunctionEnd

Function RecoverHubTransaction
    Call ValidateHubTransactionOwnership
    StrCmp $KeireHubTransactionFailure "0" 0 RecoverHubTransactionDone
    ClearErrors
    ReadINIStr $R0 "$KeireHubTransactionJournal" "Transaction" "Phase"
    IfErrors RecoverHubTransactionFailed
    StrCmp $R0 "initializing" RecoverHubTransactionDiscard
    StrCmp $R0 "preparing" RecoverHubTransactionDiscard
    StrCmp $R0 "prepared" RecoverHubTransactionRollback
    StrCmp $R0 "backupMoved" RecoverHubTransactionRollback
    StrCmp $R0 "payloadPublished" RecoverHubTransactionRollback
    StrCmp $R0 "published" RecoverHubTransactionFinish RecoverHubTransactionFailed

RecoverHubTransactionDiscard:
    Call RemoveOwnedHubTransaction
    Return

RecoverHubTransactionFinish:
    Call ValidatePublishedHubPayload
    StrCmp $KeireHubTransactionFailure "0" 0 RecoverHubTransactionRollback
    Call RemoveOwnedHubTransaction
    Return

RecoverHubTransactionRollback:
    StrCpy $KeireHubTransactionFailure "0"
    IfFileExists "$KeireHubTransactionDiscard" 0 RecoverHubTransactionCreateDiscard
    Push "$KeireHubTransactionDiscard"
    Call RemoveHubTransactionTree
    StrCmp $KeireHubTransactionFailure "0" 0 RecoverHubTransactionFailed
RecoverHubTransactionCreateDiscard:
    ClearErrors
    CreateDirectory "$KeireHubTransactionDiscard"
    IfErrors RecoverHubTransactionFailed
    !insertmacro RestoreHubTransactionPayload
    StrCmp $KeireHubTransactionFailure "0" 0 RecoverHubTransactionDone
    Call RestoreHubRegistration
    StrCmp $KeireHubTransactionFailure "0" 0 RecoverHubTransactionDone
    Call RemoveOwnedHubTransaction
    Return

RecoverHubTransactionFailed:
    StrCpy $KeireHubTransactionFailure "1"
RecoverHubTransactionDone:
FunctionEnd

Function HubInstallerAbort
    StrCmp $KeireHubTransactionActive "1" 0 HubInstallerAbortDone
    Call RecoverHubTransaction
    StrCmp $KeireHubTransactionFailure "0" HubInstallerAbortDone
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub installer could not restore the previous payload automatically. The transaction backup was preserved at:$\r$\n$KeireHubTransactionRoot" \
        /SD IDOK
HubInstallerAbortDone:
FunctionEnd

Function .onInit
    Call AcquireHubInstallerMutex
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
    StrCmp $KeireHubUpdateToVersion "${PRODUCT_VERSION}" 0 HubUpdateInitInvalid
    IfFileExists "$KeireHubUpdateResumeToken" 0 HubUpdateInitInvalid
    StrCpy $INSTDIR $KeireHubUpdateRoot
    StrCpy $KeireHubUpdateMode "1"

    ; Wait for the verified running Hub process before replacing any payload file. A missing PID is already closed;
    ; every other OpenProcess failure is unsafe because the installer cannot prove that the Hub stopped.
    ; System's ?e captures the OpenProcess last-error value before any plug-in cleanup can overwrite it.
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
    StrCpy $KeireHubTransactionActive "0"
    StrCpy $KeireHubTransactionFailure "0"
    Call SetHubTransactionPaths
    StrCmp $KeireHubUpdateMode "1" 0 CheckForPendingHubTransaction
    ReadRegStr $0 HKCU "Software\${PRODUCT_IDENTIFIER}\HubInstaller" "InstallDirectory"
    StrCmp $0 "$INSTDIR" 0 UnsafeUpdate
CheckForPendingHubTransaction:
    IfFileExists "$KeireHubTransactionRoot" 0 HubTransactionRecovered
    Call RecoverHubTransaction
    StrCmp $KeireHubTransactionFailure "0" HubTransactionRecovered HubTransactionRecoveryFailed

HubTransactionRecovered:
    StrCmp $KeireHubUpdateMode "1" 0 PrepareHubTransaction
    IfFileExists "$INSTDIR\.keire-hub-install" 0 UnsafeUpdate
PrepareHubTransaction:
    Call BeginHubTransaction
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionPreparationFailed

    ; Extract and finish the complete replacement in an operation-owned sibling before moving any active payload.
    SetOverwrite try
    SetOutPath "$KeireHubTransactionStage"
    ClearErrors
    File /r "${SOURCE_DIRECTORY}\*"
    IfErrors HubTransactionStageFailed
!ifdef KEIRE_HUB_TEST_FAIL_DURING_STAGE
    Goto HubTransactionStageFailed
!endif

    ClearErrors
    FileOpen $0 "$KeireHubTransactionStage\.keire-hub-install" w
    IfErrors HubTransactionStageFailed
    FileWrite $0 "${INSTALL_MARKER}$\r$\n"
    FileClose $0
    IfErrors HubTransactionStageFailed

    ClearErrors
    WriteUninstaller "$KeireHubTransactionStage\Uninstall.exe"
    IfErrors HubTransactionStageFailed
    StrCpy $KeireHubTransactionFailure "0"
    !insertmacro ValidateHubTransactionStage
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionStageFailed
    Push "prepared"
    Call SetHubTransactionPhase
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionStageFailed

    ; Only allowlisted installer-owned paths move. Unknown top-level entries outside that allowlist remain in place.
    !insertmacro BackupHubTransactionPayload
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed
    Push "backupMoved"
    Call SetHubTransactionPhase
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed
!ifdef KEIRE_HUB_TEST_INTERRUPT_AFTER_BACKUP
    ; Test-only hard interruption: bypass callbacks so the next installer must consume the persisted journal.
    System::Call 'kernel32::GetCurrentProcess()p.r0'
    System::Call 'kernel32::TerminateProcess(p r0, i 86)'
!endif

    ClearErrors
    CreateDirectory "$INSTDIR"
    IfErrors HubTransactionCommitFailed
    Push "bin"
    Call PromoteHubTransactionEntry
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed
!ifdef KEIRE_HUB_TEST_FAIL_AFTER_FIRST_PROMOTION
    Goto HubTransactionCommitFailed
!endif
    !insertmacro PromoteRemainingHubTransactionPayload
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed
!ifdef KEIRE_HUB_TEST_INVALIDATE_PUBLISHED_PAYLOAD
    Delete "$INSTDIR\bin\${HUB_TARGET}.exe"
!endif
!ifdef KEIRE_HUB_TEST_INVALIDATE_PUBLISHED_DOCUMENTATION
    Delete "$INSTDIR\README.md"
!endif
    Call ValidatePublishedHubPayload
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed
    Push "payloadPublished"
    Call SetHubTransactionPhase
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed

    ; Registration is committed only after every replacement path is present. A registration failure rolls payload back.
    ClearErrors
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
    IfErrors HubTransactionCommitFailed
    Push "published"
    Call SetHubTransactionPhase
    StrCmp $KeireHubTransactionFailure "0" 0 HubTransactionCommitFailed

    SetOverwrite on
    SetOutPath "$INSTDIR"
    Call RemoveOwnedHubTransaction
    StrCmp $KeireHubTransactionFailure "0" HubTransactionComplete
    ; The committed Hub is usable. Its ownership journal deliberately remains so the next installer can retry cleanup.
    DetailPrint "The Hub was installed, but its previous-payload backup could not yet be removed."
    StrCpy $KeireHubTransactionFailure "0"
    Goto HubTransactionComplete

HubTransactionStageFailed:
    Call RecoverHubTransaction
    StrCmp $KeireHubTransactionFailure "0" HubTransactionStageRestored HubTransactionRollbackFailed
HubTransactionStageRestored:
    MessageBox MB_ICONSTOP|MB_OK \
        "The replacement Hub could not be staged. The existing installation was left unchanged." /SD IDOK
    Abort

HubTransactionCommitFailed:
    Call RecoverHubTransaction
    StrCmp $KeireHubTransactionFailure "0" HubTransactionCommitRestored HubTransactionRollbackFailed
HubTransactionCommitRestored:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub update could not be committed. The previous installation was restored." /SD IDOK
    Abort

HubTransactionPreparationFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub installer could not create an owned staging transaction. No application files were changed." /SD IDOK
    Abort

HubTransactionRecoveryFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "An interrupted Hub installation could not be recovered safely. Its staging and backup files were preserved at:$\r$\n$KeireHubTransactionRoot" \
        /SD IDOK
    Abort

HubTransactionRollbackFailed:
    MessageBox MB_ICONSTOP|MB_OK \
        "The Hub installer could not restore the previous payload automatically. Its staging and backup files were preserved at:$\r$\n$KeireHubTransactionRoot" \
        /SD IDOK
    Abort

UnsafeUpdate:
    MessageBox MB_ICONSTOP|MB_OK \
        "The existing Hub installation could not be verified. No application files were changed." /SD IDOK
    Abort

HubTransactionComplete:
    SetOutPath "$INSTDIR"
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
