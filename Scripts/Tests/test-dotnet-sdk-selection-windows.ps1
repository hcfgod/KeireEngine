param([switch]$Live)
$ErrorActionPreference = 'Stop'
$Root = Resolve-Path (Join-Path $PSScriptRoot '../..')
. (Join-Path $Root 'Scripts/Windows/common.ps1')
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $Root 'Scripts/Windows/coral.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Coral script did not parse.' }
foreach ($name in @('Test-KeireCompatibleSdkVersion', 'Resolve-KeirePinnedDotnetSdk', 'Resolve-KeirePinnedNetHost')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Assert-True($condition, [string]$message) { if (-not $condition) { throw $message } }
Assert-True (Test-KeireCompatibleSdkVersion '10.0.401' '10.0.302') 'Newer stable SDK rejected.'
Assert-True (Test-KeireCompatibleSdkVersion '10.0.302' '10.0.302') 'Pinned SDK rejected.'
foreach ($version in @('10.0.301', '9.0.316', '11.0.100', '10.1.100', '10.0.500-preview.1', '', '../escape')) {
    Assert-True (-not (Test-KeireCompatibleSdkVersion $version '10.0.302')) "Incompatible SDK accepted: $version"
}
Assert-True (Test-KeireCompatibleSdkVersion '10.0.302' '10.0.302' 'locked') 'Locked version rejected.'
Assert-True (-not (Test-KeireCompatibleSdkVersion '10.0.401' '10.0.302' 'locked')) 'Locked mode upgraded the SDK.'
if (-not $Live) {
    Write-Host 'Compatible and locked SDK selection policy tests passed.'
    return
}
$lock = Get-DependencyLock
$automatic = Resolve-KeirePinnedDotnetSdk $lock.DOTNET_SDK_VERSION
Assert-True (Test-KeireCompatibleSdkVersion $automatic.Version $lock.DOTNET_SDK_VERSION) 'Invalid live SDK selection.'
$hostPack = Resolve-KeirePinnedNetHost $automatic.Root $automatic.Version (Get-NativeArchitecture)
Assert-True ($hostPack.Library.StartsWith($automatic.Root, [StringComparison]::OrdinalIgnoreCase)) 'Host pack escaped selected SDK.'
Write-Host "Compatible SDK policy and live canonical host-pack selection passed: $($automatic.Version)"
