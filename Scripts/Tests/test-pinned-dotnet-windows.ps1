$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'Scripts/Windows/dotnet.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'SDK installer did not parse.' }
$installer = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Install-PinnedDotnetSdk'
}, $true)
. ([scriptblock]::Create($installer.Extent.Text))

function Test-PinnedDotnetSdk([string]$Directory, [string]$Version) {
    $marker = Join-Path $Directory 'version.txt'
    return (Test-Path -LiteralPath $marker) -and (Get-Content -LiteralPath $marker -Raw).Trim() -eq $Version
}
function Invoke-WebRequest([string]$Uri, [string]$OutFile) {
    $script:downloads++
    Copy-Item -LiteralPath $script:archive -Destination $OutFile
}
function Assert-True($condition, [string]$message) { if (-not $condition) { throw $message } }
function Assert-Fails([scriptblock]$operation, [string]$message) {
    try { & $operation } catch { return }
    throw $message
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-dotnet-test-' + [guid]::NewGuid().ToString('N'))
try {
    $source = Join-Path $fixture 'source'
    New-Item -ItemType Directory -Path $source -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $source 'version.txt') -Value '10.0.302'
    $script:archive = Join-Path $fixture 'sdk.zip'
    Compress-Archive -Path (Join-Path $source '*') -DestinationPath $script:archive
    $hash = (Get-FileHash -LiteralPath $script:archive -Algorithm SHA512).Hash
    $destination = Join-Path $fixture 'installed'
    $script:downloads = 0
    Install-PinnedDotnetSdk $destination '10.0.302' 'fixture' $hash
    Assert-True (Test-PinnedDotnetSdk $destination '10.0.302') 'Verified SDK was not published.'
    Install-PinnedDotnetSdk $destination '10.0.302' 'fixture' $hash
    Assert-True ($script:downloads -eq 1) 'A valid SDK was downloaded again.'
    $bad = Join-Path $fixture 'bad'
    Assert-Fails { Install-PinnedDotnetSdk $bad '10.0.302' 'fixture' ('0' * 128) } 'Corrupt archive accepted.'
    Assert-True (-not (Test-Path -LiteralPath $bad)) 'Corrupt archive was published.'
    Assert-Fails { Install-PinnedDotnetSdk $bad '10.0.999' 'fixture' $hash } 'Wrong SDK version accepted.'
    Assert-True (-not (Test-Path -LiteralPath $bad)) 'Wrong SDK was published.'
    New-Item -ItemType Directory -Path $bad | Out-Null
    Set-Content -LiteralPath (Join-Path $bad 'keep.txt') -Value 'keep'
    Assert-Fails { Install-PinnedDotnetSdk $bad '10.0.302' 'fixture' $hash } 'Incomplete existing SDK was overwritten.'
    Assert-True ((Get-Content -LiteralPath (Join-Path $bad 'keep.txt')) -eq 'keep') 'Existing files changed.'
    Assert-Fails { Install-PinnedDotnetSdk $bad '../escape' 'fixture' $hash } 'Unsafe version accepted.'
    Assert-True (@(Get-ChildItem -LiteralPath $fixture -Directory -Filter '.sdk-*').Count -eq 0) 'Temporary SDK files leaked.'
    Write-Host 'Pinned SDK download, verification, reuse, failure cleanup and preservation tests passed.'
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ([IO.Path]::GetDirectoryName($resolved) -eq [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') -and
        [IO.Path]::GetFileName($resolved).StartsWith('keire-dotnet-test-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
