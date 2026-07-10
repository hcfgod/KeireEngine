[CmdletBinding()]
param(
    [ValidateSet("menu", "bootstrap", "generate", "build", "test", "clean")]
    [string]$Command = "menu",
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan")]
    [string]$Configuration = "Debug",
    [string]$Target = "Client",
    [switch]$InstallOptional,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$WindowsScripts = Join-Path $PSScriptRoot "Windows"

function Invoke-ProjectCommand {
    param(
        [string]$SelectedCommand,
        [string]$SelectedGenerator,
        [string]$SelectedConfiguration,
        [string]$SelectedTarget
    )

    switch ($SelectedCommand) {
        "bootstrap" {
            & (Join-Path $WindowsScripts "bootstrap.ps1") -Generators @($SelectedGenerator) -InstallOptional:$InstallOptional -Force:$Force
        }
        "generate" {
            & (Join-Path $WindowsScripts "generate.ps1") -Generator $SelectedGenerator -Force:$Force
        }
        "build" {
            & (Join-Path $WindowsScripts "build.ps1") -Generator $SelectedGenerator -Configuration $SelectedConfiguration -Target $SelectedTarget -Generate:$Force
        }
        "test" {
            & (Join-Path $WindowsScripts "test.ps1") -Generator $SelectedGenerator -Configuration $SelectedConfiguration -Generate:$Force
        }
        "clean" {
            & (Join-Path $WindowsScripts "clean.ps1") -All
        }
        default {
            throw "Unknown command '$SelectedCommand'."
        }
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
        Write-Host "5. Clean"
        Write-Host "6. Exit"
        Write-Host ""

        $choice = Read-Host "Choose an option"
        try {
            switch ($choice) {
                "1" { Invoke-ProjectCommand "bootstrap" $Generator $Configuration $Target }
                "2" {
                    $selectedGenerator = Read-Host "Generator (vs2026, vs2022, vs2019, ninja, gmake, compilecommands) [$Generator]"
                    if ([string]::IsNullOrWhiteSpace($selectedGenerator)) { $selectedGenerator = $Generator }
                    Invoke-ProjectCommand "generate" $selectedGenerator $Configuration $Target
                    $Generator = $selectedGenerator
                }
                "3" {
                    $selectedGenerator = Read-Host "Generator (vs2026, vs2022, vs2019, ninja, gmake) [$Generator]"
                    if ([string]::IsNullOrWhiteSpace($selectedGenerator)) { $selectedGenerator = $Generator }
                    $selectedConfiguration = Read-Host "Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan) [$Configuration]"
                    if ([string]::IsNullOrWhiteSpace($selectedConfiguration)) { $selectedConfiguration = $Configuration }
                    Invoke-ProjectCommand "build" $selectedGenerator $selectedConfiguration $Target
                    $Generator = $selectedGenerator
                    $Configuration = $selectedConfiguration
                }
                "4" {
                    $selectedGenerator = Read-Host "Generator (vs2026, vs2022, vs2019, ninja, gmake) [$Generator]"
                    if ([string]::IsNullOrWhiteSpace($selectedGenerator)) { $selectedGenerator = $Generator }
                    $selectedConfiguration = Read-Host "Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan) [$Configuration]"
                    if ([string]::IsNullOrWhiteSpace($selectedConfiguration)) { $selectedConfiguration = $Configuration }
                    Invoke-ProjectCommand "test" $selectedGenerator $selectedConfiguration "Tests"
                    $Generator = $selectedGenerator
                    $Configuration = $selectedConfiguration
                }
                "5" { Invoke-ProjectCommand "clean" $Generator $Configuration $Target }
                "6" { return }
                default {
                    Write-Warning "Invalid menu choice '$choice'."
                }
            }
        }
        catch {
            Write-Host ""
            Write-Host "Command failed:" -ForegroundColor Red
            Write-Host $_.Exception.Message -ForegroundColor Red
        }

        Write-Host ""
        Read-Host "Press Enter to return to the menu"
    }
}

if ($Command -eq "menu") {
    Show-Menu
}
else {
    Invoke-ProjectCommand $Command $Generator $Configuration $Target
}
