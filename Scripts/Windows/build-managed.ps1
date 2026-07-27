[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"
$project = Join-Path $root "KeireManaged\Keire.Managed.csproj"
$output = Join-Path $root "Build\Managed"
$intermediate = Join-Path $root "Build\Intermediates\Managed\"

if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "The bundled .NET SDK is missing: $dotnet"
}

& $dotnet build $project --nologo --configuration Release --output $output `
    "--property:BaseIntermediateOutputPath=$intermediate"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
