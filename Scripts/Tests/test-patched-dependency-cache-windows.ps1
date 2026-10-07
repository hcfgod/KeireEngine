[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Windows\common.ps1')

function Assert-Fixture([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Invoke-FixtureGit {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Fixture git failed: $args" }
}
function Assert-Rejected([scriptblock]$Action, [string]$Pattern) {
    try { & $Action | Out-Null }
    catch {
        if ($_.Exception.Message -match $Pattern) { return }
        throw "Expected '$Pattern', received: $($_.Exception.Message)"
    }
    throw "Expected rejection matching '$Pattern'."
}

# Load only the actual functions, not dependencies.ps1's network/build entrypoint.
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot '..\Windows\dependencies.ps1'), [ref]$tokens, [ref]$parseErrors)
Assert-Fixture ($parseErrors.Count -eq 0) 'Dependency script must parse.'
foreach ($name in @('Get-DependencyPatchDigest', 'Assert-PatchedDependencySource', 'Get-PatchedDependencySource')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    Assert-Fixture ($null -ne $definition) "Missing function $name."
    . ([scriptblock]::Create($definition.Extent.Text))
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-patched-cache-' + [guid]::NewGuid().ToString('N'))
$previousLocal = $env:LOCALAPPDATA
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $Root = Join-Path $fixture 'workspace'
    $source = Join-Path $fixture 'source'
    New-Item -ItemType Directory -Path $Root, $source | Out-Null
    Invoke-FixtureGit init --quiet $source
    Invoke-FixtureGit -C $source config core.autocrlf false
    [IO.File]::WriteAllText((Join-Path $source 'value.txt'), "before`n")
    Invoke-FixtureGit -C $source add value.txt
    Invoke-FixtureGit -C $source -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m baseline
    $commit = (Invoke-FixtureGit -C $source rev-parse HEAD).Trim()
    [IO.File]::WriteAllText((Join-Path $source 'value.txt'), "after`n")
    $patchPath = Join-Path $fixture 'change.patch'
    $patch = @(Invoke-FixtureGit -C $source diff --full-index --no-ext-diff -- value.txt)
    [IO.File]::WriteAllText($patchPath, ($patch -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Invoke-FixtureGit -C $source checkout -- value.txt
    $patches = @((Get-Item -LiteralPath $patchPath))
    $digest = Get-DependencyPatchDigest $patches

    foreach ($name in @('Assimp', 'SDL')) {
        $env:LOCALAPPDATA = Join-Path $fixture $name
        $base = Join-Path $env:LOCALAPPDATA 'KeireDependencySources'
        $prefix = if ($name -eq 'Assimp') { 'ap' } else { 'sp' }
        $cache = Get-KeireWorkspaceJunctionPath -BasePath $base -RepositoryRoot $Root `
            -Prefix "$prefix-$($commit.Substring(0,8))-$($digest.Substring(0,8))"
        New-Item -ItemType Directory -Path $cache -Force | Out-Null
        $result = Get-PatchedDependencySource $source $commit $digest $patches -Name $name
        Assert-Fixture ($result -eq $cache) 'Recovery must publish to the original cache path.'
        Assert-Fixture (([IO.File]::ReadAllText((Join-Path $cache 'value.txt'))).Trim() -eq 'after') 'Patch content missing.'
        $retained = @(Get-ChildItem -LiteralPath $base -Directory -Force | Where-Object Name -Like '*.empty-*')
        Assert-Fixture ($retained.Count -eq 1) 'Exactly one empty cache must be retained.'
        Assert-Fixture (@(Get-ChildItem -LiteralPath $retained[0].FullName -Force).Count -eq 0) 'Retained cache changed.'
        $stamp = Join-Path $cache "keire-$($name.ToLowerInvariant())-patch.stamp"
        $stampTime = (Get-Item -LiteralPath $stamp).LastWriteTimeUtc
        $result = Get-PatchedDependencySource $source $commit $digest $patches -Name $name
        Assert-Fixture ($result -eq $cache -and (Get-Item -LiteralPath $stamp).LastWriteTimeUtc -eq $stampTime) 'Reuse rebuilt valid cache.'
        Assert-Fixture (@(Get-ChildItem -LiteralPath $base -Directory -Force | Where-Object Name -Like '*.empty-*').Count -eq 1) 'Reuse retained another cache.'
        Write-Host "PASS: $name empty recovery, verified full-index patch and unchanged reuse."
    }

    $env:LOCALAPPDATA = Join-Path $fixture 'invalid'
    $base = Join-Path $env:LOCALAPPDATA 'KeireDependencySources'
    $cache = Get-KeireWorkspaceJunctionPath -BasePath $base -RepositoryRoot $Root `
        -Prefix "ap-$($commit.Substring(0,8))-$($digest.Substring(0,8))"
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $sentinel = Join-Path $cache 'keep.txt'
    [IO.File]::WriteAllText($sentinel, 'preserve me')
    Assert-Rejected { Get-PatchedDependencySource $source $commit $digest $patches } 'has no Git metadata.*Preserve or move'
    Assert-Fixture ([IO.File]::ReadAllText($sentinel) -eq 'preserve me') 'Nonempty invalid cache was modified.'
    Assert-Fixture (@(Get-ChildItem -LiteralPath $base -Force).Count -eq 1) 'Invalid cache created unrelated replacement/lock.'
    Write-Host 'PASS: nonempty invalid cache preserved with actionable Git metadata diagnostic.'

    $env:LOCALAPPDATA = Join-Path $fixture 'reparse'
    $base = Join-Path $env:LOCALAPPDATA 'KeireDependencySources'
    New-Item -ItemType Directory -Path $base -Force | Out-Null
    $cache = Get-KeireWorkspaceJunctionPath -BasePath $base -RepositoryRoot $Root `
        -Prefix "ap-$($commit.Substring(0,8))-$($digest.Substring(0,8))"
    $outside = Join-Path $fixture 'outside-cache'
    New-Item -ItemType Directory -Path $outside | Out-Null
    New-Item -ItemType Junction -Path $cache -Target $outside | Out-Null
    Assert-Rejected { Get-PatchedDependencySource $source $commit $digest $patches } 'missing or unsafe'
    Assert-Fixture ((Get-Item -LiteralPath $cache -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) 'Junction was replaced.'
    Assert-Fixture (@(Get-ChildItem -LiteralPath $outside -Force).Count -eq 0) 'Junction target was modified.'
    Assert-Rejected { Get-KeireWorkspaceJunctionPath -BasePath $base -RepositoryRoot $Root -Prefix '../escape' } 'safe path component'
    Write-Host 'PASS: empty reparse cache rejects without moving or writing target; traversal prefix rejects.'
    Write-Host "PASS: patched cache regression on PowerShell $($PSVersionTable.PSVersion). Evidence retained: $fixture"
}
finally { $env:LOCALAPPDATA = $previousLocal }
