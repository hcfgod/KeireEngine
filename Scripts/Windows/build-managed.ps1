[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"
$project = Join-Path $root "KeireManaged\Keire.Managed.csproj"
$sourceRoot = Join-Path $root "KeireManaged"
$output = Join-Path $root "Build\Managed"
$intermediate = Join-Path $root "Build\Intermediates\Managed\"
$assembly = Join-Path $output "Keire.Managed.dll"

if (Test-Path -LiteralPath $assembly -PathType Leaf) {
    $assemblyWriteTime = (Get-Item -LiteralPath $assembly).LastWriteTimeUtc
    $managedInputs = @((Get-Item -LiteralPath $sourceRoot)) + @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force |
        Where-Object {
            $_.FullName -notmatch '[\\/](?:bin|obj|Build)(?:[\\/]|$)' -and
            ($_.PSIsContainer -or $_.Extension -in @(".cs", ".csproj"))
        })
    $newerInput = $managedInputs | Where-Object {
        $_.LastWriteTimeUtc -gt $assemblyWriteTime
    } | Select-Object -First 1
    $wrapperIsNewer = (Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc -gt $assemblyWriteTime
    if (-not $newerInput -and -not $wrapperIsNewer) {
        Write-Host "==> Managed runtime API is current"
        $global:LASTEXITCODE = 0
        return
    }
}

if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "The bundled .NET SDK is missing: $dotnet"
}

& $dotnet build $project --nologo --configuration Release --output $output `
    "--property:BaseIntermediateOutputPath=$intermediate"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $assembly -PathType Leaf)) {
    throw "The managed runtime API build did not produce: $assembly"
}

(Get-Item -LiteralPath $assembly).LastWriteTimeUtc = [DateTime]::UtcNow
$global:LASTEXITCODE = 0
