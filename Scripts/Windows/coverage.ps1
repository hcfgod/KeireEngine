[CmdletBinding()]
param([string]$Architecture = "", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$coverageDirectory = Join-Path $Root "Build\Coverage\windows-$outputArchitecture"
New-Item -ItemType Directory -Force $coverageDirectory | Out-Null
Add-LLVMToPath | Out-Null
$env:LLVM_PROFILE_FILE = Join-Path $coverageDirectory "%p.profraw"

& (Join-Path $PSScriptRoot "build.ps1") -Generator ninja -Configuration Coverage -Architecture $Architecture -Toolset clang -Target $Project.TESTS_TARGET -CI:$CI -Update:$Update -Generate:$Generate
$tests = Join-Path $Root "Build\Bin\Coverage-windows-$outputArchitecture\$($Project.TESTS_TARGET)\$($Project.TESTS_TARGET).exe"
Push-Location $Root; try { & $tests; if ($LASTEXITCODE -ne 0) { throw "Coverage tests failed with exit code $LASTEXITCODE." } } finally { Pop-Location }
& (Join-Path $PSScriptRoot "build.ps1") -Generator ninja -Configuration Coverage -Architecture $Architecture -Toolset clang -Target $Project.CLIENT_TARGET -CI:$CI
$client = Join-Path $Root "Build\Bin\Coverage-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe"
Push-Location $Root; try { & $client; if ($LASTEXITCODE -ne 0) { throw "Coverage KeireClient failed with exit code $LASTEXITCODE." } } finally { Pop-Location }

$profiles = @(Get-ChildItem $coverageDirectory -Filter *.profraw | ForEach-Object FullName)
if (-not $profiles) { throw "No LLVM coverage profiles were produced." }
$profileData = Join-Path $coverageDirectory "coverage.profdata"
& llvm-profdata merge -sparse @profiles -o $profileData; if ($LASTEXITCODE -ne 0) { throw "llvm-profdata failed." }
$common = @("-instr-profile=$profileData", $tests, "-object=$client", "-ignore-filename-regex=Vendor|KeireTests")
& llvm-cov export -format=lcov @common | Set-Content (Join-Path $coverageDirectory "coverage.info") -Encoding UTF8
& llvm-cov show @common -format=html "-output-dir=$(Join-Path $coverageDirectory 'html')"; if ($LASTEXITCODE -ne 0) { throw "llvm-cov show failed." }
$summary = (& llvm-cov export -summary-only @common) -join "`n" | ConvertFrom-Json
$lineCoverage = [double]$summary.data[0].totals.lines.percent
& llvm-cov report @common
if ($lineCoverage -lt 80.0) { throw "Coverage is $lineCoverage%, below the required 80%." }
Write-Host "==> Coverage report: $(Join-Path $coverageDirectory 'html\index.html')"
