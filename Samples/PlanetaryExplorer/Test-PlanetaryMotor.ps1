param(
    [string] $ManagedAssembly = '',
    [string] $Dotnet = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($Dotnet))
{
    . (Join-Path $repo 'Scripts/Windows/common.ps1')
    $dependencyLock = Get-DependencyLock
    $Dotnet = Join-Path $repo "Build/Tools/dotnet/$($dependencyLock.DOTNET_SDK_VERSION)/dotnet.exe"
}
if ([string]::IsNullOrWhiteSpace($ManagedAssembly))
{
    $ManagedAssembly = Join-Path $repo 'Build/Managed/Keire.Managed.dll'
}
$ManagedAssembly = (Resolve-Path -LiteralPath $ManagedAssembly).Path
$motor = Join-Path $repo 'Samples/KeireSandbox/Assets/FirstPersonController/Runtime/FirstPersonMotor.cs'
$template = Join-Path $repo 'KeireHubContent/Templates/Payloads/Sandbox/Assets/FirstPersonController/Runtime/FirstPersonMotor.cs'
if ((Get-FileHash -LiteralPath $motor).Hash -ne (Get-FileHash -LiteralPath $template).Hash)
{
    throw 'Canonical and Hub template motors differ.'
}
$directory = Join-Path $repo ('Build/Validation/PlanetaryMotor-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
$assemblyXml = [Security.SecurityElement]::Escape($ManagedAssembly)
$motorXml = [Security.SecurityElement]::Escape($motor)
$harnessXml = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'PlanetaryMotorValidation.cs'))
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="Keire.Managed"><HintPath>$assemblyXml</HintPath></Reference>
    <Compile Include="$motorXml" />
    <Compile Include="$harnessXml" />
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $directory 'MotorValidation.csproj')
& $Dotnet run --project (Join-Path $directory 'MotorValidation.csproj') --configuration Release
if ($LASTEXITCODE -ne 0)
{
    throw "Planetary motor validation failed: $LASTEXITCODE"
}
