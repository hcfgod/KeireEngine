[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string[]]$InputLibraries
)

$ErrorActionPreference = "Stop"

if ($InputLibraries.Count -eq 0) { throw "At least one input library is required." }
$resolvedInputs = foreach ($inputLibrary in $InputLibraries) {
    if (-not (Test-Path -LiteralPath $inputLibrary -PathType Leaf)) {
        throw "Static library input does not exist: $inputLibrary"
    }
    (Resolve-Path -LiteralPath $inputLibrary).Path
}

$librarian = Get-Command lib.exe -ErrorAction SilentlyContinue
if (-not $librarian) { throw "The Microsoft librarian was not found in the active tool environment." }

$outputPath = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
$temporary = "$outputPath.$([guid]::NewGuid().ToString('N')).tmp.lib"
try {
    & $librarian.Source /NOLOGO "/OUT:$temporary" @resolvedInputs
    if ($LASTEXITCODE -ne 0) { throw "Static library merge failed with exit code $LASTEXITCODE." }
    Move-Item -LiteralPath $temporary -Destination $outputPath -Force
}
finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}
