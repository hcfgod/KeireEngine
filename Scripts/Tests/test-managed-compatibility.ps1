param(
    [string]$Dotnet = "dotnet"
)

$ErrorActionPreference = "Stop"
$consumer = Join-Path $PSScriptRoot "ManagedCompatibilityConsumer"
Push-Location -LiteralPath $consumer
try {
    & $Dotnet --version
    if ($LASTEXITCODE -ne 0) {
        throw "The installed .NET 8 SDK is required for compatibility consumer validation."
    }
    & $Dotnet run --project "ManagedCompatibilityConsumer.csproj" --configuration Release
    if ($LASTEXITCODE -ne 0) {
        throw "The SDK 8 native generator consumer failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
