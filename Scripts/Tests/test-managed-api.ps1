param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"

if (-not (Test-Path -LiteralPath $dotnet)) {
    throw "The bundled .NET SDK was not found. Generate dependencies before running managed API tests."
}

$projects = @(
    (Join-Path $root "KeireManaged.Tests\Keire.Managed.Production.Tests.csproj"),
    (Join-Path $root "KeireEditorManaged.Tests\Keire.Editor.Managed.Production.Tests.csproj"),
    (Join-Path $root "KeireManaged.Generators.Tests\Keire.Managed.Generators.Production.Tests.csproj")
)
foreach ($project in $projects) {
    & $dotnet run --project $project --configuration $Configuration --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Managed API tests failed for '$project' with exit code $LASTEXITCODE."
    }
}

& (Join-Path $root "Scripts/Windows/build-managed.ps1")
& $dotnet run --project (Join-Path $root "Scripts/Tests/ManagedDesignTimeParity/ManagedDesignTimeParity.csproj") `
    --configuration $Configuration -- (Join-Path $root "Build/Managed")
if ($LASTEXITCODE -ne 0) {
    throw "Managed design-time binary API parity failed with exit code $LASTEXITCODE."
}
