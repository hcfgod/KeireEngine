[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dependencyLock = Get-DependencyLock
# The analyzer must load in the compiler shipped with the Editor, even when the build machine has a newer SDK.
$dotnet = Join-Path $root "Build\Tools\dotnet\$($dependencyLock.DOTNET_SDK_VERSION)\dotnet.exe"
$project = Join-Path $root "KeireEditorManaged\Keire.Editor.Managed.csproj"
$generatorProject = Join-Path $root "KeireManaged.Generators\Keire.Managed.Generators.csproj"
$designTimeProject = Join-Path $root "Scripts\ManagedDesignTime\Keire.Editor.Managed.DesignTime.csproj"
$sourceRoots = @(
    (Join-Path $root "KeireManaged"),
    (Join-Path $root "KeireEditorManaged"),
    (Join-Path $root "KeireManaged.Generators"),
    (Join-Path $root "Scripts\ManagedDesignTime")
)
$output = Join-Path $root "Build\Managed"
$intermediate = Join-Path $root "Build\Intermediates\Managed\"
$assembly = Join-Path $output "Keire.Managed.dll"
$editorAssembly = Join-Path $output "Keire.Editor.Managed.dll"
$generatorAssembly = Join-Path $output "Keire.Managed.Generators.dll"
$designTimeOutput = Join-Path $output "DesignTime"
$designTimeAssembly = Join-Path $designTimeOutput "Keire.Managed.dll"
$designTimeEditorAssembly = Join-Path $designTimeOutput "Keire.Editor.Managed.dll"
$requiredOutputs = @($assembly, $editorAssembly, $generatorAssembly, $designTimeAssembly, $designTimeEditorAssembly)

if (@($requiredOutputs | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -eq 0) {
    $assemblyWriteTime = $requiredOutputs | ForEach-Object {
        (Get-Item -LiteralPath $_).LastWriteTimeUtc
    } | Sort-Object | Select-Object -First 1
    $managedInputs = @()
    foreach ($sourceRoot in $sourceRoots) {
        $managedInputs += @((Get-Item -LiteralPath $sourceRoot))
        $managedInputs += @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force | Where-Object {
                $_.FullName -notmatch '[\\/](?:bin|obj|Build)(?:[\\/]|$)' -and
                ($_.PSIsContainer -or $_.Extension -in @(".cs", ".csproj", ".props", ".targets", ".md"))
            })
    }
    $newerInput = $managedInputs | Where-Object {
        $_.LastWriteTimeUtc -gt $assemblyWriteTime
    } | Select-Object -First 1
    $wrapperIsNewer = (Get-Item -LiteralPath $PSCommandPath).LastWriteTimeUtc -gt $assemblyWriteTime -or
        (Get-Item -LiteralPath (Join-Path $root 'Config/Dependencies.lock')).LastWriteTimeUtc -gt $assemblyWriteTime
    if (-not $newerInput -and -not $wrapperIsNewer) {
        Write-Host "==> Managed runtime API is current"
        $global:LASTEXITCODE = 0
        return
    }
}

if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "The locked Editor .NET SDK is missing: $dotnet. Run Scripts/Windows/dotnet.ps1 first."
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

& $dotnet build $designTimeProject --nologo --configuration Release --output $designTimeOutput
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

foreach ($requiredOutput in $requiredOutputs) {
    if (-not (Test-Path -LiteralPath $requiredOutput -PathType Leaf)) {
        throw "The managed build did not produce: $requiredOutput"
    }
}
foreach ($requiredOutput in $requiredOutputs) {
    (Get-Item -LiteralPath $requiredOutput).LastWriteTimeUtc = [DateTime]::UtcNow
}
$global:LASTEXITCODE = 0
