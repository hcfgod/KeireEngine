[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\Windows\common.ps1")

# Do not touch shared engine/dependency caches while exercising installed developer environments.
$fixture = Join-Path (Get-RepositoryRoot) "Build\Validation\VisualStudioToolchains"
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
$source = Join-Path $fixture "smoke.cpp"
@'
#include <algorithm>
#include <array>
#include <span>
int main()
{
    std::array values{3, 1, 2};
    std::ranges::sort(values);
    const std::span<const int> view(values);
    return view[0] == 1 && view[2] == 3 ? 0 : 1;
}
'@ | Set-Content -LiteralPath $source -Encoding ASCII

$installed = @()
foreach ($major in @(16, 17, 18)) {
    try { $null = Get-VSBuildEnvironment $major; $installed += $major }
    catch { Write-Host "SKIP: Visual Studio $major C++ environment unavailable." }
}
if ($installed.Count -eq 0) { throw "No supported Visual Studio C++ environment is installed." }
# Return to the first installation as well as switching forward: inherited VC paths must not leak either way.
foreach ($major in @($installed) + @($installed[0])) {
    $environment = Enter-VSDeveloperEnvironment $major x86_64
    $expected = (Get-Content -LiteralPath (Join-Path $environment.InstallationPath `
        "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt") -Raw).Trim()
    if ($env:VCToolsVersion.TrimEnd('\') -ne $expected) {
        throw "VS $major selected compiler $env:VCToolsVersion instead of $expected."
    }
    $compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop).Source
    $expectedRoot = [IO.Path]::GetFullPath($environment.InstallationPath).TrimEnd('\') + '\'
    if (-not $compiler.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "VS $major selected a compiler from another installation: $compiler"
    }
    $object = Join-Path $fixture "smoke-$major.obj"
    $executable = Join-Path $fixture "smoke-$major.exe"
    & $compiler /nologo /std:c++20 /EHsc $source "/Fo$object" "/Fe$executable"
    if ($LASTEXITCODE -ne 0) { throw "VS $major C++20 compile/link failed." }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "VS $major C++20 execution failed." }
    Write-Host "PASS: Visual Studio $major, compiler $expected, C++20 compile/link/run."
}
