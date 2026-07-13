[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan", "Coverage")]
    [string]$Configuration = "Debug",
    [string]$Architecture = "",
    [ValidateSet("default", "msc", "gcc", "clang")]
    [string]$Toolset = "default",
    [switch]$CI,
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

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration `
    -Architecture $Architecture -Toolset $Toolset -Target $Project.CLIENT_TARGET -CI:$CI -Update:$Update -Generate:$Generate
if (-not (Test-Path $ClientExe)) { throw "Client executable was not found: $ClientExe" }

Push-Location $Root
$originalPath = $env:PATH
try {
    $usesMSVC = $Generator -like "vs*" -or ($Generator -eq "ninja" -and $Toolset -eq "msc")
    if ($Configuration -eq "DebugASan" -and $usesMSVC) {
        $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
        $runtimeDirectory = Get-MSVCASanRuntimeDirectory $majorVersion $Architecture
        $env:PATH = "$runtimeDirectory;$env:PATH"
    }
    Write-Host "==> Running Client $Configuration for $Architecture"
    & $ClientExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $cliRoot = Join-Path $env:TEMP ("client-cli-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory $cliRoot | Out-Null
    try {
        foreach ($option in @("--help", "-h", "--version", "-v")) {
            $result = Start-Process -FilePath $ClientExe -ArgumentList $option -WorkingDirectory $cliRoot -NoNewWindow -Wait -PassThru
            if ($result.ExitCode -ne 0) { throw "Client $option failed with exit code $($result.ExitCode)." }
        }
        $invalidOutput = Join-Path $cliRoot "invalid.txt"
        $invalid = Start-Process -FilePath $ClientExe -ArgumentList "--invalid" -WorkingDirectory $cliRoot -NoNewWindow -Wait -PassThru -RedirectStandardError $invalidOutput
        if ($invalid.ExitCode -ne 2) { throw "Client invalid option returned $($invalid.ExitCode), expected 2." }
        if (Test-Path (Join-Path $cliRoot "Logs")) { throw "Informational Client commands created logs." }
    }
    finally { Remove-Item $cliRoot -Recurse -Force -ErrorAction SilentlyContinue }
    exit 0
}
finally { $env:PATH = $originalPath; Pop-Location }
