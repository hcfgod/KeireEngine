$ErrorActionPreference = 'Stop'
$deployment = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Deployment'))
$source = Get-Content (Join-Path $deployment 'configure-windows-validator-firewall.ps1') -Raw
$body = $source.Substring($source.IndexOf('$programs ='), $source.IndexOf('if (-not [string]::IsNullOrWhiteSpace($Attestation))') - $source.IndexOf('$programs ='))
$script:rules = @{}
$script:mutations = 0
function Get-NetFirewallRule($DisplayName)
{
    if ($script:rules.ContainsKey($DisplayName))
    {
        $script:rules[$DisplayName]
    }
}

function Get-NetFirewallApplicationFilter
{
    process
    {
        [pscustomobject]@{ Program = $_.Program }
    }
}

function Set-NetFirewallRule
{
    process
    {
        $script:mutations++
        $_.Enabled = 'True'
        $_.Direction = 'Outbound'
        $_.Action = 'Block'
        $_.Profile = 'Any'
    }
}

function New-NetFirewallRule($DisplayName, $Direction, $Action, $Enabled, $Profile, $Program, $Group)
{
    $script:mutations++
    $script:rules[$DisplayName] = [pscustomobject]@{
        DisplayName = $DisplayName
        Direction = $Direction
        Action = $Action
        Enabled = $Enabled
        Profile = $Profile
        Program = $Program
        Group = $Group
    }
}
$Validator = $PSCommandPath
$AssetTool = $PSCommandPath
$MalwareScanner = $PSCommandPath
$Dotnet = $PSCommandPath
$FirewallRulePrefix = 'Keire QA fixture'
$FirewallRuleGroup = 'Keire QA fixture'
& ([scriptblock]::Create($body))
if ($script:mutations -ne 4 -or $script:rules.Count -ne 4)
{
    throw 'Isolated creation failed.'
}
& ([scriptblock]::Create($body))
if ($script:mutations -ne 8)
{
    throw 'Same-target repeat failed.'
}
$script:rules["$FirewallRulePrefix - Managed Compiler"].Program = 'C:\different.exe'
$script:mutations = 0
$failed = $false
try
{
    & ([scriptblock]::Create($body))
}
catch
{
    $failed = $true
}
if (-not $failed -or $script:mutations -ne 0)
{
    throw 'Late target collision mutated earlier rules.'
}
$script:rules["$FirewallRulePrefix - Managed Compiler"].Program = $PSCommandPath
$script:rules["$FirewallRulePrefix - Managed Compiler"].Group = 'Other worker'
$failed = $false
try
{
    & ([scriptblock]::Create($body))
}
catch
{
    $failed = $true
}
if (-not $failed -or $script:mutations -ne 0)
{
    throw 'Group collision mutated rules.'
}
$script:rules["$FirewallRulePrefix - Managed Compiler"] = @($script:rules["$FirewallRulePrefix - Managed Compiler"], $script:rules["$FirewallRulePrefix - Managed Compiler"])
$failed = $false
try
{
    & ([scriptblock]::Create($body))
}
catch
{
    $failed = $true
}
if (-not $failed -or $script:mutations -ne 0)
{
    throw 'Ambiguous collision mutated rules.'
}
foreach ($file in @(
        (Join-Path $deployment 'configure-windows-validator-firewall.ps1'),
        (Join-Path $deployment 'start-windows-marketplace-validator.ps1'),
        (Join-Path $deployment 'install-windows-marketplace-validator-tasks.ps1'),
        (Join-Path $PSScriptRoot 'RunOfflineWorker.ps1')))
{
    $tokens = $null
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors)
    if ($errors.Count)
    {
        throw 'Script syntax failed.'
    }
    foreach ($name in @('FirewallRulePrefix', 'FirewallRuleGroup'))
    {
        $parameter = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $name }
        if (-not $parameter -or -not ($parameter.Attributes | Where-Object { $_.TypeName.Name -eq 'ValidatePattern' }))
        {
            throw 'Missing namespace validation.'
        }
    }
}
$installer = Get-Content (Join-Path $deployment 'install-windows-marketplace-validator-tasks.ps1') -Raw
if ($installer -notmatch '"-FirewallRulePrefix"' -or $installer -notmatch '"-FirewallRuleGroup"')
{
    throw 'The durable worker task does not retain its isolated firewall namespace.'
}
Write-Output 'Passed 9 firewall isolation checks; all firewall operations mocked.'
