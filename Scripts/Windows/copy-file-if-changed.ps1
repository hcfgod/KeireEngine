[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "generated-content-cache.ps1")

$sourceFile = Get-Item -LiteralPath $Source
if ($sourceFile.PSIsContainer) {
    throw "Copy source must be a file: $Source"
}
if (($sourceFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Copy source must not be a reparse point: $Source"
}

$destinationPath = [IO.Path]::GetFullPath($Destination)
$destinationFile = Get-Item -LiteralPath $destinationPath -ErrorAction SilentlyContinue
if ($destinationFile) {
    if ($destinationFile.PSIsContainer) {
        throw "Copy destination must be a file: $Destination"
    }
    if (($destinationFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Copy destination must not be a reparse point: $Destination"
    }
    if ($destinationFile.Length -eq $sourceFile.Length -and
        (Get-GeneratedContentFileHash -Path $destinationFile.FullName) -eq
        (Get-GeneratedContentFileHash -Path $sourceFile.FullName)) {
        return
    }
}

$destinationDirectory = Split-Path -Parent $destinationPath
[IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
$temporary = "$destinationPath.$([Guid]::NewGuid().ToString('N')).tmp"
try {
    Copy-Item -LiteralPath $sourceFile.FullName -Destination $temporary
    Move-Item -LiteralPath $temporary -Destination $destinationPath -Force
}
finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}
