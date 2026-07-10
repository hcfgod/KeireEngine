[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake", "compilecommands")]
    [string]$Generator = "vs2022",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$PremakeExe = Join-Path $Root "Tools\Windows\premake5.exe"

function Get-NinjaExecutable {
    $command = Get-Command "ninja" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $link = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\ninja.exe"
    if (Test-Path $link) {
        return $link
    }

    $packageRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (Test-Path $packageRoot) {
        $packageNinja = Get-ChildItem -Path $packageRoot -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($packageNinja) {
            return $packageNinja.FullName
        }
    }

    throw "Ninja was installed but is not visible in this shell. Open a new terminal or add the Winget package path to PATH."
}

& (Join-Path $PSScriptRoot "bootstrap.ps1") -Generators @($Generator) -Force:$Force

Write-Host "==> Generating project files with Premake action '$Generator'"
$premakeFile = Join-Path $Root "premake5.lua"

if ($Generator -eq "compilecommands") {
    & $PremakeExe "--file=$premakeFile" "ninja"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $ninja = Get-NinjaExecutable
    & $ninja -C $Root -f "build.ninja" -t compdb | Set-Content -Path (Join-Path $Root "compile_commands.json") -Encoding UTF8
    Write-Host "Generated compile_commands.json"
}
else {
    & $PremakeExe "--file=$premakeFile" $Generator
}
