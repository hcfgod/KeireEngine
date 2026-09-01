[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dotnet = Join-Path $root "Build\Dependencies\dotnet-sdk\dotnet.exe"
$project = Join-Path $root "KeireEditorManaged\Keire.Editor.Managed.csproj"
$generatorProject = Join-Path $root "KeireManaged.Generators\Keire.Managed.Generators.csproj"
$sourceRoots = @(
    (Join-Path $root "KeireManaged"),
    (Join-Path $root "KeireEditorManaged"),
    (Join-Path $root "KeireManaged.Generators")
)
$output = Join-Path $root "Build\Managed"
$intermediate = Join-Path $root "Build\Intermediates\Managed\"
$assembly = Join-Path $output "Keire.Managed.dll"
$editorAssembly = Join-Path $output "Keire.Editor.Managed.dll"
$generatorAssembly = Join-Path $output "Keire.Managed.Generators.dll"

if ((Test-Path -LiteralPath $assembly -PathType Leaf) -and
    (Test-Path -LiteralPath $editorAssembly -PathType Leaf) -and
    (Test-Path -LiteralPath $generatorAssembly -PathType Leaf)) {
    $assemblyWriteTime = @(
        (Get-Item -LiteralPath $assembly).LastWriteTimeUtc,
        (Get-Item -LiteralPath $editorAssembly).LastWriteTimeUtc,
        (Get-Item -LiteralPath $generatorAssembly).LastWriteTimeUtc
    ) | Sort-Object | Select-Object -First 1
    $managedInputs = @()
    foreach ($sourceRoot in $sourceRoots) {
        $managedInputs += @((Get-Item -LiteralPath $sourceRoot))
        $managedInputs += @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force | Where-Object {
                $_.FullName -notmatch '[\\/](?:bin|obj|Build)(?:[\\/]|$)' -and
                ($_.PSIsContainer -or $_.Extension -in @(".cs", ".csproj"))
            })
    }
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

$generatorIntermediate = Join-Path $root "Build\Intermediates\ManagedGenerators\"
& $dotnet build $generatorProject --nologo --configuration Release --output $output `
    "--property:BaseIntermediateOutputPath=$generatorIntermediate"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $assembly -PathType Leaf)) {
    throw "The managed runtime API build did not produce: $assembly"
}
if (-not (Test-Path -LiteralPath $editorAssembly -PathType Leaf)) {
    throw "The managed editor API build did not produce: $editorAssembly"
}
if (-not (Test-Path -LiteralPath $generatorAssembly -PathType Leaf)) {
    throw "The managed generator build did not produce: $generatorAssembly"
}

(Get-Item -LiteralPath $assembly).LastWriteTimeUtc = [DateTime]::UtcNow
(Get-Item -LiteralPath $editorAssembly).LastWriteTimeUtc = [DateTime]::UtcNow
(Get-Item -LiteralPath $generatorAssembly).LastWriteTimeUtc = [DateTime]::UtcNow
$global:LASTEXITCODE = 0
