[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$benchmarkScript = Join-Path $repositoryRoot "Scripts\Windows\render-benchmark.ps1"
$fixtureFunctionName = "function Get-MaterialShaderFixture"
$benchmarkSource = Get-Content -LiteralPath $benchmarkScript -Raw
$functionStart = $benchmarkSource.IndexOf($fixtureFunctionName, [StringComparison]::Ordinal)
$functionEnd = $benchmarkSource.IndexOf("New-Item -ItemType Directory", $functionStart, [StringComparison]::Ordinal)
if ($functionStart -lt 0 -or $functionEnd -lt 0) {
    throw "Render benchmark fixture function could not be isolated."
}

Invoke-Expression $benchmarkSource.Substring($functionStart, $functionEnd - $functionStart)

$fixtureRoot = Join-Path $repositoryRoot "Samples\KeireSandbox"
$expectedRoots = @(
    "Assets/Examples/MaterialLab",
    "Assets/Scenes/SandboxShowcase.keirescene",
    "Assets/Scenes/SandboxShowcase.keirescene.keiremeta",
    "Assets/Textures",
    "Assets/Vfx",
    "ProjectSettings/BuildScenes.keiresettings"
)
$first = Get-MaterialShaderFixture -Root $fixtureRoot
$second = Get-MaterialShaderFixture -Root $fixtureRoot

$fixtureRootsMatch = ($first.roots -join "`0") -ceq ($expectedRoots -join "`0")
if ($first.schemaVersion -ne 1 -or -not $fixtureRootsMatch) {
    throw "Material/shader fixture contract has unexpected schema or roots."
}
if ($first.files.Count -eq 0 -or $first.identitySha256 -notmatch "^[0-9a-f]{64}$") {
    throw "Material/shader fixture did not produce a non-empty SHA-256 manifest."
}
if ($first.identitySha256 -cne $second.identitySha256 -or
    ($first.files | ConvertTo-Json -Depth 4 -Compress) -cne ($second.files | ConvertTo-Json -Depth 4 -Compress)) {
    throw "Material/shader fixture manifest is not deterministic."
}

$paths = @($first.files | ForEach-Object { $_.path })
$sortedPaths = [Collections.Generic.List[string]]::new()
foreach ($path in $paths) {
    $sortedPaths.Add($path)
}
$pathComparison = [Comparison[string]]{
    param($left, $right)
    return [StringComparer]::Ordinal.Compare($left, $right)
}
$sortedPaths.Sort($pathComparison)
if (($paths -join "`0") -cne ($sortedPaths -join "`0")) {
    throw "Material/shader fixture manifest paths are not sorted."
}
$uniquePathCount = @($paths | Select-Object -Unique).Count
$invalidEntries = @($first.files | Where-Object {
        $_.sizeBytes -lt 0 -or $_.sha256 -notmatch "^[0-9a-f]{64}$"
    })
if ($paths.Count -ne $uniquePathCount -or $invalidEntries.Count -ne 0) {
    throw "Material/shader fixture manifest has duplicate or invalid entries."
}

try {
    Get-MaterialShaderFixture -Root (Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString("N"))) |
        Out-Null
    throw "Material/shader fixture accepted missing roots."
}
catch {
    if ($_.Exception.Message -notlike "Material/shader benchmark fixture is missing Assets/Examples/MaterialLab.") {
        throw
    }
}

$mutationRoot = Join-Path ([IO.Path]::GetTempPath()) ("keire-material-shader-fixture-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force $mutationRoot | Out-Null
    foreach ($relativeRoot in $expectedRoots) {
        $source = Join-Path $fixtureRoot $relativeRoot
        $destination = Join-Path $mutationRoot $relativeRoot
        New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Recurse
    }
    $copied = Get-MaterialShaderFixture -Root $mutationRoot
    if ($copied.identitySha256 -cne $first.identitySha256) {
        throw "Material/shader fixture changed while copied without mutation."
    }
    [IO.File]::AppendAllText((Join-Path $mutationRoot "Assets\Examples\MaterialLab\README.md"),
                             "`nFixture hash test mutation.`n", [Text.UTF8Encoding]::new($false))
    $mutated = Get-MaterialShaderFixture -Root $mutationRoot
    if ($mutated.identitySha256 -ceq $copied.identitySha256) {
        throw "Material/shader fixture hash did not change after source mutation."
    }
}
finally {
    if (Test-Path -LiteralPath $mutationRoot) {
        Remove-Item -LiteralPath $mutationRoot -Recurse -Force
    }
}

Write-Host "Material/shader benchmark fixture test passed ($($first.files.Count) files, $($first.identitySha256))."
