[CmdletBinding()]
param(
    [ValidateSet("all", "editor", "hub")]
    [string]$Product = "all",
    [ValidateSet("all", "legacy-migration")]
    [string]$CaseFilter = "all",
    [switch]$PreserveContexts
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"
. (Join-Path $Windows "common.ps1")
$worker = Join-Path $Root "Build\Bin\Debug-windows-x86_64\KeireInstallWorker\KeireInstallWorker.exe"
$verifier = Join-Path $Root "Build\Bin\Debug-windows-x86_64\KeireInstallVerifyFixture\KeireInstallVerifyFixture.exe"
$manifestWriter = Join-Path $PSScriptRoot "write-install-worker-fixture.py"
foreach ($required in @($worker, $verifier, $manifestWriter)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "The install-worker runtime fixture is missing: $required"
    }
}
$python = Get-PythonInvocation
$pythonPrefix = @($python.PrefixArguments)
$testRoot = [IO.Path]::GetFullPath((Join-Path $Root "Build\TestTemp\InstallWorkerRuntime"))
$testRootPrefix = $testRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Remove-TestTree {
    param([Parameter(Mandatory = $true)][string]$Path)

    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($testRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an install-worker fixture outside the test root: $full"
    }
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
}

function New-TestContext {
    param([Parameter(Mandatory = $true)][string]$CaseName)

    $id = [Guid]::NewGuid().ToString("N")
    $caseRoot = Join-Path $testRoot "$CaseName-$id"
    $shellRoot = Join-Path $testRoot "keire-install-worker-shell-tests-$id"
    New-Item -ItemType Directory -Force -Path $caseRoot, (Join-Path $shellRoot "Programs"),
        (Join-Path $shellRoot "Desktop") | Out-Null
    $context = [pscustomobject]@{
        Id = $id
        Label = $CaseName
        CaseRoot = $caseRoot
        ShellRoot = $shellRoot
        RegistryRelative = "Software\KeireInstallerTests\$id"
        RegistryPath = "Registry::HKEY_CURRENT_USER\Software\KeireInstallerTests\$id"
        InstallRoot = Join-Path $caseRoot "install"
    }
    Write-Host "==> [install worker] $CaseName"
    Write-Host "    filesystem=$caseRoot"
    Write-Host "    registry=$($context.RegistryPath)"
    return $context
}

function Enter-TestContext {
    param([Parameter(Mandatory = $true)]$Context)

    $env:KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT = $Context.RegistryRelative
    $env:KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT = $Context.ShellRoot
    Remove-Item Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER -ErrorAction SilentlyContinue
    $env:KEIRE_INSTALL_VERIFY_FIXTURE_MODE = $null
}

function Remove-TestContext {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)][bool]$Succeeded
    )

    if (-not $Succeeded -or $PreserveContexts) {
        Write-Host "Preserved install-worker fixture context: $($Context.CaseRoot)"
        Write-Host "Preserved install-worker fixture shell root: $($Context.ShellRoot)"
        Write-Host "Preserved install-worker fixture registry: $($Context.RegistryPath)"
        return
    }

    Remove-Item Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER -ErrorAction SilentlyContinue
    $env:KEIRE_INSTALL_VERIFY_FIXTURE_MODE = $null
    $env:KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT = $null
    $env:KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT = $null
    if ($Context.RegistryRelative -notmatch '^Software\\KeireInstallerTests\\[0-9a-f]{32}$') {
        throw "Refusing to remove an unexpected test registry path: $($Context.RegistryRelative)"
    }
    Remove-Item -LiteralPath $Context.RegistryPath -Recurse -Force -ErrorAction SilentlyContinue
    Remove-TestTree -Path $Context.CaseRoot
    Remove-TestTree -Path $Context.ShellRoot
    foreach ($path in @($Context.CaseRoot, $Context.ShellRoot, $Context.RegistryPath)) {
        if (Test-Path -LiteralPath $path) {
            throw "The successful install-worker case '$($Context.Label)' left test state behind: $path"
        }
    }
    Write-Host "<== [install worker] $($Context.Label) passed"
}

function Write-TestCaseFailure {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$ErrorRecord
    )

    $details = @(
        "case=$($Context.Label)",
        "filesystem=$($Context.CaseRoot)",
        "registry=$($Context.RegistryPath)",
        "position=$($ErrorRecord.InvocationInfo.PositionMessage)",
        "exception=$($ErrorRecord.Exception.ToString())"
    )
    $inner = $ErrorRecord.Exception.InnerException
    while ($null -ne $inner) {
        $details += "innerException=$($inner.ToString())"
        $inner = $inner.InnerException
    }
    $failurePath = Join-Path $Context.CaseRoot "failure.txt"
    [IO.File]::WriteAllText($failurePath, ($details -join [Environment]::NewLine))
    Write-Warning "Install-worker case '$($Context.Label)' failed; evidence: $failurePath"
    Write-Host ($details -join [Environment]::NewLine)
}

function Get-ProductInfo {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -eq "editor") {
        return [pscustomobject]@{
            Name = "editor"
            Target = "KeireClient.exe"
            Display = "Kéire Editor"
            ProductRegistry = "Product\Editor"
            UninstallRegistry = "Uninstall\Editor"
        }
    }
    return [pscustomobject]@{
        Name = "hub"
        Target = "KeireHub.exe"
        Display = "Kéire Hub"
        ProductRegistry = "Product\HubInstaller"
        UninstallRegistry = "Uninstall\Hub"
    }
}

function New-PackageFixture {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$Info,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $stage = Join-Path $Context.CaseRoot "stage-$Version"
    foreach ($directory in @("bin", "Config\nested", "Docs\nested", "Samples\nested")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage $directory) | Out-Null
    }
    Copy-Item -LiteralPath $worker -Destination (Join-Path $stage "bin\KeireInstallWorker.exe")
    Copy-Item -LiteralPath $verifier -Destination (Join-Path $stage "bin\$($Info.Target)")
    Copy-Item -LiteralPath $verifier -Destination (Join-Path $stage "Uninstall.exe")
    [IO.File]::WriteAllText((Join-Path $stage "README.md"), "payload-$Version")
    foreach ($directory in @("Config", "Docs", "Samples")) {
        [IO.File]::WriteAllText((Join-Path $stage "$directory\owned.txt"), "payload-$Version-$directory")
    }
    & $python.Executable @pythonPrefix $manifestWriter --stage $stage --artifact $Info.Name --version $Version
    if ($LASTEXITCODE -ne 0) {
        throw "The $($Info.Name) schema-2 fixture manifest could not be generated."
    }
    return $stage
}

function Invoke-Worker {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int[]]$ExpectedExitCodes
    )

    $output = @(& $worker @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -notin $ExpectedExitCodes) {
        throw "KeireInstallWorker $($Arguments -join ' ') exited $exitCode; expected " +
            "$($ExpectedExitCodes -join ', ').`n$($output -join [Environment]::NewLine)"
    }
    return $exitCode
}

function Assert-WorkerFaultCleared {
    Remove-Item Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER -ErrorAction SilentlyContinue
    if (Test-Path Env:\KEIRE_INSTALL_WORKER_INTERRUPT_AFTER) {
        throw "The install-worker interruption selector could not be removed from the process environment."
    }
}

function Get-ShellLinks {
    param([Parameter(Mandatory = $true)]$Context)

    return @(Get-ChildItem -LiteralPath $Context.ShellRoot -Recurse -Filter "*.lnk" -File `
            -ErrorAction SilentlyContinue)
}

function Assert-NoShellReceipt {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$Info
    )

    $keyPath = Join-Path $Context.RegistryPath $Info.ProductRegistry
    if (-not (Test-Path -LiteralPath $keyPath)) {
        return
    }
    $key = Get-Item -LiteralPath $keyPath
    foreach ($name in @("PendingShellIntegrationReceipt", "ShellIntegrationReceipt")) {
        if ($null -ne $key.GetValue($name, $null)) {
            throw "$($Info.Name) recovery left the $name value behind."
        }
    }
}

function Get-ShellReceiptDocument {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$Info,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $keyPath = Join-Path $Context.RegistryPath $Info.ProductRegistry
    $bytes = Get-ItemPropertyValue -LiteralPath $keyPath -Name $Name
    return $bytes | ConvertFrom-Json
}

function Assert-NormalizedShellReceiptEntries {
    param(
        [Parameter(Mandatory = $true)]$Document,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $entries = @($Document.entries)
    if ($entries.Count -eq 0) {
        throw "$Label did not contain any owned shortcut entries."
    }
    foreach ($entry in $entries) {
        if ($entry.createdByPending -ne $false -or
            -not [string]::IsNullOrEmpty([string]$entry.temporaryRoot)) {
            throw "$Label retained transaction-local shortcut state for '$($entry.kind)'."
        }
    }
}

function Assert-CommittedShellReceiptNormalized {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$Info
    )

    $receipt = Get-ShellReceiptDocument -Context $Context -Info $Info -Name "ShellIntegrationReceipt"
    Assert-NormalizedShellReceiptEntries -Document $receipt -Label "$($Info.Name) committed shell receipt"
    if ($null -ne $receipt.previous) {
        throw "$($Info.Name) committed shell receipt retained its transaction rollback receipt."
    }
}

function Install-Deferred {
    param($Context, $Info, [string]$Stage)

    [void](Invoke-Worker -Arguments @("install-deferred", "--product", $Info.Name, "--source", $Stage,
            "--root", $Context.InstallRoot) -ExpectedExitCodes @(0))
    $installedVerifier = Join-Path $Context.InstallRoot "bin\$($Info.Target)"
    & $installedVerifier --verify-installation
    if ($LASTEXITCODE -ne 0) {
        throw "The installed $($Info.Name) hidden verification command failed."
    }
}

function Complete-Install {
    param($Context, $Info, [string]$Stage, [bool]$Desktop = $true)

    Install-Deferred -Context $Context -Info $Info -Stage $Stage
    [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root", $Context.InstallRoot,
            "--start-menu", "1", "--desktop", $(if ($Desktop) { "1" } else { "0" })) `
        -ExpectedExitCodes @(0))
    [void](Invoke-Worker -Arguments @("commit", "--product", $Info.Name, "--root", $Context.InstallRoot) `
        -ExpectedExitCodes @(0))
    Assert-CommittedShellReceiptNormalized -Context $Context -Info $Info
    [void](Invoke-Worker -Arguments @("--verify-installation", "--product", $Info.Name, "--root",
            $Context.InstallRoot) -ExpectedExitCodes @(0))
}

function Test-ShellReceiptUpdateRecovery {
    param($Info)

    $context = New-TestContext -CaseName "$($Info.Name)-shell-update-recovery"
    Enter-TestContext -Context $context
    $caseSucceeded = $false
    try {
        $first = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        $second = New-PackageFixture -Context $context -Info $Info -Version "2.0.0"
        Complete-Install -Context $context -Info $Info -Stage $first

        Install-Deferred -Context $context -Info $Info -Stage $second
        [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root",
                $context.InstallRoot, "--start-menu", "1", "--desktop", "1") -ExpectedExitCodes @(0))
        $pending = Get-ShellReceiptDocument -Context $context -Info $Info `
            -Name "PendingShellIntegrationReceipt"
        Assert-NormalizedShellReceiptEntries -Document $pending `
            -Label "$($Info.Name) reused pending shell receipt"
        if ($null -eq $pending.previous) {
            throw "$($Info.Name) update did not retain its validated prior shell receipt for rollback."
        }
        Assert-NormalizedShellReceiptEntries -Document $pending.previous `
            -Label "$($Info.Name) prior rollback shell receipt"

        $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = "shellCommitted"
        [void](Invoke-Worker -Arguments @("commit", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(86))
        Assert-WorkerFaultCleared
        [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ([IO.File]::ReadAllText((Join-Path $context.InstallRoot "README.md")) -ne "payload-1.0.0") {
            throw "$($Info.Name) shell-commit interruption did not restore the prior update payload."
        }
        Assert-CommittedShellReceiptNormalized -Context $context -Info $Info
        $productKey = Join-Path $context.RegistryPath $Info.ProductRegistry
        if ($null -ne (Get-Item -LiteralPath $productKey).GetValue("PendingShellIntegrationReceipt", $null)) {
            throw "$($Info.Name) update recovery retained its pending shell receipt."
        }
        if ((Get-ShellLinks -Context $context).Count -ne 3) {
            throw "$($Info.Name) update recovery did not preserve the exact prior shortcuts."
        }
        $transactionResidue = @(Get-ChildItem -LiteralPath $context.CaseRoot -Force |
                Where-Object { $_.Name.StartsWith("install.__keire-install-transaction") })
        if ($transactionResidue.Count -ne 0) {
            throw "$($Info.Name) update recovery retained pinned transaction state before retry."
        }

        Complete-Install -Context $context -Info $Info -Stage $second
        Assert-CommittedShellReceiptNormalized -Context $context -Info $Info
        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Test-ShellFaultRecovery {
    param($Info, [string]$Phase)

    $context = New-TestContext -CaseName "$($Info.Name)-$Phase"
    Enter-TestContext -Context $context
    $caseSucceeded = $false
    try {
        $stage = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        Install-Deferred -Context $context -Info $Info -Stage $stage
        if ($Phase -eq "shellCommitted") {
            [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root",
                    $context.InstallRoot, "--start-menu", "1", "--desktop", "1") -ExpectedExitCodes @(0))
            $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = $Phase
            [void](Invoke-Worker -Arguments @("commit", "--product", $Info.Name, "--root", $context.InstallRoot) `
                -ExpectedExitCodes @(86))
        }
        else {
            $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = $Phase
            [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root",
                    $context.InstallRoot, "--start-menu", "1", "--desktop", "1") -ExpectedExitCodes @(86))
        }
        Assert-WorkerFaultCleared
        [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ((Get-ShellLinks -Context $context).Count -ne 0) {
            throw "$($Info.Name) $Phase recovery left a worker-created shortcut behind."
        }
        if (Test-Path -LiteralPath (Join-Path $context.InstallRoot "bin\$($Info.Target)")) {
            throw "$($Info.Name) $Phase recovery left the interrupted fresh payload active."
        }
        Assert-NoShellReceipt -Context $context -Info $Info

        Complete-Install -Context $context -Info $Info -Stage $stage
        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ((Get-ShellLinks -Context $context).Count -ne 0) {
            throw "$($Info.Name) retry after $Phase did not remove its exact shortcuts."
        }
        $startMenu = Join-Path $context.ShellRoot "Programs\$($Info.Display)"
        if (Test-Path -LiteralPath $startMenu) {
            throw "$($Info.Name) retry after $Phase did not prune its empty Start Menu directory."
        }
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Test-UninstallCrashRecovery {
    param($Info)

    $context = New-TestContext -CaseName "$($Info.Name)-uninstall-committed"
    Enter-TestContext -Context $context
    $caseSucceeded = $false
    try {
        $stage = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        Complete-Install -Context $context -Info $Info -Stage $stage
        $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = "committed"
        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(1))
        Assert-WorkerFaultCleared
        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ((Get-ShellLinks -Context $context).Count -ne 0) {
            throw "$($Info.Name) uninstall rerun left shortcuts after the payload committed first."
        }
        Assert-NoShellReceipt -Context $context -Info $Info
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Test-MaliciousShellReceipts {
    param($Info)

    $mutations = @(
        @{ Name = "unknown-kind"; Apply = {
                param($document)
                $document.entries[0].kind = "unknown"
            } },
        @{ Name = "duplicate-kind"; Apply = {
                param($document)
                $duplicate = $document.entries[0] | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                $document.entries = @($document.entries) + @($duplicate)
            } },
        @{ Name = "duplicate-path"; Apply = {
                param($document)
                $document.entries[2].path = $document.entries[0].path
            } },
        @{ Name = "cross-product-previous"; Apply = {
                param($document)
                $previous = $document | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                $previous.product = if ($Info.Name -eq "editor") { "hub" } else { "editor" }
                Add-Member -InputObject $document -MemberType NoteProperty -Name "previous" -Value $previous
            } },
        @{ Name = "cross-root-previous"; Apply = {
                param($document)
                $previous = $document | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                $previous.root = Join-Path $testRoot "forged-cross-root"
                Add-Member -InputObject $document -MemberType NoteProperty -Name "previous" -Value $previous
            } },
        @{ Name = "nested-previous"; Apply = {
                param($document)
                $previous = $document | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                $nested = $document | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                Add-Member -InputObject $previous -MemberType NoteProperty -Name "previous" -Value $nested
                Add-Member -InputObject $document -MemberType NoteProperty -Name "previous" -Value $previous
            } },
        @{ Name = "forged-previous-binding"; Apply = {
                param($document)
                $previous = $document | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json
                $previous.installationId = "forged-installation-id"
                Add-Member -InputObject $document -MemberType NoteProperty -Name "previous" -Value $previous
            } }
    )

    foreach ($mutation in $mutations) {
        $context = New-TestContext -CaseName "$($Info.Name)-malicious-$($mutation.Name)"
        Enter-TestContext -Context $context
        $caseSucceeded = $false
        try {
            $stage = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
            Install-Deferred -Context $context -Info $Info -Stage $stage
            $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = "shellIntent"
            [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root",
                    $context.InstallRoot, "--start-menu", "1", "--desktop", "1") -ExpectedExitCodes @(86))
            Assert-WorkerFaultCleared
            $productKey = Join-Path $context.RegistryPath $Info.ProductRegistry
            $original = Get-ItemPropertyValue -LiteralPath $productKey -Name "PendingShellIntegrationReceipt"
            $document = $original | ConvertFrom-Json
            & $mutation.Apply $document
            $malicious = $document | ConvertTo-Json -Depth 30 -Compress
            Set-ItemProperty -LiteralPath $productKey -Name "PendingShellIntegrationReceipt" -Value $malicious
            $sentinel = Join-Path $context.ShellRoot "external-sentinel.txt"
            [IO.File]::WriteAllText($sentinel, "preserve-malicious-receipt-neighbor")
            [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
                -ExpectedExitCodes @(1))
            if ([IO.File]::ReadAllText($sentinel) -ne "preserve-malicious-receipt-neighbor" -or
                (Get-ShellLinks -Context $context).Count -ne 0) {
                throw "$($Info.Name) malicious $($mutation.Name) receipt mutated shell content."
            }
            Set-ItemProperty -LiteralPath $productKey -Name "PendingShellIntegrationReceipt" -Value $original
            [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
                -ExpectedExitCodes @(0))
            Assert-NoShellReceipt -Context $context -Info $Info
            $caseSucceeded = $true
        }
        catch {
            Write-TestCaseFailure -Context $context -ErrorRecord $_
            throw
        }
        finally {
            Remove-TestContext -Context $context -Succeeded $caseSucceeded
        }
    }
}

function Test-DriftSafeUninstall {
    param($Info)

    $context = New-TestContext -CaseName "$($Info.Name)-drift"
    Enter-TestContext -Context $context
    $caseSucceeded = $false
    try {
        $stage = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        Complete-Install -Context $context -Info $Info -Stage $stage
        $links = Get-ShellLinks -Context $context
        if ($links.Count -ne 3) {
            throw "$($Info.Name) created $($links.Count) shortcuts instead of three."
        }
        $desktopLink = Join-Path $context.ShellRoot "Desktop\$($Info.Display).lnk"
        [IO.File]::WriteAllText($desktopLink, "user-replaced-link")
        $startMenu = Join-Path $context.ShellRoot "Programs\$($Info.Display)"
        $shellNeighbor = Join-Path $startMenu "user-neighbor.txt"
        [IO.File]::WriteAllText($shellNeighbor, "preserve-shell-neighbor")
        foreach ($directory in @("Config", "Docs", "Samples")) {
            [IO.File]::WriteAllText((Join-Path $context.InstallRoot "$directory\user-neighbor.txt"),
                "preserve-$directory")
        }
        [IO.File]::WriteAllText((Join-Path $context.InstallRoot "README.md"), "user-modified-readme")
        [IO.File]::WriteAllText((Join-Path $context.InstallRoot "bin\KeireInstallWorker.exe"),
            "replaced-installed-worker-must-not-run")

        $productKey = Join-Path $context.RegistryPath $Info.ProductRegistry
        $uninstallKey = Join-Path $context.RegistryPath $Info.UninstallRegistry
        New-ItemProperty -LiteralPath $productKey -Name "ExternalOwner" -Value "preserve-product" `
            -PropertyType String -Force | Out-Null
        New-ItemProperty -LiteralPath $uninstallKey -Name "ExternalOwner" -Value "preserve-uninstall" `
            -PropertyType String -Force | Out-Null
        if ($Info.Name -eq "hub") {
            $protocolRoot = Join-Path $context.RegistryPath "Classes\keirehub"
            $protocolCommand = Join-Path $protocolRoot "shell\open\command"
            Set-ItemProperty -LiteralPath $protocolCommand -Name "(default)" -Value '"external-hub.exe" "%1"'
            New-ItemProperty -LiteralPath $protocolRoot -Name "ExternalOwner" -Value "preserve-protocol" `
                -PropertyType String -Force | Out-Null
        }

        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ([IO.File]::ReadAllText($desktopLink) -ne "user-replaced-link" -or
            [IO.File]::ReadAllText($shellNeighbor) -ne "preserve-shell-neighbor") {
            throw "$($Info.Name) uninstall changed an external shell integration."
        }
        if ((Get-ShellLinks -Context $context).Count -ne 1) {
            throw "$($Info.Name) uninstall did not remove only its two exact Start Menu links."
        }
        foreach ($directory in @("Config", "Docs", "Samples")) {
            $neighbor = Join-Path $context.InstallRoot "$directory\user-neighbor.txt"
            if ([IO.File]::ReadAllText($neighbor) -ne "preserve-$directory") {
                throw "$($Info.Name) uninstall changed the unowned nested $directory file."
            }
        }
        if ([IO.File]::ReadAllText((Join-Path $context.InstallRoot "README.md")) -ne "user-modified-readme" -or
            [IO.File]::ReadAllText((Join-Path $context.InstallRoot "bin\KeireInstallWorker.exe")) -ne
                "replaced-installed-worker-must-not-run") {
            throw "$($Info.Name) uninstall removed a drifted receipt-owned file."
        }
        if ((Get-ItemPropertyValue -LiteralPath $productKey -Name "ExternalOwner") -ne "preserve-product" -or
            (Get-ItemPropertyValue -LiteralPath $uninstallKey -Name "ExternalOwner") -ne "preserve-uninstall") {
            throw "$($Info.Name) uninstall removed external registry values."
        }
        if ($Info.Name -eq "hub") {
            $protocolRoot = Join-Path $context.RegistryPath "Classes\keirehub"
            if ((Get-ItemPropertyValue -LiteralPath $protocolRoot -Name "ExternalOwner") -ne "preserve-protocol") {
                throw "Hub uninstall removed the drifted external protocol registration."
            }
        }
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Write-LegacyRegistration {
    param(
        [Parameter(Mandatory = $true)]$Context,
        [Parameter(Mandatory = $true)]$Info,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $markerGuid = if ($Info.Name -eq "editor") {
        "{1D37B84D-13B7-4C73-96BD-6D23AD40757A}"
    }
    else {
        "{B2499023-1E3C-4F87-A8D5-E8DFA0470B97}"
    }
    $marker = "$markerGuid|Keire"
    [IO.File]::WriteAllText((Join-Path $Context.InstallRoot ".keire-$($Info.Name)-install"),
        "$marker`r`n", [Text.Encoding]::ASCII)

    $productKey = Join-Path $Context.RegistryPath $Info.ProductRegistry
    $uninstallKey = Join-Path $Context.RegistryPath $Info.UninstallRegistry
    New-Item -ItemType Directory -Force -Path $productKey, $uninstallKey | Out-Null
    New-ItemProperty -LiteralPath $productKey -Name "InstallDirectory" -Value $Context.InstallRoot `
        -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath $productKey -Name "OwnershipMarker" -Value $marker `
        -PropertyType String -Force | Out-Null
    $uninstaller = '"' + (Join-Path $Context.InstallRoot "Uninstall.exe") + '"'
    $values = @{
        DisplayName = $Info.Display
        DisplayVersion = $Version
        DisplayIcon = Join-Path $Context.InstallRoot "bin\$($Info.Target)"
        Publisher = "Kéire"
        InstallLocation = $Context.InstallRoot
        UninstallString = $uninstaller
        QuietUninstallString = "$uninstaller /S"
        OwnershipMarker = $marker
    }
    foreach ($entry in $values.GetEnumerator()) {
        New-ItemProperty -LiteralPath $uninstallKey -Name $entry.Key -Value $entry.Value `
            -PropertyType String -Force | Out-Null
    }
    foreach ($name in @("NoModify", "NoRepair")) {
        New-ItemProperty -LiteralPath $uninstallKey -Name $name -Value 1 -PropertyType DWord -Force | Out-Null
    }

    if ($Info.Name -eq "hub") {
        $protocolRoot = Join-Path $Context.RegistryPath "Classes\keirehub"
        $protocolIcon = Join-Path $protocolRoot "DefaultIcon"
        $protocolCommand = Join-Path $protocolRoot "shell\open\command"
        New-Item -ItemType Directory -Force -Path $protocolRoot, $protocolIcon, $protocolCommand | Out-Null
        New-ItemProperty -LiteralPath $protocolRoot -Name "(default)" -Value "URL:Kéire Hub Protocol" `
            -PropertyType String -Force | Out-Null
        New-ItemProperty -LiteralPath $protocolRoot -Name "URL Protocol" -Value "" `
            -PropertyType String -Force | Out-Null
        New-ItemProperty -LiteralPath $protocolIcon -Name "(default)" `
            -Value ((Join-Path $Context.InstallRoot "bin\$($Info.Target)") + ",0") `
            -PropertyType String -Force | Out-Null
        New-ItemProperty -LiteralPath $protocolCommand -Name "(default)" `
            -Value ('"' + (Join-Path $Context.InstallRoot "bin\$($Info.Target)") + '" "%1"') `
            -PropertyType String -Force | Out-Null
    }
}

function Test-LegacyMigrationRecovery {
    param($Info)

    $context = New-TestContext -CaseName "$($Info.Name)-legacy-migration"
    Enter-TestContext -Context $context
    $caseSucceeded = $false
    try {
        $legacy = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        $replacement = New-PackageFixture -Context $context -Info $Info -Version "2.0.0"
        New-Item -ItemType Directory -Force -Path $context.InstallRoot | Out-Null
        Copy-Item -Path (Join-Path $legacy "*") -Destination $context.InstallRoot -Recurse -Force
        Write-LegacyRegistration -Context $context -Info $Info -Version "1.0.0"
        foreach ($directory in @("Config", "Docs", "Samples")) {
            [IO.File]::WriteAllText((Join-Path $context.InstallRoot "$directory\user-neighbor.txt"),
                "preserve-legacy-$directory")
        }

        $uninstallKey = Join-Path $context.RegistryPath $Info.UninstallRegistry
        $displayName = Get-ItemPropertyValue -LiteralPath $uninstallKey -Name "DisplayName"
        Set-ItemProperty -LiteralPath $uninstallKey -Name "DisplayName" -Value "external-product"
        [void](Invoke-Worker -Arguments @("install-deferred", "--product", $Info.Name, "--source", $replacement,
                "--root", $context.InstallRoot) -ExpectedExitCodes @(1))
        if (Test-Path -LiteralPath (Join-Path $context.InstallRoot ".keire-install-receipt.json")) {
            throw "$($Info.Name) mismatched legacy registration acquired a modern receipt."
        }
        Set-ItemProperty -LiteralPath $uninstallKey -Name "DisplayName" -Value $displayName

        if ($Info.Name -eq "hub") {
            $protocolIcon = Join-Path $context.RegistryPath "Classes\keirehub\DefaultIcon"
            $expectedIcon = Get-ItemPropertyValue -LiteralPath $protocolIcon -Name "(default)"
            Set-ItemProperty -LiteralPath $protocolIcon -Name "(default)" -Value "external-hub.exe,0"
            [void](Invoke-Worker -Arguments @("install-deferred", "--product", "hub", "--source", $replacement,
                    "--root", $context.InstallRoot) -ExpectedExitCodes @(1))
            if (Test-Path -LiteralPath (Join-Path $context.InstallRoot ".keire-install-receipt.json")) {
                throw "Hub mismatched legacy protocol acquired a modern receipt."
            }
            Set-ItemProperty -LiteralPath $protocolIcon -Name "(default)" -Value $expectedIcon
        }

        $env:KEIRE_INSTALL_WORKER_INTERRUPT_AFTER = "registrationWritten"
        [void](Invoke-Worker -Arguments @("install-deferred", "--product", $Info.Name, "--source", $replacement,
                "--root", $context.InstallRoot) -ExpectedExitCodes @(86))
        Assert-WorkerFaultCleared
        [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        if ([IO.File]::ReadAllText((Join-Path $context.InstallRoot "README.md")) -ne "payload-1.0.0") {
            throw "$($Info.Name) interrupted legacy migration did not restore the previous payload."
        }
        foreach ($directory in @("Config", "Docs", "Samples")) {
            if ([IO.File]::ReadAllText((Join-Path $context.InstallRoot "$directory\user-neighbor.txt")) -ne
                "preserve-legacy-$directory") {
                throw "$($Info.Name) legacy migration recovery changed unowned $directory content."
            }
        }
        [void](Invoke-Worker -Arguments @("--verify-installation", "--product", $Info.Name, "--root",
                $context.InstallRoot) -ExpectedExitCodes @(0))
        $productKey = Join-Path $context.RegistryPath $Info.ProductRegistry
        if (-not (Get-ItemPropertyValue -LiteralPath $productKey -Name "ProductId") -or
            -not (Test-Path -LiteralPath (Join-Path $context.InstallRoot ".keire-install-receipt.json"))) {
            throw "$($Info.Name) recovery did not retain the valid receipt migration binding."
        }

        Complete-Install -Context $context -Info $Info -Stage $replacement
        [void](Invoke-Worker -Arguments @("uninstall", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Test-ReparseKnownFolderRejection {
    param($Info)

    $context = New-TestContext -CaseName "$($Info.Name)-shell-reparse"
    Enter-TestContext -Context $context
    $programs = Join-Path $context.ShellRoot "Programs"
    $caseSucceeded = $false
    try {
        $stage = New-PackageFixture -Context $context -Info $Info -Version "1.0.0"
        Install-Deferred -Context $context -Info $Info -Stage $stage
        $outside = Join-Path $context.CaseRoot "outside-programs"
        New-Item -ItemType Directory -Force -Path $outside | Out-Null
        $sentinel = Join-Path $outside "sentinel.txt"
        [IO.File]::WriteAllText($sentinel, "preserve-reparse-target")
        Remove-Item -LiteralPath $programs -Force
        New-Item -ItemType Junction -Path $programs -Target $outside | Out-Null
        [void](Invoke-Worker -Arguments @("integrate", "--product", $Info.Name, "--root", $context.InstallRoot,
                "--start-menu", "1", "--desktop", "0") -ExpectedExitCodes @(1))
        if ([IO.File]::ReadAllText($sentinel) -ne "preserve-reparse-target") {
            throw "$($Info.Name) shell integration traversed a reparse-point known-folder parent."
        }
        Remove-Item -LiteralPath $programs -Force
        [void](Invoke-Worker -Arguments @("recover", "--product", $Info.Name, "--root", $context.InstallRoot) `
            -ExpectedExitCodes @(0))
        $caseSucceeded = $true
    }
    catch {
        Write-TestCaseFailure -Context $context -ErrorRecord $_
        throw
    }
    finally {
        if (Test-Path -LiteralPath $programs) {
            $item = Get-Item -LiteralPath $programs -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Remove-Item -LiteralPath $programs -Force
            }
        }
        Remove-TestContext -Context $context -Succeeded $caseSucceeded
    }
}

function Test-HubProtocolSkeletonSafety {
    $info = Get-ProductInfo -Name "hub"
    foreach ($scenario in @("empty", "unknown-value", "unknown-subkey")) {
        $context = New-TestContext -CaseName "hub-protocol-$scenario"
        Enter-TestContext -Context $context
        $caseSucceeded = $false
        try {
            $stage = New-PackageFixture -Context $context -Info $info -Version "1.0.0"
            $protocolRoot = Join-Path $context.RegistryPath "Classes\keirehub"
            New-Item -ItemType Directory -Force -Path (Join-Path $protocolRoot "DefaultIcon"),
                (Join-Path $protocolRoot "shell\open\command") | Out-Null
            if ($scenario -eq "unknown-value") {
                New-ItemProperty -LiteralPath $protocolRoot -Name "ExternalOwner" -Value "preserve-value" `
                    -PropertyType String -Force | Out-Null
            }
            elseif ($scenario -eq "unknown-subkey") {
                New-Item -ItemType Directory -Force -Path (Join-Path $protocolRoot "external-owner") | Out-Null
            }
            $expected = if ($scenario -eq "empty") { @(0) } else { @(1) }
            [void](Invoke-Worker -Arguments @("install-deferred", "--product", "hub", "--source", $stage,
                    "--root", $context.InstallRoot) -ExpectedExitCodes $expected)
            if ($scenario -eq "empty") {
                [void](Invoke-Worker -Arguments @("recover", "--product", "hub", "--root", $context.InstallRoot) `
                    -ExpectedExitCodes @(0))
            }
            elseif ($scenario -eq "unknown-value") {
                if ((Get-ItemPropertyValue -LiteralPath $protocolRoot -Name "ExternalOwner") -ne "preserve-value") {
                    throw "Hub protocol validation changed an unknown value while rejecting the skeleton."
                }
            }
            elseif (-not (Test-Path -LiteralPath (Join-Path $protocolRoot "external-owner"))) {
                throw "Hub protocol validation removed an unknown subkey while rejecting the skeleton."
            }
            if ($scenario -ne "empty" -and
                (Test-Path -LiteralPath (Join-Path $context.InstallRoot "bin\KeireHub.exe"))) {
                throw "Hub protocol rejection left a fresh payload active."
            }
            $caseSucceeded = $true
        }
        catch {
            Write-TestCaseFailure -Context $context -ErrorRecord $_
            throw
        }
        finally {
            Remove-TestContext -Context $context -Succeeded $caseSucceeded
        }
    }
}

$products = if ($Product -eq "all") { @("editor", "hub") } else { @($Product) }
foreach ($productName in $products) {
    $info = Get-ProductInfo -Name $productName
    if ($CaseFilter -eq "legacy-migration") {
        Test-LegacyMigrationRecovery -Info $info
        continue
    }
    foreach ($phase in @("shellIntent", "shellStaged", "shellPublished", "shellRecorded", "shellCommitted")) {
        Test-ShellFaultRecovery -Info $info -Phase $phase
    }
    Test-ShellReceiptUpdateRecovery -Info $info
    Test-DriftSafeUninstall -Info $info
    Test-UninstallCrashRecovery -Info $info
    Test-ReparseKnownFolderRejection -Info $info
    Test-MaliciousShellReceipts -Info $info
    Test-LegacyMigrationRecovery -Info $info
}
if ($CaseFilter -eq "all" -and $Product -in @("all", "hub")) {
    Test-HubProtocolSkeletonSafety
}

Write-Host "Windows install-worker runtime checks passed."
