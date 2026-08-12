param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"

if (-not (Test-Path -LiteralPath $dotnet)) {
    throw "The bundled .NET SDK was not found. Generate dependencies before running distribution service tests."
}

& $dotnet run `
    --project (Join-Path $root "Services\KeireDistributionService\tests\KeireDistributionService.Tests\KeireDistributionService.Tests.csproj") `
    --configuration $Configuration `
    --nologo
if ($LASTEXITCODE -ne 0) {
    throw "Distribution service tests failed with exit code $LASTEXITCODE."
}

& $dotnet run `
    --project (Join-Path $root "Services\KeireDistributionService\tests\KeireMarketplaceValidator.Tests\KeireMarketplaceValidator.Tests.csproj") `
    --configuration $Configuration `
    --nologo
if ($LASTEXITCODE -ne 0) {
    throw "Marketplace validator tests failed with exit code $LASTEXITCODE."
}
