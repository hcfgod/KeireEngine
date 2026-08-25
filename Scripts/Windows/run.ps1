[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Profile", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$CI,
    [switch]$SmokeWindow,
    [switch]$SmokeUi,
    [switch]$SmokeProject,
    [switch]$Editor,
    [string]$ProjectPath = "",
    [switch]$Update,
    [switch]$Generate
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset
$outputArchitecture = Get-ArchitectureOutputName $Architecture
$ClientExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe"
$HubExe = Join-Path $Root "Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.HUB_TARGET)\$($Project.HUB_TARGET).exe"
$editorDevTarget = "$($Project.PROJECT_NAMESPACE)EditorDev"

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
    -Architecture $Architecture -Toolset $Toolset -Target $editorDevTarget -CI:$CI -Update:$Update -Generate:$Generate
if (-not (Test-Path $ClientExe)) { throw "KeireClient executable was not found: $ClientExe" }
if (-not $Editor -and -not $SmokeWindow -and -not $SmokeProject -and -not $ProjectPath) {
    & (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
        -Architecture $Architecture -Toolset $Toolset -Target $Project.HUB_TARGET -CI:$CI
    if (-not (Test-Path $HubExe)) { throw "Project hub executable was not found: $HubExe" }
}

Push-Location $Root
$originalPath = $env:PATH
try {
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -eq "msc")
    if ($Configuration -eq "DebugASan" -and $usesMSVC) {
        $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
        $runtimeDirectory = Get-MSVCASanRuntimeDirectory $majorVersion $Architecture
        $env:PATH = "$runtimeDirectory;$env:PATH"
    }
    if ($SmokeUi -and -not $Editor) {
        Write-Host "==> Running project hub UI smoke $Configuration for $Architecture"
        & $HubExe --smoke-ui
    }
    elseif ($SmokeProject) {
        $smokeProjectPath = if ($ProjectPath) { $ProjectPath } else { Join-Path $Root "Samples\KeireSandbox" }
        Write-Host "==> Running project-aware editor smoke $Configuration for $Architecture"
        & $ClientExe --project $smokeProjectPath --smoke-project
    }
    elseif ($CI -or $SmokeWindow) {
        Write-Host "==> Running KeireClient window smoke $Configuration for $Architecture"
        $originalVideoDriver = $env:SDL_VIDEODRIVER
        $env:SDL_VIDEODRIVER = "dummy"
        & $ClientExe --project (Join-Path $Root "Samples\KeireSandbox") --smoke-window
        if ($null -eq $originalVideoDriver) { Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue }
        else { $env:SDL_VIDEODRIVER = $originalVideoDriver }
    }
    elseif ($Editor -or $ProjectPath) {
        if (-not $ProjectPath) { throw "-ProjectPath is required when launching the editor directly." }
        Write-Host "==> Running KeireClient for project $ProjectPath"
        & $ClientExe --project $ProjectPath
    }
    else {
        Write-Host "==> Running $($Project.HUB_TARGET) $Configuration for $Architecture"
        & $HubExe
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $cliRoot = Join-Path $env:TEMP ("client-cli-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory $cliRoot | Out-Null
    Push-Location $cliRoot
    try {
        foreach ($option in @("--help", "-h", "--version", "-v")) {
            & $ClientExe $option
            $cliExitCode = $LASTEXITCODE
            if ($cliExitCode -ne 0) { throw "KeireClient $option failed with exit code $cliExitCode." }
        }
        $invalidStart = [Diagnostics.ProcessStartInfo]::new()
        $invalidStart.FileName = $ClientExe
        $invalidStart.Arguments = "--invalid"
        $invalidStart.WorkingDirectory = $cliRoot
        $invalidStart.UseShellExecute = $false
        $invalidStart.CreateNoWindow = $true
        $invalidStart.RedirectStandardError = $true
        $invalid = [Diagnostics.Process]::new()
        $invalid.StartInfo = $invalidStart
        try {
            if (-not $invalid.Start()) { throw "KeireClient invalid option probe did not start." }
            $invalidDiagnostic = $invalid.StandardError.ReadToEnd()
            $invalid.WaitForExit()
            $invalidExitCode = $invalid.ExitCode
        }
        finally { $invalid.Dispose() }
        if ($invalidExitCode -ne 2) {
            throw "KeireClient invalid option returned $invalidExitCode, expected 2. Diagnostic: $invalidDiagnostic"
        }
        if (Test-Path (Join-Path $cliRoot "Logs")) { throw "Informational KeireClient commands created logs." }
    }
    finally {
        Pop-Location
        Remove-Item $cliRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    exit 0
}
finally { $env:PATH = $originalPath; Pop-Location }
