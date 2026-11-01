[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'Scripts/Windows/shader-blob-cache.ps1')
$testRoot = Join-Path $repo ('Build/Validation/ShaderBlobCache-Unicode-é-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$compiler = Join-Path $testRoot 'fake-compiler.ps1'
@'
$arguments = @($args)
if (($arguments -join '|') -match '[^\x00-\x7F]')
{
    throw 'Native compiler received a non-ASCII source/include/output argument.'
}
$stagedClosure = @(Get-ShaderIncludeClosure @($arguments[0]) @((Split-Path -Parent $arguments[0])))
if ($stagedClosure.Count -lt 1)
{
    throw 'Staged source closure is missing.'
}
$countPath = Join-Path $PSScriptRoot 'count.txt'
$count = if (Test-Path -LiteralPath $countPath) { [int][IO.File]::ReadAllText($countPath) } else { 0 }
[IO.File]::WriteAllText($countPath, [string]($count + 1))
$output = $arguments[[Array]::IndexOf($arguments, '-o') + 1]
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail'))
{
    [IO.File]::WriteAllText($output, 'partial output')
    exit 7
}
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'empty')) { exit 0 }
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'mutate'))
{
    Add-Content -LiteralPath (Join-Path $PSScriptRoot 'nested.hlsli') 'float midCompileChange = 3;'
}
[IO.File]::WriteAllText($output, ($arguments[0..($arguments.Count - 3)] -join '|'))
exit 0
'@ | Set-Content -LiteralPath $compiler
$source = Join-Path $testRoot 'cloud.hlsl'
$independent = Join-Path $testRoot 'visibility.hlsl'
$include = Join-Path $testRoot 'cloud.hlsli'
$nested = Join-Path $testRoot 'nested.hlsli'
Set-Content -LiteralPath $source '#include "cloud.hlsli"'
Set-Content -LiteralPath $include '#include "nested.hlsli"'
Set-Content -LiteralPath $nested 'float density = 1;'
Set-Content -LiteralPath $independent 'void Main() {}'
Set-Content -LiteralPath (Join-Path $testRoot 'compiler-dependency.dll') 'identity-one'
$cache = Join-Path $testRoot 'cache'
$output = Join-Path $testRoot 'output.bin'
function Compile-TestShader
{
    param([string] $Path = $source, [string] $Entry = 'Main', [string[]] $Flags = @(),
        [string] $Stage = 'fragment', [string] $Destination = 'DXIL')
    Invoke-CachedShaderCompile -Compiler $compiler -Source $Path -Stage $Stage -Entry $Entry -Destination $Destination `
        -IncludeDirectories @($testRoot) -Output $output -CacheDirectory $cache -ExtraArguments $Flags
}
function Require-Count
{
    param([int] $Expected)
    $actual = [int][IO.File]::ReadAllText((Join-Path $testRoot 'count.txt'))
    if ($actual -ne $Expected) { throw "Expected $Expected compiles; saw $actual" }
}
Compile-TestShader
Compile-TestShader
Require-Count 1
Compile-TestShader -Path $independent
Require-Count 2
Add-Content -LiteralPath $nested 'float changed = 2;'
Compile-TestShader -Path $independent
Require-Count 2
Compile-TestShader
Require-Count 3
Compile-TestShader -Flags @('-D', 'QUALITY=1')
Compile-TestShader -Flags @('-D', 'QUALITY=1')
Require-Count 4
Compile-TestShader -Entry Other
Require-Count 5
Add-Content -LiteralPath $compiler '# compiler identity changed'
Compile-TestShader
Require-Count 6
Add-Content -LiteralPath (Join-Path $testRoot 'compiler-dependency.dll') 'identity-two'
Compile-TestShader
Require-Count 7
Get-ChildItem -LiteralPath $cache -Filter '*.sha256' | Remove-Item -Force
Compile-TestShader
Require-Count 8
Get-ChildItem -LiteralPath $cache -Filter '*.blob' | ForEach-Object { Set-Content -LiteralPath $_.FullName 'corrupt' }
Compile-TestShader
Require-Count 9
foreach ($mode in @('fail', 'empty'))
{
    $marker = Join-Path $testRoot $mode
    Set-Content -LiteralPath $marker ''
    $before = @(Get-ChildItem -LiteralPath $cache -Filter '*.sha256').Count
    $caught = $false
    try { Compile-TestShader -Entry $mode } catch { $caught = $true }
    if (-not $caught) { throw "Compiler $mode outcome was accepted" }
    if (@(Get-ChildItem -LiteralPath $cache -Filter '*.sha256').Count -ne $before) { throw 'Failure published a success stamp' }
    Remove-Item -LiteralPath $marker
    Compile-TestShader -Entry $mode
}
Require-Count 13
Compile-TestShader -Destination SPIRV
Compile-TestShader -Stage vertex
Require-Count 15
$mutationMarker = Join-Path $testRoot 'mutate'
Set-Content -LiteralPath $mutationMarker ''
$before = @(Get-ChildItem -LiteralPath $cache -Filter '*.sha256').Count
$caught = $false
try { Compile-TestShader -Entry Mutated } catch { $caught = $true }
if (-not $caught) { throw 'Concurrent source mutation was accepted' }
if (@(Get-ChildItem -LiteralPath $cache -Filter '*.sha256').Count -ne $before) { throw 'Mutation published a success stamp' }
Remove-Item -LiteralPath $mutationMarker
Compile-TestShader -Entry Mutated
Require-Count 17
Set-Content -LiteralPath $source '#include "missing.hlsli"'
$caught = $false
try { Compile-TestShader } catch { $caught = $true }
if (-not $caught) { throw 'Unresolved include was accepted' }
Require-Count 17
Write-Output "PASS: no-op, independent visibility, recursive includes, flags, entry, compiler/DLL identity, corruption, incomplete cache, failed/empty output. Evidence: $testRoot"
