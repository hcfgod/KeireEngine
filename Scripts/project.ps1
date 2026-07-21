[CmdletBinding()]
param(
    [ValidateSet("menu", "bootstrap", "generate", "build", "test", "run", "clean", "coverage", "package", "doctor", "rename", "vendor-update", "help")]
    [string]$Command = "menu",
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [string]$Target = "",
    [ValidateSet("spdlog", "doctest", "SDL", "json", "imgui", "zstd", "entt", "glm", "SDL_shadercross", "assimp", "stb")]
    [string]$Dependency = "spdlog",
    [string]$Tag = "",
    [string]$Name = "",
    [string]$DisplayName = "",
    [string]$Repository = "",
    [ValidateSet("all", "full", "build", "generated")][string]$CleanScope = "full",
    [switch]$InstallOptional,
    [switch]$Update,
    [switch]$CI,
    [switch]$SmokeUi,
    [switch]$SmokeProject,
    [switch]$Editor,
    [string]$ProjectPath = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$TargetWasProvided = $PSBoundParameters.ContainsKey("Target")
# Windows PowerShell 5 otherwise uses the active OEM code page for interactive
# input and native command output, which corrupts names such as "Kéire".
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
$ConfigurationWasProvided = $PSBoundParameters.ContainsKey("Configuration")
$WindowsScripts = Join-Path $PSScriptRoot "Windows"
. (Join-Path $WindowsScripts "common.ps1")
$script:ProjectCommandExitCode = 0
$Project = Get-ProjectConfig
$Target = if ($Target) { $Target } else { $Project.CLIENT_TARGET }
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
if ($Command -eq "package" -and -not $ConfigurationWasProvided) { $Configuration = "Release" }

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $global:LASTEXITCODE = 0
    & $Action
    $exitCode = $global:LASTEXITCODE
    if ($exitCode -ne 0) {
        $script:ProjectCommandExitCode = $exitCode
        throw "$Description failed with exit code $exitCode."
    }
}

function Invoke-ProjectCommand {
    param([string]$SelectedCommand)
    switch ($SelectedCommand) {
        "bootstrap" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "bootstrap.ps1") -Generators @($Generator) -Architecture $Architecture `
                    -Toolset $Toolset -InstallOptional:$InstallOptional -Update:$Update -Force:$Force
            } "Bootstrap"
        }
        "generate" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "generate.ps1") -Generator $Generator -Architecture $Architecture `
                    -Toolset $Toolset -CI:$CI -Update:$Update -Force:$Force
            } "Project generation"
        }
        "build" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "build.ps1") -Generator $Generator -Configuration $Configuration `
                    -Architecture $Architecture -Toolset $Toolset -Target $Target -CI:$CI -Update:$Update `
                    -Generate:$Force
            } "Build"
        }
        "test" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "test.ps1") -Generator $Generator -Configuration $Configuration `
                    -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Force
            } "Tests"
        }
        "run" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "run.ps1") -Generator $Generator -Configuration $Configuration `
                    -Architecture $Architecture -Toolset $Toolset -CI:$CI -SmokeUi:$SmokeUi `
                    -SmokeProject:$SmokeProject -Editor:$Editor -ProjectPath $ProjectPath -Update:$Update `
                    -Generate:$Force
            } "Run"
        }
        "clean" {
            Invoke-CheckedCommand { & (Join-Path $WindowsScripts "clean.ps1") -Scope $CleanScope } "Clean"
        }
        "coverage" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "coverage.ps1") -Architecture $Architecture -CI:$CI -Update:$Update `
                    -Generate:$Force
            } "Coverage"
        }
        "package" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "package.ps1") -Generator $Generator -Configuration $Configuration `
                    -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Force
            } "Package"
        }
        "doctor" {
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "doctor.ps1") -Generator $Generator -Architecture $Architecture `
                    -Toolset $Toolset
            } "Doctor"
        }
        "rename" {
            if (-not $Name) { throw "-Name is required for rename." }
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "rename.ps1") -Name $Name -DisplayName $DisplayName `
                    -Repository $Repository
            } "Rename"
        }
        "vendor-update" {
            if (-not $Tag) { throw "-Tag is required for vendor-update." }
            Invoke-CheckedCommand {
                & (Join-Path $WindowsScripts "vendor-update.ps1") -Dependency $Dependency -Tag $Tag
            } "Vendor update"
        }
        "help" { Show-Help }
    }
}

function Show-Help {
    Write-Host @"
Usage: Scripts\project.ps1 <command> [options]

Commands: bootstrap, generate, build, test, run, clean, coverage, package,
          doctor, rename, vendor-update, help

Common options:
  -Generator <vs2026|vs2022|vs2019|ninja|gmake|compilecommands>
  -Configuration <Debug|Release|Dist|DebugASan|DebugUBSan|DebugTSan|Coverage>
  -Architecture <x86_64|ARM64>  -Toolset <default|msc|gcc|clang>
  -SmokeUi (run command only; requires a graphics-capable environment)
  -SmokeProject (run the sample project editor and exit after several frames)
  -Editor -ProjectPath <path> (open the editor directly instead of the project hub)
"@
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
        $script:Configuration = Read-Setting "Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan, Coverage)" $Configuration
    }
}

function Show-Menu {
    while ($true) {
        Write-Host ""
        Write-Host $Project.PROJECT_IDENTIFIER
        Write-Host "1. Bootstrap prerequisites"
        Write-Host "2. Generate project files"
        Write-Host "3. Build"
        Write-Host "4. Run tests"
        Write-Host "5. Run $($Project.HUB_TARGET)"
        Write-Host "6. Coverage report"
        Write-Host "7. Package SDK"
        Write-Host "8. Doctor"
        Write-Host "9. Clean"
        Write-Host "10. Vendor update"
        Write-Host "11. Rename template"
        Write-Host "12. Exit"
        Write-Host ""
        $choice = Read-Host "Choose an option"
        try {
            switch ($choice) {
                "1" { Read-BuildSettings $false; $script:InstallOptional=(Read-Setting "Install optional toolchains (yes, no)" "no") -match '^(y|yes)$'; Invoke-ProjectCommand bootstrap }
                "2" { Read-BuildSettings $false; $script:Force=(Read-Setting "Force regeneration (yes, no)" "no") -match '^(y|yes)$'; Invoke-ProjectCommand generate }
                "3" { Read-BuildSettings $true; Invoke-ProjectCommand build }
                "4" { Read-BuildSettings $true; Invoke-ProjectCommand test }
                "5" { Read-BuildSettings $true; Invoke-ProjectCommand run }
                "6" { Read-BuildSettings $false; Invoke-ProjectCommand coverage }
                "7" { Read-BuildSettings $false; $script:Configuration=Read-Setting "Package configuration (Release, Dist)" "Release"; Invoke-ProjectCommand package }
                "8" { Read-BuildSettings $false; Invoke-ProjectCommand doctor }
                "9" { $script:CleanScope = Read-Setting "Clean scope (full, build, generated)" $CleanScope; Invoke-ProjectCommand clean }
                "10" { $script:Dependency=Read-Setting "Dependency (spdlog, doctest, SDL, json, imgui)" $Dependency; $script:Tag=Read-Setting "Tag" $Tag; Invoke-ProjectCommand vendor-update }
                "11" {
                    # Keep proposed values local so a failed rename cannot poison
                    # the defaults shown by the next menu attempt.
                    $proposedName = Read-Setting "PascalCase identifier" $Project.PROJECT_IDENTIFIER
                    $proposedDisplayName = Read-Setting "Display name" $Project.PROJECT_DISPLAY_NAME
                    $proposedRepository = Read-Setting "Repository (owner/name, optional)" $Project.REPOSITORY_SLUG
                    $script:Name = $proposedName
                    $script:DisplayName = $proposedDisplayName
                    $script:Repository = $proposedRepository
                    Invoke-ProjectCommand rename
                    $script:Project = Get-ProjectConfig
                    if (-not $TargetWasProvided) {
                        $script:Target = $Project.CLIENT_TARGET
                    }
                }
                "12" { return }
                default { Write-Warning "Invalid menu choice '$choice'." }
            }
        }
        catch { Write-Host "`nCommand failed:`n$($_.Exception.Message)" -ForegroundColor Red }
        Write-Host ""
        Read-Host "Press Enter to return to the menu"
    }
}

if ($Command -eq "menu") {
    Show-Menu
}
else {
    try {
        Invoke-ProjectCommand $Command
    }
    catch {
        [Console]::Error.WriteLine($_.Exception.Message)
        exit $(if ($script:ProjectCommandExitCode -ne 0) { $script:ProjectCommandExitCode } else { 1 })
    }
    exit 0
}
