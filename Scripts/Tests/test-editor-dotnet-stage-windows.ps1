$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'Scripts/Windows/package-editor.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Editor packager did not parse.' }
$selector = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-WindowsEditorBundledSdk'
}, $true)
if (-not $selector) { throw 'Editor SDK selection function is missing.' }
. ([scriptblock]::Create($selector.Extent.Text))
function Assert-Fails([scriptblock]$operation) {
    try { & $operation | Out-Null } catch { return }
    throw 'Invalid SDK source was accepted.'
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-editor-sdk-' + [guid]::NewGuid().ToString('N'))
$pinned = Join-Path $fixture 'Build/Tools/dotnet/10.0.302'
$selected = Join-Path $fixture 'Build/Dependencies/dotnet-sdk'
try {
    New-Item -ItemType Directory -Force (Join-Path $pinned 'sdk/10.0.302'),
        (Join-Path $selected 'packs/unrelated-mobile-workload') | Out-Null
    Set-Content -LiteralPath (Join-Path $pinned 'dotnet.exe') -Value 'fixture'
    Set-Content -LiteralPath (Join-Path $selected 'packs/unrelated-mobile-workload/keep.txt') -Value 'system workload'
    $actual = Get-WindowsEditorBundledSdk $fixture '10.0.302'
    if ([IO.Path]::GetFullPath($actual) -ne [IO.Path]::GetFullPath($pinned)) {
        throw 'Packaging selected a system SDK instead of its isolated locked SDK.'
    }
    $staged = Join-Path $fixture 'staged'
    New-Item -ItemType Directory $staged | Out-Null
    Get-ChildItem -LiteralPath $actual -Force | Copy-Item -Destination $staged -Recurse -Force
    if (Test-Path -LiteralPath (Join-Path $staged 'packs/unrelated-mobile-workload')) {
        throw 'Unrelated machine workloads leaked into the Editor SDK payload.'
    }
    Assert-Fails { Get-WindowsEditorBundledSdk $fixture '../escape' }
    Assert-Fails { Get-WindowsEditorBundledSdk $fixture '10.0.999' }
    Move-Item -LiteralPath (Join-Path $pinned 'dotnet.exe') -Destination (Join-Path $pinned 'saved.exe')
    Assert-Fails { Get-WindowsEditorBundledSdk $fixture '10.0.302' }
    Move-Item -LiteralPath (Join-Path $pinned 'saved.exe') -Destination (Join-Path $pinned 'dotnet.exe')
    $saved = "$pinned-saved"
    Move-Item -LiteralPath $pinned -Destination $saved
    New-Item -ItemType Junction -Path $pinned -Target $selected | Out-Null
    try { Assert-Fails { Get-WindowsEditorBundledSdk $fixture '10.0.302' } }
    finally { [IO.Directory]::Delete($pinned) }
    if ((Get-Content -LiteralPath (Join-Path $selected 'packs/unrelated-mobile-workload/keep.txt')) -ne 'system workload') {
        throw 'The machine SDK source was modified.'
    }
    Write-Host 'Editor SDK isolation, staging, invalid-source rejection and source preservation passed.'
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ([IO.Path]::GetDirectoryName($resolved) -eq [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') -and
        [IO.Path]::GetFileName($resolved).StartsWith('keire-editor-sdk-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
