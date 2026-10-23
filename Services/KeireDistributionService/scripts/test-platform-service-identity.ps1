[CmdletBinding()]
param ()

$ErrorActionPreference = 'Stop'
$module = Join-Path $PSScriptRoot 'platform-service-identity-common.psm1'
Import-Module $module -Force
$checks = 0
function Assert-IdentityTest([bool] $Condition)
{
    if (-not $Condition)
    {
        throw 'Identity migration regression failed.'
    }
    $script:checks++
}
function Assert-IdentityReject([scriptblock] $Action)
{
    $rejected = $false
    try
    {
        & $Action | Out-Null
    }
    catch
    {
        $rejected = $true
    }
    Assert-IdentityTest $rejected
}

# Tests only validate pure planning, comparisons, parser structure and guards. No DPAPI/task/elevation calls.
$source = Join-Path ([IO.Path]::GetTempPath()) 'keire-identity-test-source-not-created'
$plan = [pscustomobject]@{ schemaVersion = 1; sources = @(
    [pscustomobject]@{ kind = 'mysql'; sourceDirectory = $source },
    [pscustomobject]@{ kind = 'github'; sourceDirectory = $source }) }
$entries = @(ConvertTo-KeireIdentityPlan $plan)
Assert-IdentityTest ($entries.Count -eq 3)
Assert-IdentityTest ($entries[0].filename -ceq 'mysql-settings.dpapi')
Assert-IdentityTest ($entries[1].filename -ceq 'account-record-key.dpapi')
Assert-IdentityTest ($entries[1].entropy -ceq 'Keire.Platform.AccountEncryption.v1')
Assert-IdentityTest ($entries[2].targetDirectory -ceq 'PlatformGitHub')
Assert-IdentityReject { ConvertTo-KeireIdentityPlan ([pscustomobject]@{ schemaVersion = 2; sources = $plan.sources }) }
Assert-IdentityReject { ConvertTo-KeireIdentityPlan ([pscustomobject]@{ schemaVersion = 1; sources = @() }) }
Assert-IdentityReject { ConvertTo-KeireIdentityPlan ([pscustomobject]@{ schemaVersion = 1; sources = @($plan.sources[0], $plan.sources[0]) }) }
Assert-IdentityReject { ConvertTo-KeireIdentityPlan ([pscustomobject]@{ schemaVersion = 1; sources = @(
    [pscustomobject]@{ kind = 'unknown'; sourceDirectory = $source }) }) }
Assert-IdentityReject { ConvertTo-KeireIdentityPlan ([pscustomobject]@{ schemaVersion = 1; sources = @(
    [pscustomobject]@{ kind = 'mail'; sourceDirectory = $source; password = 'not-allowed' }) }) }
Assert-IdentityReject { Resolve-KeireIdentityPath 'relative-path' }
Assert-IdentityReject { Resolve-KeireIdentityPath '\\server\share\secret' }
Assert-IdentityReject { Resolve-KeireIdentityPath 'C:\test\file:alternate-stream' }
Assert-IdentityTest (Test-KeireIdentityBytesEqual ([byte[]]@(0, 1, 255)) ([byte[]]@(0, 1, 255)))
Assert-IdentityTest (-not (Test-KeireIdentityBytesEqual ([byte[]]@(0, 1, 255)) ([byte[]]@(0, 1, 254))))
Assert-IdentityTest (-not (Test-KeireIdentityBytesEqual ([byte[]]@(0, 1)) ([byte[]]@(0, 1, 0))))
foreach ($name in @('migrate-platform-service-identity.ps1', 'complete-platform-service-identity.ps1', 'platform-service-identity-common.psm1', 'test-platform-service-identity-system.ps1'))
{
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name), [ref]$null, [ref]$errors)
    Assert-IdentityTest ($errors.Count -eq 0)
    $commands = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
    Assert-IdentityTest (-not ($commands | Where-Object { $_.GetCommandName() -in @('Remove-Item', 'Clear-Content', 'Set-Content') }))
}
$purpose = Get-KeireIdentityBridgeEntropy ('a' * 32) 'mysql' 'account-record-key.dpapi'
Assert-IdentityTest ($purpose -ceq ('Keire.Platform.IdentityBridge.v1:' + ('a' * 32) + ':mysql:account-record-key.dpapi'))
Assert-IdentityReject { Get-KeireIdentityBridgeEntropy 'invalid' 'mysql' 'account-record-key.dpapi' }
Assert-IdentityReject { Get-KeireIdentityBridgeEntropy ('a' * 32) 'mysql' 'other.dpapi' }
$receiver = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'complete-platform-service-identity.ps1') -Raw
Assert-IdentityTest ($receiver.Contains(".User.Value -cne 'S-1-5-18'"))
Assert-IdentityTest ($receiver.Contains('Test-KeireIdentityBytesEqual'))
$coordinator = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'migrate-platform-service-identity.ps1') -Raw
Assert-IdentityTest ($coordinator.Contains('Assert-KeireIdentityAdministrator'))
Assert-IdentityTest ($coordinator.Contains('[Security.Cryptography.DataProtectionScope]::LocalMachine'))
Assert-IdentityTest (-not $coordinator.Contains('Start-Process'))
@{ status = 'passed'; checks = $checks; actualMigrationExecuted = $false; scheduledTasksCreated = $false } | ConvertTo-Json -Compress
