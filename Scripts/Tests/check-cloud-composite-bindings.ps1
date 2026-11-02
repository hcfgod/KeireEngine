param(
    [Parameter(Mandatory = $true)][string] $RepositoryRoot,
    [Parameter(Mandatory = $true)][string] $OutputDirectory,
    [string] $Dxc = '',
    [switch] $AllowUnverifiedDxil
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$helper = Join-Path $root 'Scripts/Windows/shader-blob-cache.ps1'
. $helper
$source = Join-Path $root 'KeireCore/Shaders/BuiltinCloudComposite.hlsl'
$include = Join-Path $root 'KeireCore/Shaders'
$compiler = Join-Path $root 'Build/Tools/ShaderCompiler/KeireShaderCompiler.exe'
$header = Join-Path $root 'Build/Generated/Keire/BuiltinUnlitShaders.h'
$compilerInputs = @(Get-ShaderCompilerInputs $compiler)
$compilerIdentity = Get-GeneratedContentFingerprint -Schema ('shader-compiler-v1|' + ($compilerInputs -join '|')) -Inputs $compilerInputs
$identity = Get-ShaderBlobInputIdentity $source @($include) $compilerIdentity
$inputs = @($compilerInputs) + @(Get-ShaderIncludeClosure @($source) @($include)) + @(
    $helper, (Join-Path $root 'Scripts/Windows/generated-content-cache.ps1'))
$inputHashes = @($inputs | Select-Object -Unique | ForEach-Object {
    @{ path = $_; sha256 = (Get-GeneratedContentFileHash $_) }
})
$artifacts = @()
foreach ($format in @('DXIL', 'SPIRV', 'MSL')) {
    # Deliberately mirror Invoke-CachedShaderCompile's key without invoking it or any compiler.
    $arguments = @([IO.Path]::GetFullPath($source), '-s', 'HLSL', '-d', $format, '-t', 'fragment', '-e', 'PSMain',
        '-I', [IO.Path]::GetFullPath($include))
    $payload = ConvertTo-Json -Compress -Depth 4 @('shader-blob-v2-ascii-stage', $identity, $arguments)
    $key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($payload))).ToLowerInvariant()
    $blob = Join-Path $root "Build/Cache/ShaderBlobs/windows/$key.blob"
    $stamp = "$($blob.Substring(0, $blob.Length - 5)).sha256"
    if (-not (Test-Path -LiteralPath $blob) -or -not (Test-Path -LiteralPath $stamp)) {
        throw "No current-source cached $format artifact. Build first; this checker never compiles: $blob"
    }
    $hash = Get-GeneratedContentFileHash $blob
    if ($hash -cne [IO.File]::ReadAllText($stamp).Trim()) {
        throw "Cached artifact hash mismatch: $blob"
    }
    $artifacts += @{ format = $format; path = $blob; sha256 = $hash; cacheKey = $key }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($output) | Out-Null
$manifest = @{
    schema = 1
    source = $source
    compilerIdentity = $compilerIdentity
    inputIdentity = $identity
    header = @{ path = $header; sha256 = (Get-GeneratedContentFileHash $header) }
    inputs = $inputHashes
    artifacts = $artifacts
}
$manifestPath = Join-Path $output 'source-artifacts.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$checkArguments = @((Join-Path $PSScriptRoot 'check-cloud-composite-bindings.py'), '--manifest', $manifestPath,
    '--report', (Join-Path $output 'binding-report.json'))
if ($Dxc) { $checkArguments += @('--dxc', $Dxc) }
if ($AllowUnverifiedDxil) { $checkArguments += '--allow-unverified-dxil' }
& python @checkArguments
exit $LASTEXITCODE
