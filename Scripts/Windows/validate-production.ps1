[CmdletBinding()]
param(
    [switch]$SkipSanitizers,
    [switch]$IncludePackage,
    [string]$Architecture = "x86_64"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$project = Join-Path $root "Scripts/project.ps1"

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
if ($IncludePackage)
{
    Invoke-Checked "SDK package and consumer validation" {
        & $project package -Generator ninja -Configuration Release -Toolset msc -Architecture $Architecture
    }
}

Write-Host "Production validation completed successfully."
