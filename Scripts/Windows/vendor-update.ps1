[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("spdlog", "doctest")]
    [string]$Dependency,
    [Parameter(Mandatory=$true)]
    [string]$Tag
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$directory = Join-Path $Root "Vendor\$Dependency"
if (-not (Test-Path $directory)) { throw "Vendor/$Dependency is not initialized. Run bootstrap first." }

& git -C $directory fetch --tags --force
if ($LASTEXITCODE -ne 0) { throw "Failed to fetch $Dependency tags." }
& git -C $directory checkout --detach $Tag
if ($LASTEXITCODE -ne 0) { throw "Failed to check out $Dependency tag $Tag." }
$commit = (& git -C $directory rev-parse HEAD).Trim()

Write-Host "==> $Dependency now points to $Tag ($commit)"
Write-Host "Review the dependency, update the pinned commit in vendor scripts, then run:"
Write-Host "  git add Vendor/$Dependency Scripts"
