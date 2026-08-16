[CmdletBinding()]
param(
    [switch]$SkipSanitizers,
    [switch]$IncludePackage,
    [switch]$IncludeGraphicsSmokes,
    [string]$Architecture = "x86_64",
    [string]$PerformanceSnapshot = "",
    [string]$PerformanceHistory = "",
    [string]$PerformanceMetadata = "",
    [string]$PerformanceProfile = "sandbox-vfx-reference"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$project = Join-Path $root "Scripts/project.ps1"
. (Join-Path $PSScriptRoot "common.ps1")
$python = Get-PythonInvocation
$pythonPrefix = @($python.PrefixArguments)

function Invoke-Checked
{
    param([string]$Label, [scriptblock]$Command)

    Write-Host "==> $Label"
    & $Command
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
}

Invoke-Checked "VFX manifest and generated capability validation" {
    & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Vfx/validate_vfx_parity_manifest.py")
    if ($LASTEXITCODE -eq 0) {
        & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Vfx/reconcile_vfx_manifest.py") --check
    }
    if ($LASTEXITCODE -eq 0) {
        & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Vfx/generate_vfx_capabilities.py") --check
    }
    if ($LASTEXITCODE -eq 0) {
        & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Vfx/test_vfx_parity_tooling.py")
    }
}
Invoke-Checked "Performance gate tooling tests" {
    & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Performance/test_validate_capture.py")
}
if ($PerformanceSnapshot -or $PerformanceHistory -or $PerformanceMetadata)
{
    if (-not ($PerformanceSnapshot -and $PerformanceHistory -and $PerformanceMetadata))
    {
        throw "PerformanceSnapshot, PerformanceHistory, and PerformanceMetadata must be supplied together."
    }
    Invoke-Checked "Reference-hardware performance gate" {
        & $python.Executable @pythonPrefix (Join-Path $root "Scripts/Performance/validate_capture.py") `
            --snapshot $PerformanceSnapshot --history $PerformanceHistory --metadata $PerformanceMetadata `
            --profile $PerformanceProfile
    }
}

Invoke-Checked "Debug test matrix" {
    & $project test -Generator ninja -Configuration Debug -Toolset msc -Architecture $Architecture
}
Invoke-Checked "Release test matrix" {
    & $project test -Generator ninja -Configuration Release -Toolset msc -Architecture $Architecture
}
if (-not $SkipSanitizers)
{
    Invoke-Checked "AddressSanitizer test matrix" {
        & $project test -Generator ninja -Configuration DebugASan -Toolset msc -Architecture $Architecture
    }
}
Invoke-Checked "Windows regression harness" {
    & (Join-Path $root "Scripts/Tests/test-windows.ps1")
}
if ($IncludeGraphicsSmokes)
{
    Invoke-Checked "Release project-aware graphics smoke" {
        & $project run -Generator ninja -Configuration Release -Toolset msc -Architecture $Architecture -SmokeProject
    }
}
if ($IncludePackage)
{
    Invoke-Checked "SDK package and consumer validation" {
        & $project package -Generator ninja -Configuration Release -Toolset msc -Architecture $Architecture
    }
}

Write-Host "Production validation completed successfully."
