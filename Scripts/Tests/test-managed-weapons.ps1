param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"

if (-not (Test-Path -LiteralPath $dotnet)) {
    throw "The bundled .NET SDK was not found. Generate dependencies before running managed weapon tests."
}

& $dotnet run `
    --project (Join-Path $root "KeireManaged.Tests\Keire.Managed.Production.Tests.csproj") `
    --configuration $Configuration `
    --nologo
if ($LASTEXITCODE -ne 0) {
    throw "Managed production tests failed with exit code $LASTEXITCODE."
}
