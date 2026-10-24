$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$installer = Join-Path $repositoryRoot 'Services\KeireDistributionService\scripts\install-windows-startup-task.ps1'
$tokens = $null
$parseErrors = $null
$installerAst = [Management.Automation.Language.Parser]::ParseFile(
    $installer,
    [ref] $tokens,
    [ref] $parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "The Windows host task installer has PowerShell parse errors: $($parseErrors[0].Message)"
}

$settingsAssignments = @($installerAst.FindAll({
    param($node)
    return $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $node.Left.VariablePath.UserPath -eq 'settings'
}, $true))
if ($settingsAssignments.Count -ne 1) {
    throw 'The host task installer must define exactly one settings assignment.'
}

# Evaluate only the settings constructor: no host startup, task registration, or administrator rights.
$settings = & ([scriptblock]::Create($settingsAssignments[0].Right.Extent.Text))
if ($settings.Priority -ne 4) {
    throw "Latency-sensitive host processes must inherit Normal priority (task priority 4), not $($settings.Priority)."
}
if ($settings.ExecutionTimeLimit -ne 'PT0S') {
    throw 'The persistent host supervisor must not have an execution time limit.'
}
if ($settings.MultipleInstances -ne 'IgnoreNew') {
    throw 'The host task must prevent duplicate supervisors.'
}
if ($settings.RestartCount -ne 999 -or $settings.RestartInterval -ne 'PT1M' -or -not $settings.StartWhenAvailable) {
    throw 'The host task must retain its startup and bounded-interval restart policy.'
}

Write-Host 'Windows host task settings regression passed: Normal priority and recovery policy retained.'
