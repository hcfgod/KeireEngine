[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("editor", "hub")]
    [string]$Product,
    [switch]$FreshOnly,
    [switch]$PreserveContext
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
. (Join-Path $Root "Scripts\Windows\common.ps1")

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

function Invoke-Process {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    return $process.ExitCode
}

function Assert-ExitCode {
    param(
        [Parameter(Mandatory = $true)][int]$Actual,
        [Parameter(Mandatory = $true)][int[]]$Expected,
        [Parameter(Mandatory = $true)][string]$Operation
    )

    if ($Actual -notin $Expected) {
        throw "$Operation exited $Actual; expected $($Expected -join ', ')."
    }
}

$makensis = Get-MakensisPath
if (-not $makensis) {
    Write-Warning "NSIS 3 is unavailable; skipped the $Product worker-authority NSIS runtime checks."
    return
}

$worker = Join-Path $Root "Build\Bin\Debug-windows-x86_64\KeireInstallWorker\KeireInstallWorker.exe"
$verifier = Join-Path $Root `
    "Build\Bin\Debug-windows-x86_64\KeireInstallVerifyFixture\KeireInstallVerifyFixture.exe"
$manifestWriter = Join-Path $PSScriptRoot "write-install-worker-fixture.py"
foreach ($required in @($worker, $verifier, $manifestWriter)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "The NSIS worker-authority fixture is missing: $required"
    }
}

$python = Get-PythonInvocation
$pythonPrefix = @($python.PrefixArguments)
$info = if ($Product -eq "editor") {
    [pscustomobject]@{
        Template = Join-Path $Root "Installer\Windows\KeireEditor.nsi"
        TargetDefine = "CLIENT_TARGET"
        Target = "KeireClient"
        Executable = "KeireClient.exe"
        ProductRegistry = "Product\Editor"
        UninstallRegistry = "Uninstall\Editor"
        LegacyMarker = ".keire-editor-install"
        PackageManifest = "editor-package.json"
        StageFailureDefine = "KEIRE_EDITOR_TEST_FAIL_DURING_STAGE"
    }
}
else {
    [pscustomobject]@{
        Template = Join-Path $Root "Installer\Windows\KeireHub.nsi"
        TargetDefine = "HUB_TARGET"
        Target = "KeireHub"
        Executable = "KeireHub.exe"
        ProductRegistry = "Product\HubInstaller"
        UninstallRegistry = "Uninstall\Hub"
        LegacyMarker = ".keire-hub-install"
        PackageManifest = "hub-package.json"
        StageFailureDefine = "KEIRE_HUB_TEST_FAIL_DURING_STAGE"
    }
}

$id = [Guid]::NewGuid().ToString("N")
$testBase = [IO.Path]::GetFullPath((Join-Path $Root "Build\TestTemp\NsisWorkerRuntime"))
$testPrefix = $testBase.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$caseRoot = [IO.Path]::GetFullPath((Join-Path $testBase "$Product-$id"))
$shellRoot = [IO.Path]::GetFullPath((Join-Path $testBase "keire-install-worker-shell-tests-$id"))
if (-not $caseRoot.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not $shellRoot.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The NSIS worker-authority fixture escaped its D-drive test root."
}

$registryRelative = "Software\KeireInstallerTests\$id"
$registryPath = "Registry::HKEY_CURRENT_USER\$registryRelative"
$productRegistry = Join-Path $registryPath $info.ProductRegistry
$processTemp = Join-Path $caseRoot "process-temp"
$savedEnvironment = @{
    TEMP = $env:TEMP
    TMP = $env:TMP
    KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT = $env:KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT
    KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT = $env:KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT
    KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER
    KEIRE_INSTALL_VERIFY_FIXTURE_MODE = $env:KEIRE_INSTALL_VERIFY_FIXTURE_MODE
}
$completed = $false

function Invoke-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Operation
    )

    $safeName = $Operation -replace '[^A-Za-z0-9_.-]', '-'
    $record = Join-Path $caseRoot "$safeName.process.txt"
    Write-Host "==> [$Product NSIS] $Operation"
    $exitCode = Invoke-Process -Path $Path -Arguments $Arguments
    [IO.File]::WriteAllText($record,
        "executable=$Path`r`narguments=$($Arguments -join ' ')`r`nexitCode=$exitCode`r`n")
    Write-Host "    exit=$exitCode"
    return $exitCode
}

function New-Payload {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Path
    )

    foreach ($directory in @("bin", "Config\nested", "Docs\nested", "Samples\nested")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Path $directory) | Out-Null
    }
    Copy-Item -LiteralPath $worker -Destination (Join-Path $Path "bin\KeireInstallWorker.exe")
    Copy-Item -LiteralPath $verifier -Destination (Join-Path $Path "bin\$($info.Executable)")
    [IO.File]::WriteAllText((Join-Path $Path "README.md"), "payload-$Version")
    foreach ($directory in @("Config", "Docs", "Samples")) {
        [IO.File]::WriteAllText((Join-Path $Path "$directory\owned.txt"), "$Product-$Version-$directory")
    }
    & $python.Executable @pythonPrefix $manifestWriter --stage $Path --artifact $Product --version $Version
    if ($LASTEXITCODE -ne 0) {
        throw "The $Product NSIS schema-2 fixture manifest could not be generated."
    }
}

function New-Installer {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Output,
        [string]$TestDefine = ""
    )

    $tracePath = "$Output.trace.log"
    $arguments = @(
        "/V2",
        "/WX",
        "/DKEIRE_INSTALL_WORKER_AUTHORITY=1",
        "/DKEIRE_INSTALL_TEST_TRACE_PATH=$tracePath",
        "/DINSTALL_OWNERSHIP_IDENTIFIER=Keire",
        "/DPRODUCT_IDENTIFIER=KeireInstallerFixture$id",
        "/DPRODUCT_DISPLAY_NAME=KeireInstallerFixture$id",
        "/DPRODUCT_VERSION=$Version",
        "/DPRODUCT_FILE_VERSION=$Version.0",
        "/DPRODUCT_ARCHITECTURE=x86_64",
        "/D$($info.TargetDefine)=$($info.Target)",
        "/DSOURCE_DIRECTORY=$Source",
        "/DOUTPUT_PATH=$Output",
        "/DLICENSE_PATH=$(Join-Path $Root 'LICENSE.txt')",
        "/DSETUP_ICON_PATH=$(Join-Path $Root 'Config\Branding\Keire.ico')"
    )
    if ($TestDefine) {
        $arguments += "/D$TestDefine=1"
    }
    $arguments += $info.Template
    $outputLines = @(& $makensis @arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "The $Product worker-authority NSIS fixture did not compile.`n" +
            ($outputLines -join [Environment]::NewLine)
    }
    if (-not (Test-Path -LiteralPath $Output -PathType Leaf)) {
        throw "NSIS did not publish the $Product fixture: $Output"
    }
}

function Assert-Version {
    param(
        [Parameter(Mandatory = $true)][string]$RootPath,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $readme = Join-Path $RootPath "README.md"
    if (-not (Test-Path -LiteralPath $readme -PathType Leaf) -or
        [IO.File]::ReadAllText($readme) -ne "payload-$Version") {
        throw "The $Product NSIS fixture did not preserve payload $Version."
    }
    $receipt = Join-Path $RootPath ".keire-install-receipt.json"
    if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
        throw "The $Product NSIS fixture is missing its JSON ownership receipt."
    }
    $output = @(& $worker --verify-installation --product $Product --root $RootPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "The $Product worker rejected the installed receipt.`n$($output -join [Environment]::NewLine)"
    }
    & (Join-Path $RootPath "bin\$($info.Executable)") --verify-installation
    if ($LASTEXITCODE -ne 0) {
        throw "The real installed $Product verification executable failed."
    }
}

function Assert-UnknownFiles {
    param([Parameter(Mandatory = $true)][string]$RootPath)

    foreach ($directory in @("Config", "Docs", "Samples")) {
        $path = Join-Path $RootPath "$directory\user\private.txt"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            [IO.File]::ReadAllText($path) -ne "preserve-$directory") {
            throw "The $Product NSIS operation changed unowned nested $directory content."
        }
    }
}

try {
    New-Item -ItemType Directory -Force -Path $caseRoot, (Join-Path $shellRoot "Programs"),
        (Join-Path $shellRoot "Desktop"), $processTemp | Out-Null
    $env:TEMP = $processTemp
    $env:TMP = $processTemp
    $env:KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT = $registryRelative
    $env:KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT = $shellRoot
    Remove-Item Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER -ErrorAction SilentlyContinue
    Remove-Item Env:\KEIRE_INSTALL_VERIFY_FIXTURE_MODE -ErrorAction SilentlyContinue

    $payload1 = Join-Path $caseRoot "payload-1"
    New-Payload -Version "1.0.0" -Path $payload1
    $installer1 = Join-Path $caseRoot "$Product-1.exe"
    New-Installer -Version "1.0.0" -Source $payload1 -Output $installer1
    if (-not $FreshOnly) {
        $payload2 = Join-Path $caseRoot "payload-2"
        New-Payload -Version "2.0.0" -Path $payload2
        $installer2 = Join-Path $caseRoot "$Product-2.exe"
        $stageFailure = Join-Path $caseRoot "$Product-stage-failure.exe"
        New-Installer -Version "2.0.0" -Source $payload2 -Output $installer2
        New-Installer -Version "1.0.0" -Source $payload1 -Output $stageFailure `
            -TestDefine $info.StageFailureDefine

        $unownedRoot = Join-Path $caseRoot "unowned"
        foreach ($directory in @("Config", "Docs", "Samples")) {
            $path = Join-Path $unownedRoot "$directory\user\private.txt"
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
            [IO.File]::WriteAllText($path, "preserve-$directory")
        }
        $exit = Invoke-Installer -Path $installer1 -Arguments @("/S", "/D=$unownedRoot") `
            -Operation "reject-unowned"
        if ($exit -eq 0) {
            throw "The $Product NSIS shell accepted a non-empty unowned destination."
        }
        Assert-UnknownFiles -RootPath $unownedRoot
        if (Test-Path -LiteralPath (Join-Path $unownedRoot ".keire-install-receipt.json")) {
            throw "The rejected $Product destination acquired an ownership receipt."
        }

        $stageFailureRoot = Join-Path $caseRoot "stage-failure"
        New-Item -ItemType Directory -Path $stageFailureRoot | Out-Null
        $exit = Invoke-Installer -Path $stageFailure -Arguments @("/S", "/D=$stageFailureRoot") `
            -Operation "stage-failure"
        if ($exit -eq 0 -or @(Get-ChildItem -LiteralPath $stageFailureRoot -Force).Count -ne 0) {
            throw "The $Product NSIS pre-worker staging fault changed the destination."
        }
    }

    $installRoot = Join-Path $caseRoot "custom-install"
    New-Item -ItemType Directory -Path $installRoot | Out-Null
    $exit = Invoke-Installer -Path $installer1 -Arguments @("/S", "/D=$installRoot") `
        -Operation "fresh-install"
    Assert-ExitCode -Actual $exit -Expected @(0) -Operation "$Product fresh NSIS install"
    Assert-Version -RootPath $installRoot -Version "1.0.0"
    if (@(Get-ChildItem -LiteralPath $shellRoot -Filter "*.lnk" -File -Recurse).Count -eq 0) {
        throw "The $Product worker-authority NSIS install did not publish its selected Start Menu link."
    }
    if ($FreshOnly) {
        $completed = $true
        Write-Host "$Product worker-authority NSIS fresh-install check passed."
        return
    }

    foreach ($directory in @("Config", "Docs", "Samples")) {
        $path = Join-Path $installRoot "$directory\user\private.txt"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        [IO.File]::WriteAllText($path, "preserve-$directory")
    }

    $installedWorker = Join-Path $installRoot "bin\KeireInstallWorker.exe"
    Copy-Item -LiteralPath $verifier -Destination $installedWorker -Force
    $exit = Invoke-Installer -Path $installer2 -Arguments @("/S", "/D=$installRoot") `
        -Operation "reject-drifted-worker-update"
    if ($exit -eq 0) {
        throw "The $Product NSIS updater accepted a drifted installed worker."
    }
    Assert-UnknownFiles -RootPath $installRoot
    if ([IO.File]::ReadAllText((Join-Path $installRoot "README.md")) -ne "payload-1.0.0") {
        throw "The rejected $Product drift update changed active payload."
    }
    Copy-Item -LiteralPath $worker -Destination $installedWorker -Force

    $receipt = Join-Path $installRoot ".keire-install-receipt.json"
    $receiptHash = (Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash
    $env:KEIRE_INSTALL_VERIFY_FIXTURE_MODE = "fail"
    $exit = Invoke-Installer -Path $installer2 -Arguments @("/S", "/D=$installRoot") `
        -Operation "verification-failure"
    if ($exit -eq 0) {
        throw "The $Product NSIS update ignored a failed real executable verification."
    }
    Remove-Item Env:\KEIRE_INSTALL_VERIFY_FIXTURE_MODE -ErrorAction SilentlyContinue
    Assert-Version -RootPath $installRoot -Version "1.0.0"
    if ((Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash -ne $receiptHash) {
        throw "The failed $Product verification changed the previous receipt."
    }
    Assert-UnknownFiles -RootPath $installRoot

    $env:KEIRE_INSTALL_VERIFY_FIXTURE_MODE = "timeout"
    $exit = Invoke-Installer -Path $installer2 -Arguments @("/S", "/D=$installRoot") `
        -Operation "verification-timeout"
    if ($exit -eq 0) {
        throw "The $Product NSIS update ignored the 30-second verification timeout."
    }
    Remove-Item Env:\KEIRE_INSTALL_VERIFY_FIXTURE_MODE -ErrorAction SilentlyContinue
    Assert-Version -RootPath $installRoot -Version "1.0.0"
    Assert-UnknownFiles -RootPath $installRoot

    $exit = Invoke-Installer -Path $installer2 -Arguments @("/S", "/D=$installRoot") `
        -Operation "update"
    Assert-ExitCode -Actual $exit -Expected @(0) -Operation "$Product NSIS update"
    Assert-Version -RootPath $installRoot -Version "2.0.0"
    Assert-UnknownFiles -RootPath $installRoot

    $externalRegistryValue = "preserve-external-registry"
    New-ItemProperty -LiteralPath $productRegistry -Name "ExternalOwner" -Value $externalRegistryValue `
        -PropertyType String -Force | Out-Null
    $uninstaller = Join-Path $installRoot "Uninstall.exe"
    Copy-Item -LiteralPath $verifier -Destination $installedWorker -Force
    $driftedWorkerHash = (Get-FileHash -LiteralPath $installedWorker -Algorithm SHA256).Hash
    $exit = Invoke-Installer -Path $uninstaller -Arguments @("/S") `
        -Operation "uninstall-with-drifted-installed-worker"
    Assert-ExitCode -Actual $exit -Expected @(0) -Operation "$Product drift-safe NSIS uninstall"
    Assert-UnknownFiles -RootPath $installRoot
    if (-not (Test-Path -LiteralPath $installedWorker -PathType Leaf) -or
        (Get-FileHash -LiteralPath $installedWorker -Algorithm SHA256).Hash -ne $driftedWorkerHash) {
        throw "The trusted $Product uninstaller changed the drifted installed worker."
    }
    foreach ($owned in @("README.md", ".keire-install-receipt.json", ".keire-install-marker.json",
            $info.LegacyMarker, $info.PackageManifest, "Uninstall.exe", "bin\$($info.Executable)",
            "Config\owned.txt", "Docs\owned.txt", "Samples\owned.txt")) {
        if (Test-Path -LiteralPath (Join-Path $installRoot $owned)) {
            throw "The $Product NSIS uninstall left receipt-owned '$owned' behind."
        }
    }
    $trace = (Get-Content -LiteralPath "$installer2.trace.log" -Raw)
    if (-not $trace.Contains("uninstall embedded worker extracted")) {
        throw "The $Product NSIS uninstall did not record extraction of its trusted embedded worker."
    }
    if ((Get-ItemPropertyValue -LiteralPath $productRegistry -Name "ExternalOwner") -ne
        $externalRegistryValue) {
        throw "The $Product NSIS uninstall changed an unrelated registry value."
    }
    $productKey = Get-Item -LiteralPath $productRegistry
    foreach ($name in @("ProductId", "InstallationId", "InstallDirectory", "DisplayVersion",
            "ManifestFingerprint", "ReceiptSha256", "PendingShellIntegrationReceipt",
            "ShellIntegrationReceipt")) {
        if ($null -ne $productKey.GetValue($name, $null)) {
            throw "The $Product NSIS uninstall left owned product registration '$name' behind."
        }
    }
    $uninstallRegistry = Join-Path $registryPath $info.UninstallRegistry
    if (Test-Path -LiteralPath $uninstallRegistry) {
        $uninstallKey = Get-Item -LiteralPath $uninstallRegistry
        foreach ($name in @("ProductId", "InstallationId", "ManifestFingerprint", "ReceiptSha256",
                "DisplayName", "DisplayVersion", "DisplayIcon", "Publisher", "InstallLocation",
                "UninstallString", "QuietUninstallString", "NoModify", "NoRepair")) {
            if ($null -ne $uninstallKey.GetValue($name, $null)) {
                throw "The $Product NSIS uninstall left owned uninstall registration '$name' behind."
            }
        }
    }
    if ($Product -eq "hub") {
        foreach ($protocolKeyPath in @("Classes\keirehub", "Classes\keirehub\DefaultIcon",
                "Classes\keirehub\shell\open\command")) {
            $protocolKey = Join-Path $registryPath $protocolKeyPath
            if (Test-Path -LiteralPath $protocolKey) {
                $key = Get-Item -LiteralPath $protocolKey
                if ($null -ne $key.GetValue("", $null) -or
                    $null -ne $key.GetValue("URL Protocol", $null)) {
                    throw "The Hub NSIS uninstall left owned protocol registration in '$protocolKeyPath'."
                }
            }
        }
    }
    if (@(Get-ChildItem -LiteralPath $shellRoot -Filter "*.lnk" -File -Recurse).Count -ne 0) {
        throw "The $Product NSIS uninstall left exact receipt-owned shortcuts behind."
    }
    $completed = $true
}
finally {
    Remove-Item Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER -ErrorAction SilentlyContinue
    Remove-Item Env:\KEIRE_INSTALL_VERIFY_FIXTURE_MODE -ErrorAction SilentlyContinue
    $cleanupFailure = ""
    if ($completed -and -not $PreserveContext) {
        if ($registryRelative -match '^Software\\KeireInstallerTests\\[0-9a-f]{32}$') {
            Remove-Item -LiteralPath $registryPath -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($caseRoot.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($shellRoot.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $shellRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
        $remaining = @()
        foreach ($path in @($caseRoot, $shellRoot, $registryPath)) {
            if (Test-Path -LiteralPath $path) {
                $remaining += $path
            }
        }
        if ($remaining.Count -ne 0) {
            $cleanupFailure = "The successful $Product NSIS fixture left test state behind: " +
                ($remaining -join ", ")
        }
    }
    else {
        Write-Warning "Preserved $Product NSIS fixture filesystem: $caseRoot"
        Write-Warning "Preserved $Product NSIS fixture shell root: $shellRoot"
        Write-Warning "Preserved $Product NSIS fixture registry: $registryPath"
        Get-ChildItem -LiteralPath $caseRoot -Filter "*.log" -File -ErrorAction SilentlyContinue |
            ForEach-Object {
                Write-Host "--- NSIS log: $($_.FullName)"
                Get-Content -LiteralPath $_.FullName -Tail 120
            }
        Get-ChildItem -LiteralPath $caseRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in @("journal.json", "owner.json") -or $_.Name -like "*.locator.json" } |
            ForEach-Object { Write-Warning "Preserved transaction evidence: $($_.FullName)" }
    }
    foreach ($name in $savedEnvironment.Keys) {
        $value = $savedEnvironment[$name]
        if ($null -eq $value) {
            Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
        }
        else {
            Set-Item "Env:\$name" $value
        }
    }
    if ($cleanupFailure) {
        throw $cleanupFailure
    }
}

Write-Host "$Product worker-authority NSIS runtime checks passed."
