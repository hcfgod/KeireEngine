[CmdletBinding()]
param(
    [ValidateSet("vs2026", "vs2022", "vs2019", "ninja", "gmake")]
    [string]$Generator = "vs2022",
    [ValidateSet("Debug", "Release", "Dist", "DebugASan", "DebugUBSan", "DebugTSan")]
    [string]$Configuration = "Debug",
    [switch]$Generate
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$TestsExe = Join-Path $Root "Build\Bin\$Configuration-windows-x86_64\Tests\Tests.exe"

function Get-MSVCASanRuntimeDirectory {
    $majorVersion = switch ($Generator) {
        "vs2019" { 16 }
        "vs2022" { 17 }
        "vs2026" { 18 }
        default { return $null }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found while locating the MSVC AddressSanitizer runtime."
    }

    $range = "[$majorVersion.0,$($majorVersion + 1).0)"
    $installationPaths = @(& $vswhere -products * -version $range `
        -requires Microsoft.VisualStudio.Workload.NativeDesktop `
        -property installationPath)

    foreach ($installationPath in $installationPaths) {
        $toolsetsRoot = Join-Path $installationPath "VC\Tools\MSVC"
        if (-not (Test-Path $toolsetsRoot)) {
            continue
        }

        $toolsets = Get-ChildItem -Path $toolsetsRoot -Directory |
            Sort-Object { [version]$_.Name } -Descending

        foreach ($toolset in $toolsets) {
            foreach ($hostArchitecture in @("Hostx64", "Hostx86")) {
                $runtimeDirectory = Join-Path $toolset.FullName "bin\$hostArchitecture\x64"
                $runtimeNames = @(
                    "clang_rt.asan_dynamic-x86_64.dll",
                    "clang_rt.asan_dbg_dynamic-x86_64.dll"
                )
                $runtime = $runtimeNames |
                    Where-Object { Test-Path (Join-Path $runtimeDirectory $_) } |
                    Select-Object -First 1

                if ($runtime) {
                    return $runtimeDirectory
                }
            }
        }
    }

    throw "The x64 MSVC AddressSanitizer runtime was not found in Visual Studio $majorVersion."
}

& (Join-Path $PSScriptRoot "build.ps1") -Generator $Generator -Configuration $Configuration -Target "Tests" -Generate:$Generate

if (-not (Test-Path $TestsExe)) {
    throw "Tests executable was not found: $TestsExe"
}

Write-Host "==> Running Tests $Configuration"
$originalPath = $env:PATH
$testExitCode = 1

try {
    if ($Configuration -eq "DebugASan") {
        $asanRuntimeDirectory = Get-MSVCASanRuntimeDirectory
        if ($asanRuntimeDirectory) {
            Write-Host "==> Using MSVC AddressSanitizer runtime from $asanRuntimeDirectory"
            $env:PATH = "$asanRuntimeDirectory;$env:PATH"
        }
    }

    & $TestsExe
    $testExitCode = $LASTEXITCODE
}
finally {
    $env:PATH = $originalPath
}

exit $testExitCode
