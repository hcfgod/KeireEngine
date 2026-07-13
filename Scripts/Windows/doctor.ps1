[CmdletBinding()]
param([string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
Write-Host "$($Project.PROJECT_DISPLAY_NAME) environment"
Write-Host "  Generator:    $Generator"
Write-Host "  Architecture: $Architecture"
Write-Host "  Toolset:      $Toolset"
foreach ($command in @("git", "cmake", "winget", "ninja", "clang", "llvm-profdata", "llvm-cov")) {
    $found = if ($command -eq "cmake") { Get-CMakeExecutable } else { (Get-Command $command -ErrorAction SilentlyContinue).Source }
    $commandPath = if ($found) { $found } else { "not found" }
    Write-Host ("  {0,-14} {1}" -f $command, $commandPath)
}
if ($Toolset -eq "msc") {
    $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
    $environment = Get-VSBuildEnvironment $majorVersion
    Write-Host "  MSBuild:       $($environment.MSBuild)"
}
foreach ($dependency in @("spdlog", "doctest")) {
    $directory = Join-Path $Root "Vendor\$dependency"
    $commit = if (Test-Path $directory) { (& git -C $directory rev-parse HEAD 2>$null) -join "" } else { "not initialized" }
    Write-Host ("  {0,-14} {1}" -f $dependency, $commit)
}
