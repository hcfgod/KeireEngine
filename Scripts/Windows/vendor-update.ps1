[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("spdlog", "doctest", "SDL", "json", "imgui", "zstd", "entt", "glm", "SDL_shadercross")]
    [string]$Dependency,
    [Parameter(Mandatory=$true)]
    [string]$Tag
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$LockPath = Join-Path $Root "Config\Dependencies.lock"
$directory = Join-Path $Root "Vendor\$Dependency"
if (-not (Test-Path $directory)) { throw "Vendor/$Dependency is not initialized. Run bootstrap first." }

& git -C $directory fetch --tags --force
if ($LASTEXITCODE -ne 0) { throw "Failed to fetch $Dependency tags." }
& git -C $directory checkout --detach $Tag
if ($LASTEXITCODE -ne 0) { throw "Failed to check out $Dependency tag $Tag." }
$commit = (& git -C $directory rev-parse HEAD).Trim()
$prefix = $Dependency.ToUpperInvariant()
$lines = Get-Content -LiteralPath $LockPath | ForEach-Object {
    if ($_ -match "^${prefix}_TAG=") { "${prefix}_TAG=$Tag" }
    elseif ($_ -match "^${prefix}_COMMIT=") { "${prefix}_COMMIT=$commit" }
    else { $_ }
}
[System.IO.File]::WriteAllLines($LockPath, $lines, [System.Text.UTF8Encoding]::new($false))

Write-Host "==> $Dependency now points to $Tag ($commit)"
Write-Host "Review the dependency and lock change, then run:"
Write-Host "  git add Vendor/$Dependency Config/Dependencies.lock"
