[CmdletBinding()]
param(
    [ValidateSet("menu", "bootstrap", "generate", "build", "test", "run", "clean", "vendor-update")]
    [string]$Command = "menu",
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [string]$Target = "Client",
    [ValidateSet("spdlog", "doctest")]
    [string]$Dependency = "spdlog",
    [string]$Tag = "",
    [switch]$InstallOptional,
    [switch]$Update,
    [switch]$CI,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$WindowsScripts = Join-Path $PSScriptRoot "Windows"
. (Join-Path $WindowsScripts "common.ps1")
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }

function Invoke-ProjectCommand {
    param([string]$SelectedCommand)
    switch ($SelectedCommand) {
        "bootstrap" {
            & (Join-Path $WindowsScripts "bootstrap.ps1") -Generators @($Generator) -Architecture $Architecture `
                -Toolset $Toolset -InstallOptional:$InstallOptional -Update:$Update -Force:$Force
        }
        "generate" {
            & (Join-Path $WindowsScripts "generate.ps1") -Generator $Generator -Architecture $Architecture `
                -Toolset $Toolset -CI:$CI -Update:$Update -Force:$Force
        }
        "build" {
            & (Join-Path $WindowsScripts "build.ps1") -Generator $Generator -Configuration $Configuration `
                -Architecture $Architecture -Toolset $Toolset -Target $Target -CI:$CI -Update:$Update -Generate:$Force
        }
        "test" {
            & (Join-Path $WindowsScripts "test.ps1") -Generator $Generator -Configuration $Configuration `
                -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Force
        }
        "run" {
            & (Join-Path $WindowsScripts "run.ps1") -Generator $Generator -Configuration $Configuration `
                -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Force
        }
        "clean" { & (Join-Path $WindowsScripts "clean.ps1") -All }
        "vendor-update" {
            if (-not $Tag) { throw "-Tag is required for vendor-update." }
            & (Join-Path $WindowsScripts "vendor-update.ps1") -Dependency $Dependency -Tag $Tag
        }
    }
}

function Read-Setting([string]$Prompt, [string]$Current) {
    $value = Read-Host "$Prompt [$Current]"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Current }
    return $value
}

function Read-BuildSettings([bool]$IncludeConfiguration) {
    $script:Generator = Read-Setting "Generator (vs2026, vs2022, vs2019, ninja, gmake)" $Generator
    $script:Architecture = Normalize-Architecture (Read-Setting "Architecture (x86_64, ARM64)" $Architecture)
    $script:Toolset = Read-Setting "Toolset (default, msc, gcc, clang)" $Toolset
    $updateDefault = if ($Update) { "yes" } else { "no" }
    $updateChoice = Read-Setting "Update installed prerequisites (yes, no)" $updateDefault
    $script:Update = $updateChoice -match '^(y|yes)$'
    if ($IncludeConfiguration) {
        $script:Configuration = Read-Setting "Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan)" $Configuration
    }
}

function Show-Menu {
    while ($true) {
        Write-Host ""
        Write-Host "CrossPlatformCoreClientTemplate"
        Write-Host "1. Bootstrap prerequisites"
        Write-Host "2. Generate project files"
        Write-Host "3. Build"
        Write-Host "4. Run tests"
        Write-Host "5. Run Client"
        Write-Host "6. Clean"
        Write-Host "7. Exit"
        Write-Host ""
        $choice = Read-Host "Choose an option"
        try {
            switch ($choice) {
                "1" { Read-BuildSettings $false; Invoke-ProjectCommand bootstrap }
                "2" { Read-BuildSettings $false; Invoke-ProjectCommand generate }
                "3" { Read-BuildSettings $true; Invoke-ProjectCommand build }
                "4" { Read-BuildSettings $true; Invoke-ProjectCommand test }
                "5" { Read-BuildSettings $true; Invoke-ProjectCommand run }
                "6" { Invoke-ProjectCommand clean }
                "7" { return }
                default { Write-Warning "Invalid menu choice '$choice'." }
            }
        }
        catch { Write-Host "`nCommand failed:`n$($_.Exception.Message)" -ForegroundColor Red }
        Write-Host ""
        Read-Host "Press Enter to return to the menu"
    }
}

if ($Command -eq "menu") { Show-Menu } else { Invoke-ProjectCommand $Command }
