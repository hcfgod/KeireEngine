$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot 'RunMarketplacePipeline.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count)
{
    throw 'Orchestration syntax failed.'
}
foreach ($function in $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $true))
{
    . ([scriptblock]::Create($function.Extent.Text))
}
$checks = 1
function Assert-Rejected([scriptblock] $Action)
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
    if (-not $rejected)
    {
        throw 'Expected validation rejection.'
    }
}
$now = [DateTime]::new(2026, 10, 23, 12, 0, 0)
foreach ($age in @(0, 7, -1))
{
    $date = $now.AddDays(-$age).ToString('ddd MMM d HH:mm:ss yyyy', [Globalization.CultureInfo]::InvariantCulture)
    $actual = Assert-ScannerFreshness "ClamAV fixture/1/$date" 7 $now
    if ($actual -ne $age)
    {
        throw 'Freshness boundary mismatch.'
    }
    $checks++
}
foreach ($age in @(7.01, -1.01))
{
    $date = $now.AddDays(-$age).ToString('ddd MMM d HH:mm:ss yyyy', [Globalization.CultureInfo]::InvariantCulture)
    Assert-Rejected { Assert-ScannerFreshness "ClamAV fixture/1/$date" 7 $now }
    $checks++
}
foreach ($version in @('', 'not a version', 'ClamAV/1/not a date'))
{
    Assert-Rejected { Assert-ScannerFreshness $version 7 $now }
    $checks++
}
Assert-IntegrationArguments @('--settings', 'C:\fixture.json')
$checks++
foreach ($argument in @('--worker-config', '--worker-config=bad', '--result', '--result=bad', '--RESULT=bad'))
{
    Assert-Rejected { Assert-IntegrationArguments @($argument) }
    $checks++
}
$executable = (Get-Process -Id $PID).Path
$owned = $null
try
{
    $owned = Start-OwnedProcess $executable @('-NoProfile', '-NonInteractive', '-Command', 'Write-Output fixture')
    if (-not $owned.Process.WaitForExit(10000) -or $owned.Output.Result.Trim() -ne 'fixture')
    {
        throw 'Owned child output capture failed.'
    }
}
finally
{
    Stop-OwnedProcess $owned
}
$checks++
$owned = Start-OwnedProcess $executable @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 60')
Stop-OwnedProcess $owned
$checks++
Stop-OwnedProcess $null
$checks++
$owned = $null
$syntheticSettings = [Text.Encoding]::UTF8.GetBytes('{"synthetic":true}')
try
{
    $owned = Start-OwnedProcess $executable @('-NoProfile', '-NonInteractive', '-Command',
        '$inputText = [Console]::In.ReadToEnd(); Write-Output $inputText.Length') -PrivateInput
    Write-PrivateSettings $owned $syntheticSettings
    if (-not $owned.Process.WaitForExit(10000) -or
        [int]$owned.Output.Result.Trim() -ne $syntheticSettings.Length)
    {
        throw 'Private settings pipe delivery failed.'
    }
}
finally
{
    [Array]::Clear($syntheticSettings, 0, $syntheticSettings.Length)
    Stop-OwnedProcess $owned
}
$checks++
Assert-Rejected { Write-PrivateSettings $null ([byte[]]::new(1)) }
$checks++
Assert-Rejected { Write-PrivateSettings $null ([byte[]]::new(65537)) }
$checks++
Write-Output "Passed $checks pipeline orchestration checks; only harmless owned child processes executed."
