[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$DestinationDirectory,
    [string]$Filter = "*"
)

$ErrorActionPreference = "Stop"

$sourceRoot = Resolve-Path -LiteralPath $SourceDirectory
$files = @(Get-ChildItem -LiteralPath $sourceRoot -Filter $Filter -File)
if (-not $files) {
    throw "No files matching '$Filter' were found beneath $sourceRoot."
}
foreach ($file in $files) {
    & (Join-Path $PSScriptRoot "copy-file-if-changed.ps1") -Source $file.FullName `
        -Destination (Join-Path $DestinationDirectory $file.Name)
}
