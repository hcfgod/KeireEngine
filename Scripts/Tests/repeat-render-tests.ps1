[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [ValidateSet("direct3d12", "vulkan")]
    [string]$Backend = "direct3d12",
    [ValidateRange(1, 100)]
    [int]$Count = 25,
    [switch]$GpuValidation
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executable = Join-Path $root "Build\Bin\$Configuration-windows-x86_64\KeireRenderTests\KeireRenderTests.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "KeireRenderTests is not built for $Configuration."
}

$logRoot = Join-Path $root "Build\TestLogs\RenderRepeat\$Configuration-$Backend"
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$env:KEIRE_GPU_TEST_BACKEND = $Backend
$env:KEIRE_REQUIRE_GPU_TESTS = "1"
if ($GpuValidation) {
    $env:KEIRE_GPU_VALIDATION = "1"
} else {
    Remove-Item Env:KEIRE_GPU_VALIDATION -ErrorAction SilentlyContinue
}

for ($iteration = 1; $iteration -le $Count; $iteration++) {
    $log = Join-Path $logRoot ("run-{0:D3}.log" -f $iteration)
    Write-Host "==> Render repeat $iteration/$Count ($Configuration, $Backend)"
    & $executable --no-skip 2>&1 | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) {
        throw "Rendered-output run $iteration failed with exit code $LASTEXITCODE. Log: $log"
    }
}

Write-Host "==> $Count consecutive rendered-output runs passed."
