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
        'LegalCopyright',
        '.keire-hub-install', 'Software\${PRODUCT_IDENTIFIER}\HubInstaller', 'UnsafeUninstall',
        'MUI_FINISHPAGE_RUN', 'intentionally preserved', '/KEIRE_HUB_UPDATE=', '/INSTALL_ROOT=',
        '/RESUME_TOKEN=', '/WAIT_PROCESS=', 'WaitForSingleObject', 'SkipUpdateDirectoryPage',
        'UnsafeUpdate', 'No application files were changed', 'Software\Classes\keirehub', 'URL Protocol',
        '${HUB_PROTOCOL_KEY}\shell\open\command', '"$INSTDIR\bin\${HUB_TARGET}.exe" "%1"',
        'KeepHubProtocolRegistration')) {
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
$protocolOwnershipCheck = $template.LastIndexOf(
    'ReadRegStr $2 HKCU "${HUB_PROTOCOL_KEY}\shell\open\command" ""', [StringComparison]::Ordinal)
$protocolRemoval = $template.LastIndexOf('DeleteRegKey HKCU "${HUB_PROTOCOL_KEY}"', [StringComparison]::Ordinal)
if ($protocolOwnershipCheck -lt 0 -or $protocolRemoval -lt 0 -or $protocolOwnershipCheck -gt $protocolRemoval) {
    throw "The Hub protocol handler must verify installation ownership before unregistration."
}

foreach ($transactionContract in @('HUB_TRANSACTION_MARKER', '.__keire-hub-transaction',
        'RecordHubTransactionPayload', 'ValidateHubTransactionStage', 'BackupHubTransactionPayload',
        'RestoreHubTransactionPayload', 'RecoverHubTransaction', 'CreateDirectoryW',
        'OpenProcess(i 0x00100000', 'p.r2 ?e', 'Pop $3', 'StrCmp $3 "87" HubUpdateInitDone',
        'HUB_INSTALLER_MUTEX_NAME', 'CreateMutexW', 'ReleaseHubInstallerMutex', 'Function .onGUIEnd',
        'GetFileAttributesW', 'FILE_ATTRIBUTE_REPARSE_POINT', 'ValidateHubTransactionTree',
        '.__keire-hub-transaction-cleanup-owner.ini', 'ValidateHubTransactionCleanupOwner',
        'ValidateRequiredHubPayload', 'Push "bin\${HUB_TARGET}.exe"', 'Push "README.md"',
        'Call ValidatePublishedHubPayload')) {
    if (-not $template.Contains($transactionContract)) {
        throw "The standalone Hub NSIS transaction is missing '$transactionContract'."
    }
}
if ($template.Contains('StrCmp $2 "0" HubUpdateInitDone')) {
    throw "An arbitrary OpenProcess failure must not be treated as proof that the running Hub exited."
}
$mainSectionStart = $template.IndexOf('Section "${PRODUCT_NAME} (required)" MainSection',
    [StringComparison]::Ordinal)
$mainSectionEnd = $template.IndexOf('SectionEnd', $mainSectionStart, [StringComparison]::Ordinal)
if ($mainSectionStart -lt 0 -or $mainSectionEnd -lt 0) {
    throw "The standalone Hub NSIS template is missing its required install section."
}
$mainSection = $template.Substring($mainSectionStart, $mainSectionEnd - $mainSectionStart)
if ($mainSection.Contains('!insertmacro RemoveHubPayload')) {
    throw "The Hub installer must not delete the active payload before the replacement is staged."
}
$stageExtraction = $mainSection.IndexOf('File /r "${SOURCE_DIRECTORY}\*"', [StringComparison]::Ordinal)
$stageValidation = $mainSection.IndexOf('!insertmacro ValidateHubTransactionStage', [StringComparison]::Ordinal)
$payloadBackup = $mainSection.IndexOf('!insertmacro BackupHubTransactionPayload', [StringComparison]::Ordinal)
if ($stageExtraction -lt 0 -or $stageValidation -lt $stageExtraction -or $payloadBackup -lt $stageValidation) {
    throw "The Hub installer must extract and validate the complete stage before moving the active payload."
}

function Get-MakensisPath {
    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"),
        (Join-Path $env:ProgramFiles "NSIS\makensis.exe")
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    if ($candidates) {
        return @($candidates)[0]
    }
    return ""
}

function New-HubPayloadFixture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Version
    )

    foreach ($directory in @("bin", "Config", "content", "Docs", "Samples", "third-party")) {
        $fixtureDirectory = Join-Path $Path $directory
        New-Item -ItemType Directory -Path $fixtureDirectory -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $fixtureDirectory "fixture-$directory.txt") `
            -Value "payload-$Version-$directory" -Encoding UTF8
    }
    Set-Content -LiteralPath (Join-Path $Path "bin\HubFixture.exe") -Value "payload-$Version" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $Path "CHANGELOG.md") -Value "changelog-$Version" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $Path "hub-package.json") `
        -Value ('{"version":"' + $Version + '"}') -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $Path "Launch-KeireHub.cmd") -Value "@echo off" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Path "LICENSE.txt") -Value "license-$Version" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $Path "README.md") -Value "payload-$Version" -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $Path "THIRD_PARTY_NOTICES.md") `
        -Value "notices-$Version" -Encoding UTF8
}

function New-HubFixtureInstaller {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Version,
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,
        [Parameter(Mandatory = $true)]
        [string]$OutputPath,
        [string]$TestFailureDefine = "",
        [string]$TestFailureValue = "1"
    )

    $fileVersion = "$Version.0"
    $arguments = @(
        "/V2",
        "/WX",
        "/DPRODUCT_IDENTIFIER=$productIdentifier",
        "/DPRODUCT_DISPLAY_NAME=$productDisplayName",
        "/DPRODUCT_VERSION=$Version",
        "/DPRODUCT_FILE_VERSION=$fileVersion",
        "/DPRODUCT_ARCHITECTURE=x86_64",
        "/DHUB_TARGET=HubFixture",
        "/DSOURCE_DIRECTORY=$SourceDirectory",
        "/DOUTPUT_PATH=$OutputPath",
        "/DLICENSE_PATH=$licensePath",
        "/DSETUP_ICON_PATH=$iconPath",
        "/DHUB_PROTOCOL_KEY=$protocolKey"
    )
    if ($TestFailureDefine) {
        $arguments += "/D$TestFailureDefine=$TestFailureValue"
    }
    $arguments += $templatePath

    $compilerOutput = @(& $makensisPath @arguments 2>&1)
    $compilerExitCode = $LASTEXITCODE
    if ($compilerExitCode -ne 0) {
        throw "NSIS fixture compilation failed with exit code $compilerExitCode.`n$($compilerOutput -join [Environment]::NewLine)"
    }
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "NSIS did not produce the fixture installer: $OutputPath"
    }
}

function Invoke-HubFixtureInstaller {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Installer,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $process = Start-Process -FilePath $Installer -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    return $process.ExitCode
}

function Assert-HubPayloadVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExpectedVersion
    )

    $actualVersion = (Get-Content -LiteralPath (Join-Path $installRoot "bin\HubFixture.exe") -Raw).Trim()
    if ($actualVersion -ne "payload-$ExpectedVersion") {
        throw "Expected Hub payload $ExpectedVersion, but found '$actualVersion'."
    }
    $registeredVersion = (Get-ItemProperty -LiteralPath $uninstallRegistry -Name "DisplayVersion").DisplayVersion
    if ($registeredVersion -ne $ExpectedVersion) {
        throw "Expected registered Hub version $ExpectedVersion, but found '$registeredVersion'."
    }
    $registeredRoot = (Get-ItemProperty -LiteralPath $installerRegistry -Name "InstallDirectory").InstallDirectory
    if ($registeredRoot -ne $installRoot) {
        throw "The Hub fixture registration points at an unexpected installation root."
    }
}

function Assert-NoHubTransaction {
    foreach ($path in @($transactionRoot, $transactionCleanup, $transactionCleanupOwner)) {
        if (Test-Path -LiteralPath $path) {
            $entries = @(Get-ChildItem -LiteralPath $path -Force -ErrorAction SilentlyContinue |
                    Select-Object -ExpandProperty Name) -join ", "
            throw "The completed Hub transaction left temporary state behind: $path (entries: $entries)"
        }
    }
}

$makensisPath = Get-MakensisPath
if (-not $makensisPath) {
    Write-Warning "NSIS 3 is unavailable; skipped transactional Hub installer runtime regression checks."
}
else {
    $fixtureId = [Guid]::NewGuid().ToString("N")
    $productIdentifier = "KeireHubTransactionTest$fixtureId"
    $productDisplayName = "KeireHubTransactionTest$fixtureId"
    $protocolKey = "Software\Classes\keirehub-transaction-test-$fixtureId"
    $templatePath = Join-Path $Root "Installer\Windows\KeireHub.nsi"
    $licensePath = Join-Path $Root "LICENSE.txt"
    $iconPath = Join-Path $Root "Config\Branding\Keire.ico"
    $temporaryDirectory = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $fixtureRoot = Join-Path $temporaryDirectory "keire-hub-installer-test-$fixtureId"
    $fixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $temporaryPrefix = $temporaryDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $fixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "The Hub installer fixture root is outside the system temporary directory."
    }

    $installRoot = Join-Path $fixtureRoot "install"
    $transactionRoot = "${installRoot}.__keire-hub-transaction"
    $transactionCleanup = "${installRoot}.__keire-hub-transaction-cleanup"
    $transactionCleanupOwner = "${installRoot}.__keire-hub-transaction-cleanup-owner.ini"
    $installerMutexName = "Local\$productIdentifier.KeireHubInstaller.Transaction"
    $resumeToken = Join-Path $fixtureRoot "resume.token"
    $productRegistry = "Registry::HKEY_CURRENT_USER\Software\$productIdentifier"
    $installerRegistry = "$productRegistry\HubInstaller"
    $uninstallRegistry = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\${productIdentifier}Hub"
    $protocolRegistry = "Registry::HKEY_CURRENT_USER\$protocolKey"
    $programsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
    $startMenuPath = Join-Path $programsRoot "$productDisplayName Hub"
    $desktopRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    $desktopShortcut = Join-Path $desktopRoot "$productDisplayName Hub.lnk"
    $holdingInstallerProcess = $null
    $reparseLink = ""

    try {
        New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
        Set-Content -LiteralPath $resumeToken -Value "resume" -Encoding ASCII

        $payloads = @{}
        foreach ($version in @("1.0.0", "2.0.0", "3.0.0")) {
            $sourceDirectory = Join-Path $fixtureRoot "payload-$version"
            New-Item -ItemType Directory -Path $sourceDirectory -Force | Out-Null
            New-HubPayloadFixture -Path $sourceDirectory -Version $version
            $payloads[$version] = $sourceDirectory
        }

        $installers = @{
            V1 = Join-Path $fixtureRoot "hub-1.exe"
            V2 = Join-Path $fixtureRoot "hub-2.exe"
            V3 = Join-Path $fixtureRoot "hub-3.exe"
            StageFailure = Join-Path $fixtureRoot "hub-3-stage-failure.exe"
            CommitFailure = Join-Path $fixtureRoot "hub-3-commit-failure.exe"
            Interrupted = Join-Path $fixtureRoot "hub-3-interrupted.exe"
            MutexHold = Join-Path $fixtureRoot "hub-2-mutex-hold.exe"
            CleanupInterrupted = Join-Path $fixtureRoot "hub-2-cleanup-interrupted.exe"
            PublishedValidationFailure = Join-Path $fixtureRoot "hub-3-published-validation-failure.exe"
            PublishedDocumentationFailure = Join-Path $fixtureRoot "hub-3-published-documentation-failure.exe"
        }
        New-HubFixtureInstaller -Version "1.0.0" -SourceDirectory $payloads["1.0.0"] `
            -OutputPath $installers.V1
        New-HubFixtureInstaller -Version "2.0.0" -SourceDirectory $payloads["2.0.0"] `
            -OutputPath $installers.V2
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.V3
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.StageFailure -TestFailureDefine "KEIRE_HUB_TEST_FAIL_DURING_STAGE"
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.CommitFailure -TestFailureDefine "KEIRE_HUB_TEST_FAIL_AFTER_FIRST_PROMOTION"
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.Interrupted -TestFailureDefine "KEIRE_HUB_TEST_INTERRUPT_AFTER_BACKUP"
        New-HubFixtureInstaller -Version "2.0.0" -SourceDirectory $payloads["2.0.0"] `
            -OutputPath $installers.MutexHold -TestFailureDefine "KEIRE_HUB_TEST_HOLD_MUTEX_MS" `
            -TestFailureValue "3000"
        New-HubFixtureInstaller -Version "2.0.0" -SourceDirectory $payloads["2.0.0"] `
            -OutputPath $installers.CleanupInterrupted `
            -TestFailureDefine "KEIRE_HUB_TEST_INTERRUPT_DURING_CLEANUP"
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.PublishedValidationFailure `
            -TestFailureDefine "KEIRE_HUB_TEST_INVALIDATE_PUBLISHED_PAYLOAD"
        New-HubFixtureInstaller -Version "3.0.0" -SourceDirectory $payloads["3.0.0"] `
            -OutputPath $installers.PublishedDocumentationFailure `
            -TestFailureDefine "KEIRE_HUB_TEST_INVALIDATE_PUBLISHED_DOCUMENTATION"

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.CommitFailure `
            -Arguments @("/S", "/D=$installRoot")
        if ($exitCode -eq 0) {
            throw "The injected fresh-install commit failure unexpectedly succeeded."
        }
        if ((Test-Path -LiteralPath (Join-Path $installRoot "bin")) -or
            (Test-Path -LiteralPath $installerRegistry) -or
            (Test-Path -LiteralPath $uninstallRegistry) -or
            (Test-Path -LiteralPath $protocolRegistry)) {
            throw "Fresh-install rollback left payload or registration state behind."
        }
        Assert-NoHubTransaction

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V1 `
            -Arguments @("/S", "/D=$installRoot")
        if ($exitCode -ne 0) {
            throw "The fresh Hub fixture installation failed with exit code $exitCode."
        }
        Assert-HubPayloadVersion -ExpectedVersion "1.0.0"
        Assert-NoHubTransaction

        $unknownFile = Join-Path $installRoot "user-owned-note.txt"
        Set-Content -LiteralPath $unknownFile -Value "preserve-me" -Encoding UTF8
        $holdingInstallerProcess = Start-Process -FilePath $installers.MutexHold `
            -ArgumentList @("/S", "/D=$installRoot") -PassThru -WindowStyle Hidden
        $mutexObserved = $false
        $mutexDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not $mutexObserved -and [DateTime]::UtcNow -lt $mutexDeadline) {
            if ($holdingInstallerProcess.HasExited) {
                throw "The mutex-holding Hub installer exited before exposing its update lock."
            }
            try {
                $mutexProbe = [Threading.Mutex]::OpenExisting($installerMutexName)
                $mutexProbe.Dispose()
                $mutexObserved = $true
            }
            catch [Threading.WaitHandleCannotBeOpenedException] {
                Start-Sleep -Milliseconds 50
            }
        }
        if (-not $mutexObserved) {
            throw "The mutex-holding Hub installer did not expose its update lock in time."
        }
        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V3 `
            -Arguments @("/S", "/D=$installRoot")
        if ($exitCode -eq 0) {
            throw "A concurrent Hub installer unexpectedly acquired the same product lock."
        }
        Assert-HubPayloadVersion -ExpectedVersion "1.0.0"
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "The rejected concurrent installer modified an unknown colocated file."
        }
        Assert-NoHubTransaction
        $holdingInstallerProcess.WaitForExit()
        if ($holdingInstallerProcess.ExitCode -ne 0) {
            throw "The mutex-holding normal Hub overwrite failed with exit code $($holdingInstallerProcess.ExitCode)."
        }
        $holdingInstallerProcess.Dispose()
        $holdingInstallerProcess = $null
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "The normal Hub overwrite modified an unknown colocated file."
        }
        Assert-NoHubTransaction

        $sameVersionUpdateArguments = @("/S", "/KEIRE_HUB_UPDATE=1", "/INSTALL_ROOT=$installRoot",
            "/RESUME_TOKEN=$resumeToken", "/WAIT_PROCESS=0", "/FROM_VERSION=2.0.0",
            "/TO_VERSION=2.0.0")
        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.CleanupInterrupted `
            -Arguments $sameVersionUpdateArguments
        if ($exitCode -eq 0) {
            throw "The injected cleanup interruption unexpectedly reported success."
        }
        if ((Test-Path -LiteralPath $transactionRoot) -or
            -not (Test-Path -LiteralPath $transactionCleanup -PathType Container) -or
            -not (Test-Path -LiteralPath $transactionCleanupOwner -PathType Leaf)) {
            throw "The cleanup interruption did not leave its independently owned cleanup tombstone."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"

        $reparseTarget = Join-Path $fixtureRoot "reparse-target"
        New-Item -ItemType Directory -Path $reparseTarget -Force | Out-Null
        $reparseSentinel = Join-Path $reparseTarget "preserve.txt"
        Set-Content -LiteralPath $reparseSentinel -Value "outside-transaction" -Encoding UTF8
        $reparseLink = Join-Path $transactionCleanup "stage\escape"
        New-Item -ItemType Junction -Path $reparseLink -Target $reparseTarget | Out-Null
        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V2 `
            -Arguments $sameVersionUpdateArguments
        if ($exitCode -eq 0) {
            throw "The Hub updater recursively deleted a transaction containing a reparse point."
        }
        if ((Get-Content -LiteralPath $reparseSentinel -Raw).Trim() -ne "outside-transaction") {
            throw "Transaction cleanup traversed a reparse point outside its owned tree."
        }
        if (-not (Test-Path -LiteralPath $transactionCleanupOwner -PathType Leaf)) {
            throw "Rejected reparse cleanup discarded its external ownership record."
        }
        Remove-Item -LiteralPath $reparseLink -Force
        $reparseLink = ""

        Remove-Item -LiteralPath (Join-Path $transactionCleanup ".keire-hub-transaction"), `
            (Join-Path $transactionCleanup "journal.ini") -Force
        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V2 `
            -Arguments $sameVersionUpdateArguments
        if ($exitCode -ne 0 -and
            -not (Test-Path -LiteralPath $transactionCleanup) -and
            -not (Test-Path -LiteralPath $transactionCleanupOwner)) {
            # The independently verified tombstone was removed. Allow a transient scanner lock in the new transaction
            # to settle, then require a clean end-to-end update.
            Start-Sleep -Milliseconds 250
            $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V2 `
                -Arguments $sameVersionUpdateArguments
        }
        if ($exitCode -ne 0) {
            $cleanupEntries = if (Test-Path -LiteralPath $transactionCleanup) {
                @(Get-ChildItem -LiteralPath $transactionCleanup -Force -ErrorAction SilentlyContinue |
                        Select-Object -ExpandProperty Name) -join ", "
            }
            else {
                "<absent>"
            }
            throw "The external cleanup ownership record could not recover a partial tombstone " +
                "(exit=$exitCode, root=$(Test-Path -LiteralPath $transactionRoot), " +
                "cleanup=$(Test-Path -LiteralPath $transactionCleanup), " +
                "owner=$(Test-Path -LiteralPath $transactionCleanupOwner), entries=$cleanupEntries)."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        Assert-NoHubTransaction

        $updateArguments = @("/S", "/KEIRE_HUB_UPDATE=1", "/INSTALL_ROOT=$installRoot",
            "/RESUME_TOKEN=$resumeToken", "/WAIT_PROCESS=0", "/FROM_VERSION=2.0.0",
            "/TO_VERSION=3.0.0")

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.PublishedValidationFailure `
            -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The injected promoted-payload validation failure unexpectedly succeeded."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "Promoted-payload validation rollback modified an unknown colocated file."
        }
        Assert-NoHubTransaction

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.PublishedDocumentationFailure `
            -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The injected non-executable published-payload validation failure unexpectedly succeeded."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        if ((Get-Content -LiteralPath (Join-Path $installRoot "README.md") -Raw).Trim() -ne "payload-2.0.0") {
            throw "Non-executable published-payload validation rollback did not restore documentation."
        }
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "Non-executable published-payload validation rollback modified an unknown colocated file."
        }
        Assert-NoHubTransaction

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.StageFailure -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The injected stage failure unexpectedly succeeded."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        Assert-NoHubTransaction

        New-Item -ItemType Directory -Path $transactionRoot -Force | Out-Null
        $unownedSentinel = Join-Path $transactionRoot "unowned.txt"
        Set-Content -LiteralPath $unownedSentinel -Value "do-not-delete" -Encoding UTF8
        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V3 -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The Hub updater accepted an unowned transaction directory."
        }
        if ((Get-Content -LiteralPath $unownedSentinel -Raw).Trim() -ne "do-not-delete") {
            throw "The Hub updater removed an unowned transaction directory."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        Remove-Item -LiteralPath $transactionRoot -Recurse -Force

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.CommitFailure -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The injected mid-commit failure unexpectedly succeeded."
        }
        Assert-HubPayloadVersion -ExpectedVersion "2.0.0"
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "Rollback modified an unknown colocated file."
        }
        Assert-NoHubTransaction

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.Interrupted -Arguments $updateArguments
        if ($exitCode -eq 0) {
            throw "The interrupted Hub update unexpectedly reported success."
        }
        $transactionMarkerPresent = Test-Path -LiteralPath `
            (Join-Path $transactionRoot ".keire-hub-transaction") -PathType Leaf
        $transactionBackupPresent = Test-Path -LiteralPath `
            (Join-Path $transactionRoot "backup\bin") -PathType Container
        if (-not $transactionMarkerPresent -or -not $transactionBackupPresent) {
            throw "The interrupted Hub update did not preserve its owned recovery journal and backup " +
                "(root=$(Test-Path -LiteralPath $transactionRoot), marker=$transactionMarkerPresent, " +
                "backup=$transactionBackupPresent, cleanup=$(Test-Path -LiteralPath $transactionCleanup), " +
                "exit=$exitCode)."
        }
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "The interrupted Hub update modified an unknown colocated file."
        }

        $exitCode = Invoke-HubFixtureInstaller -Installer $installers.V3 -Arguments $updateArguments
        if ($exitCode -ne 0) {
            throw "The Hub updater could not recover and finish after an interruption (exit $exitCode)."
        }
        Assert-HubPayloadVersion -ExpectedVersion "3.0.0"
        if ((Get-Content -LiteralPath $unknownFile -Raw).Trim() -ne "preserve-me") {
            throw "Interrupted-update recovery modified an unknown colocated file."
        }
        Assert-NoHubTransaction
    }
    finally {
        if ($holdingInstallerProcess) {
            if (-not $holdingInstallerProcess.HasExited) {
                [void]$holdingInstallerProcess.WaitForExit(10000)
            }
            $holdingInstallerProcess.Dispose()
        }
        if ($reparseLink -and (Test-Path -LiteralPath $reparseLink)) {
            $reparseItem = Get-Item -LiteralPath $reparseLink -Force
            if (($reparseItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Remove-Item -LiteralPath $reparseLink -Force -ErrorAction SilentlyContinue
            }
        }
        if ($reparseLink -and (Test-Path -LiteralPath $reparseLink)) {
            throw "The Hub installer fixture reparse point could not be removed safely: $reparseLink"
        }
        Remove-Item -LiteralPath $productRegistry -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $uninstallRegistry -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $protocolRegistry -Recurse -Force -ErrorAction SilentlyContinue
        if ($startMenuPath.StartsWith($programsRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $startMenuPath -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($desktopShortcut.StartsWith($desktopRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $desktopShortcut -Force -ErrorAction SilentlyContinue
        }
        if ($fixtureRoot.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            for ($cleanupAttempt = 0; $cleanupAttempt -lt 5 -and (Test-Path -LiteralPath $fixtureRoot);
                ++$cleanupAttempt) {
                Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
                if (Test-Path -LiteralPath $fixtureRoot) {
                    Start-Sleep -Milliseconds 200
                }
            }
            if (Test-Path -LiteralPath $fixtureRoot) {
                throw "The Hub installer fixture directory could not be removed: $fixtureRoot"
            }
        }
    }
}

Write-Host "Windows standalone Hub installer checks passed."
