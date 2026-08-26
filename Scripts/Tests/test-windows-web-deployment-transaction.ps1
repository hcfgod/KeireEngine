$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Deployment = Join-Path $Root "Services\KeireDistributionService\scripts\deploy-windows-web.ps1"
$FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("keire-windows-web-deployment-test-" + [Guid]::NewGuid().ToString("N"))
[IO.Directory]::CreateDirectory($FixtureRoot) | Out-Null

$global:KeireWebDeploymentTestState = [pscustomobject]@{
    TaskAvailable = $true
    TaskState = "Running"
    TaskArguments = ""
    TaskStopIssued = $false
    TaskPollStates = [Collections.Generic.Queue[string]]::new()
    TaskPollCount = 0
    StopFailure = $false
    StartErrors = [Collections.Generic.Queue[string]]::new()
    StartCount = 0
    MoveFailureAt = 0
    MoveCount = 0
    ProcessListening = $false
    ProcessWaitResult = $true
    ProcessWaitCalls = 0
    ProcessWaitMilliseconds = 0
    ProcessDisposeCount = 0
    ProcessStopCount = 0
    NodeExecutable = ""
    DestinationEntry = ""
}

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Get-ScheduledTask {
    [CmdletBinding()]
    param(
        [string] $TaskName,
        [string] $TaskPath
    )

    if (-not $global:KeireWebDeploymentTestState.TaskAvailable) {
        return
    }
    if (-not [string]::IsNullOrWhiteSpace($TaskName) -and
        $global:KeireWebDeploymentTestState.TaskStopIssued -and
        $global:KeireWebDeploymentTestState.TaskPollStates.Count -ne 0) {
        $global:KeireWebDeploymentTestState.TaskState =
            $global:KeireWebDeploymentTestState.TaskPollStates.Dequeue()
        $global:KeireWebDeploymentTestState.TaskPollCount++
    }
    return [pscustomobject]@{
        TaskName = "Keire Distribution Host Test"
        TaskPath = "\"
        State = $global:KeireWebDeploymentTestState.TaskState
        Actions = @([pscustomobject]@{ Arguments = $global:KeireWebDeploymentTestState.TaskArguments })
    }
}

function Stop-ScheduledTask {
    [CmdletBinding()]
    param(
        [string] $TaskName,
        [string] $TaskPath
    )

    $global:KeireWebDeploymentTestState.TaskStopIssued = $true
    $global:KeireWebDeploymentTestState.TaskState = if (
        $global:KeireWebDeploymentTestState.TaskPollStates.Count -ne 0) { "Running" } else { "Ready" }
    if ($global:KeireWebDeploymentTestState.StopFailure) {
        $global:KeireWebDeploymentTestState.StopFailure = $false
        throw "injected partial stop failure"
    }
}

function Start-ScheduledTask {
    [CmdletBinding()]
    param(
        [string] $TaskName,
        [string] $TaskPath
    )

    $global:KeireWebDeploymentTestState.StartCount++
    if ($global:KeireWebDeploymentTestState.StartErrors.Count -ne 0) {
        throw ($global:KeireWebDeploymentTestState.StartErrors.Dequeue())
    }
    $global:KeireWebDeploymentTestState.TaskState = "Running"
    $global:KeireWebDeploymentTestState.TaskStopIssued = $false
}

function Get-NetTCPConnection {
    [CmdletBinding()]
    param(
        [string] $State,
        [int] $LocalPort
    )

    if ($global:KeireWebDeploymentTestState.ProcessListening) {
        return [pscustomobject]@{ OwningProcess = 4242 }
    }
}

function Get-CimInstance {
    [CmdletBinding()]
    param(
        [Parameter(Position = 0)]
        [string] $ClassName,

        [string] $Filter
    )

    return [pscustomobject]@{
        ProcessId = 4242
        ExecutablePath = $global:KeireWebDeploymentTestState.NodeExecutable
        CommandLine = '"' + $global:KeireWebDeploymentTestState.NodeExecutable + '" "' +
            $global:KeireWebDeploymentTestState.DestinationEntry + '"'
    }
}

function Get-Process {
    [CmdletBinding()]
    param(
        [int] $Id
    )

    $process = [pscustomobject]@{ Id = $Id }
    $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {
        param([int] $Milliseconds)
        $global:KeireWebDeploymentTestState.ProcessWaitCalls++
        $global:KeireWebDeploymentTestState.ProcessWaitMilliseconds = $Milliseconds
        return $global:KeireWebDeploymentTestState.ProcessWaitResult
    }
    $process | Add-Member -MemberType ScriptMethod -Name Dispose -Value {
        $global:KeireWebDeploymentTestState.ProcessDisposeCount++
    }
    return $process
}

function Stop-Process {
    [CmdletBinding()]
    param(
        [object] $InputObject,
        [switch] $Force
    )

    $global:KeireWebDeploymentTestState.ProcessStopCount++
    $global:KeireWebDeploymentTestState.ProcessListening = $false
}

function Move-Item {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $LiteralPath,

        [Parameter(Mandatory = $true)]
        [string] $Destination
    )

    $global:KeireWebDeploymentTestState.MoveCount++
    if ($global:KeireWebDeploymentTestState.MoveFailureAt -ne 0 -and
        $global:KeireWebDeploymentTestState.MoveCount -eq $global:KeireWebDeploymentTestState.MoveFailureAt) {
        throw "injected second move failure"
    }
    Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination
}

function New-DeploymentFixture([string] $Name, [bool] $RuntimeUpdate) {
    $fixture = Join-Path $FixtureRoot $Name
    $sourceRoot = Join-Path $fixture "source"
    $sourceServer = Join-Path $sourceRoot "dist\server"
    $hostRoot = Join-Path $fixture "host"
    $webRoot = Join-Path $hostRoot "Web"
    $destinationServer = Join-Path $webRoot "dist\server"
    $scripts = Join-Path $hostRoot "scripts"
    $tools = Join-Path $fixture "tools"
    foreach ($directory in @($sourceServer, $destinationServer, $scripts, $tools)) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }

    Set-Content -LiteralPath (Join-Path $sourceServer "entry.mjs") -Value "source entry" -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $destinationServer "entry.mjs") -Value "previous entry" -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $sourceRoot "package.json") -Value '{"private":true}' -Encoding utf8NoBOM
    Set-Content -LiteralPath (Join-Path $sourceRoot "package-lock.json") -Value "source lock" -Encoding utf8NoBOM
    $destinationLock = if ($RuntimeUpdate) { "previous lock" } else { "source lock" }
    Set-Content -LiteralPath (Join-Path $webRoot "package-lock.json") -Value $destinationLock -Encoding utf8NoBOM

    $node = Join-Path $tools "node.cmd"
    $npm = Join-Path $tools "npm.cmd"
    Set-Content -LiteralPath $node -Value "@exit /b 0" -Encoding ascii
    Set-Content -LiteralPath $npm -Value "@exit /b 0" -Encoding ascii

    $supervisor = Join-Path $scripts "start-windows-host.ps1"
    Set-Content -LiteralPath $supervisor `
        -Value '& "$env:SystemRoot\System32\cmd.exe" /c exit 0' -Encoding utf8NoBOM
    $settingsPath = Join-Path $hostRoot "host-settings.json"
    [pscustomobject]@{
        schemaVersion = 2
        webRoot = $webRoot
        nodeExecutable = $node
    } | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding utf8NoBOM

    return [pscustomobject]@{
        SourceRoot = $sourceRoot
        SettingsPath = $settingsPath
        Supervisor = $supervisor
        WebRoot = $webRoot
        DestinationEntry = Join-Path $destinationServer "entry.mjs"
        NodeExecutable = $node
        Fixture = $fixture
    }
}

function Reset-Mocks([object] $Fixture) {
    $global:KeireWebDeploymentTestState.TaskAvailable = $true
    $global:KeireWebDeploymentTestState.TaskState = "Running"
    $global:KeireWebDeploymentTestState.TaskArguments = `
        "-File `"$($Fixture.Supervisor)`" -SettingsPath `"$($Fixture.SettingsPath)`" -KeepAlive"
    $global:KeireWebDeploymentTestState.StopFailure = $false
    $global:KeireWebDeploymentTestState.TaskStopIssued = $false
    $global:KeireWebDeploymentTestState.TaskPollStates.Clear()
    $global:KeireWebDeploymentTestState.TaskPollCount = 0
    $global:KeireWebDeploymentTestState.StartErrors.Clear()
    $global:KeireWebDeploymentTestState.StartCount = 0
    $global:KeireWebDeploymentTestState.MoveFailureAt = 0
    $global:KeireWebDeploymentTestState.MoveCount = 0
    $global:KeireWebDeploymentTestState.ProcessListening = $false
    $global:KeireWebDeploymentTestState.ProcessWaitResult = $true
    $global:KeireWebDeploymentTestState.ProcessWaitCalls = 0
    $global:KeireWebDeploymentTestState.ProcessWaitMilliseconds = 0
    $global:KeireWebDeploymentTestState.ProcessDisposeCount = 0
    $global:KeireWebDeploymentTestState.ProcessStopCount = 0
    $global:KeireWebDeploymentTestState.NodeExecutable = $Fixture.NodeExecutable
    $global:KeireWebDeploymentTestState.DestinationEntry = $Fixture.DestinationEntry
}

function Invoke-ExpectedFailure([object] $Fixture, [string] $ExpectedMessage) {
    try {
        & $Deployment -SourceWebRoot $Fixture.SourceRoot -SettingsPath $Fixture.SettingsPath -AllowRuntimeUpdate `
            -WarningAction SilentlyContinue
    }
    catch {
        if (-not $_.Exception.Message.Contains($ExpectedMessage)) {
            throw "Expected failure '$ExpectedMessage', received '$($_.Exception.Message)'."
        }
        return
    }
    throw "Expected deployment failure '$ExpectedMessage' did not occur."
}

function Assert-PreviousDeployment([object] $Fixture) {
    Assert-True (Test-Path -LiteralPath $Fixture.DestinationEntry -PathType Leaf) `
        "The previous deployment entry point was not restored."
    $entry = Get-Content -LiteralPath $Fixture.DestinationEntry -Raw
    Assert-True ($entry.Contains("previous entry")) "The restored deployment did not contain the previous payload."
}

try {
    $zeroTask = New-DeploymentFixture "zero-task" $false
    Reset-Mocks $zeroTask
    $global:KeireWebDeploymentTestState.TaskAvailable = $false
    Invoke-ExpectedFailure $zeroTask "Exactly one scheduled host task must use"
    Assert-True ($global:KeireWebDeploymentTestState.MoveCount -eq 0) `
        "A deployment without a configured task mutated the live payload."
    Assert-PreviousDeployment $zeroTask

    foreach ($runtimeUpdate in @($false, $true)) {
        $name = if ($runtimeUpdate) { "runtime-second-move" } else { "bundle-second-move" }
        $fixture = New-DeploymentFixture $name $runtimeUpdate
        Reset-Mocks $fixture
        $global:KeireWebDeploymentTestState.MoveFailureAt = 2
        Invoke-ExpectedFailure $fixture "injected second move failure"
        Assert-PreviousDeployment $fixture
        Assert-True ($global:KeireWebDeploymentTestState.TaskState -eq "Running") `
            "The previous host was not restarted after swap failure."
        Assert-True ($global:KeireWebDeploymentTestState.StartCount -eq 1) `
            "Swap failure did not perform exactly one recovery start."
        $rollbacks = @(Get-ChildItem -LiteralPath $fixture.Fixture -Recurse -Force | Where-Object {
                $_.Name -like "*.rollback-*"
            })
        Assert-True ($rollbacks.Count -eq 0) "Swap failure left the previous deployment stranded as a rollback."
    }

    $waitSuccess = New-DeploymentFixture "process-wait-success" $false
    Reset-Mocks $waitSuccess
    $global:KeireWebDeploymentTestState.TaskPollStates.Enqueue("Running")
    $global:KeireWebDeploymentTestState.TaskPollStates.Enqueue("Running")
    $global:KeireWebDeploymentTestState.TaskPollStates.Enqueue("Ready")
    $global:KeireWebDeploymentTestState.ProcessListening = $true
    $global:KeireWebDeploymentTestState.MoveFailureAt = 2
    Invoke-ExpectedFailure $waitSuccess "injected second move failure"
    Assert-PreviousDeployment $waitSuccess
    Assert-True ($global:KeireWebDeploymentTestState.TaskPollCount -eq 3) `
        "The deployment did not poll the scheduled task until it became ready."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessStopCount -eq 1) `
        "The configured web process was not stopped exactly once."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessWaitCalls -eq 1) `
        "The configured web process was not waited exactly once."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessWaitMilliseconds -eq 15000) `
        "The configured web process did not receive the 15-second wait contract."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessDisposeCount -eq 1) `
        "The configured web process handle was not disposed."

    $waitTimeout = New-DeploymentFixture "process-wait-timeout" $false
    Reset-Mocks $waitTimeout
    $global:KeireWebDeploymentTestState.ProcessListening = $true
    $global:KeireWebDeploymentTestState.ProcessWaitResult = $false
    Invoke-ExpectedFailure $waitTimeout "did not exit before the deployment deadline"
    Assert-PreviousDeployment $waitTimeout
    Assert-True ($global:KeireWebDeploymentTestState.TaskState -eq "Running") `
        "A process-wait timeout left the previous host stopped."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessWaitCalls -eq 1) `
        "The process-wait timeout path was not exercised."
    Assert-True ($global:KeireWebDeploymentTestState.ProcessDisposeCount -eq 1) `
        "The timed-out process handle was not disposed."

    $partialStop = New-DeploymentFixture "partial-stop" $false
    Reset-Mocks $partialStop
    $global:KeireWebDeploymentTestState.StopFailure = $true
    Invoke-ExpectedFailure $partialStop "injected partial stop failure"
    Assert-PreviousDeployment $partialStop
    Assert-True ($global:KeireWebDeploymentTestState.TaskState -eq "Running") `
        "A partial stop failure left the host task stopped."
    Assert-True ($global:KeireWebDeploymentTestState.StartCount -eq 1) `
        "A partial stop failure did not restart the previous host exactly once."
    Assert-True ($global:KeireWebDeploymentTestState.MoveCount -eq 0) `
        "A partial stop failure mutated the live payload."

    $rollbackStart = New-DeploymentFixture "rollback-start" $false
    Reset-Mocks $rollbackStart
    $global:KeireWebDeploymentTestState.StartErrors.Enqueue("injected primary startup failure")
    $global:KeireWebDeploymentTestState.StartErrors.Enqueue("injected rollback startup failure")
    Invoke-ExpectedFailure $rollbackStart "injected primary startup failure"
    Assert-PreviousDeployment $rollbackStart
    Assert-True ($global:KeireWebDeploymentTestState.StartCount -eq 2) `
        "The rollback-start failure did not exercise both start attempts."

    Write-Host "Windows web deployment transaction checks passed."
}
finally {
    $resolvedFixture = [IO.Path]::GetFullPath($FixtureRoot)
    $temporaryPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedFixture.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Variable -Name KeireWebDeploymentTestState -Scope Global -ErrorAction SilentlyContinue
}
