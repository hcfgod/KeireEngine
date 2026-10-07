[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$ManagedRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../Windows/common.ps1')
$targetFramework = (Get-DependencyLock).DOTNET_TARGET_FRAMEWORK
$dotnet = Join-Path ([IO.Path]::GetFullPath($SdkRoot)) 'dotnet.exe'
$analyzer = Join-Path ([IO.Path]::GetFullPath($ManagedRoot)) 'Keire.Managed.Generators.dll'
foreach ($file in @($dotnet, $analyzer)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required compiler validation input is missing: $file" }
}
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-analyzer-compat-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    $escapedAnalyzer = [Security.SecurityElement]::Escape($analyzer)
    @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>$targetFramework</TargetFramework>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <UseSharedCompilation>false</UseSharedCompilation>
  </PropertyGroup>
  <ItemGroup><Analyzer Include="$escapedAnalyzer" /></ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $fixture 'Probe.csproj') -Encoding utf8
    'public sealed class CompilerCompatibilityProbe { }' |
        Set-Content -LiteralPath (Join-Path $fixture 'Probe.cs') -Encoding utf8
    # This project needs only local framework packs; do not depend on a network restore.
    '<configuration><packageSources><clear /></packageSources></configuration>' |
        Set-Content -LiteralPath (Join-Path $fixture 'NuGet.Config') -Encoding utf8
    Push-Location $fixture
    try {
        & $dotnet build Probe.csproj --nologo --configuration Release --disable-build-servers
        if ($LASTEXITCODE -ne 0) { throw 'The packaged source generator cannot load in this SDK compiler.' }
    }
    finally { Pop-Location }
    Write-Host 'Packaged source generator compiler compatibility passed.'
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ([IO.Path]::GetDirectoryName($resolved) -eq [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') -and
        [IO.Path]::GetFileName($resolved).StartsWith('keire-analyzer-compat-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
