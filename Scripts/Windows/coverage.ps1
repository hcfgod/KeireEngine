[CmdletBinding()]
param([string]$Architecture = "", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$coverageDirectory = Join-Path $Root "Build\Coverage\windows-$outputArchitecture"
New-Item -ItemType Directory -Force $coverageDirectory | Out-Null
Get-ChildItem -LiteralPath $coverageDirectory -Filter "*.profraw" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
Add-LLVMToPath | Out-Null
$env:LLVM_PROFILE_FILE = Join-Path $coverageDirectory "%p.profraw"

$targets = @(
    $Project.TESTS_TARGET,
    "$($Project.PROJECT_NAMESPACE)EditorTests",
    "$($Project.PROJECT_NAMESPACE)HubTests",
    $Project.CLIENT_TARGET
)
$executables = @()
foreach ($target in $targets) {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator ninja -Configuration Coverage `
        -Architecture $Architecture -Toolset clang -Target $target -CI:$CI -Update:$Update -Generate:$Generate
    $executable = Join-Path $Root "Build\Bin\Coverage-windows-$outputArchitecture\$target\$target.exe"
    if (-not (Test-Path -LiteralPath $executable)) { throw "Coverage executable was not found: $executable" }
    $executables += $executable
    Push-Location $Root
    try {
        $runArguments = if ($target -eq $Project.CLIENT_TARGET) {
            @("--project", (Join-Path $Root "Samples\KeireSandbox"), "--smoke-project")
        }
        else { @() }
        & $executable @runArguments
        if ($LASTEXITCODE -ne 0) { throw "Coverage target '$target' failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }
}

$profiles = @(Get-ChildItem $coverageDirectory -Filter *.profraw | ForEach-Object FullName)
if (-not $profiles) { throw "No LLVM coverage profiles were produced." }
$profileData = Join-Path $coverageDirectory "coverage.profdata"
& llvm-profdata merge -sparse @profiles -o $profileData; if ($LASTEXITCODE -ne 0) { throw "llvm-profdata failed." }
$common = @("-instr-profile=$profileData", $executables[0])
foreach ($executable in $executables | Select-Object -Skip 1) { $common += "-object=$executable" }
$common += "-ignore-filename-regex=Vendor|KeireTests|KeireEditorTests|KeireHubTests|KeireRenderTests"
$core = @(
    "-instr-profile=$profileData",
    $executables[0],
    "-ignore-filename-regex=Vendor|KeireTests|KeireEditorTests|KeireHubTests|KeireRenderTests"
)
& llvm-cov export -format=lcov @common | Set-Content (Join-Path $coverageDirectory "coverage.info") -Encoding UTF8
& llvm-cov show @common -format=html "-output-dir=$(Join-Path $coverageDirectory 'html')"; if ($LASTEXITCODE -ne 0) { throw "llvm-cov show failed." }
$aggregateSummary = (& llvm-cov export -summary-only @common) -join "`n" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw "llvm-cov aggregate summary failed." }
$coreSummary = (& llvm-cov export -summary-only @core) -join "`n" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw "llvm-cov core summary failed." }
$aggregateLineCoverage = [double]$aggregateSummary.data[0].totals.lines.percent
$coreLineCoverage = [double]$coreSummary.data[0].totals.lines.percent
& llvm-cov report @common
$minimumCoreLineCoverage = 74.5
$minimumAggregateLineCoverage = 63.0
Write-Host ("==> Line coverage: core {0:N2}% (minimum {1:N1}%), aggregate {2:N2}% (minimum {3:N1}%)" -f `
        $coreLineCoverage, $minimumCoreLineCoverage, $aggregateLineCoverage, $minimumAggregateLineCoverage)
if ($coreLineCoverage -lt $minimumCoreLineCoverage) {
    throw "Core line coverage is $coreLineCoverage%, below the required $minimumCoreLineCoverage%."
}
if ($aggregateLineCoverage -lt $minimumAggregateLineCoverage) {
    throw "Aggregate line coverage is $aggregateLineCoverage%, below the required $minimumAggregateLineCoverage%."
}
Write-Host "==> Coverage report: $(Join-Path $coverageDirectory 'html\index.html')"
