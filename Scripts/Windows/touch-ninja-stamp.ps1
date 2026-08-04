[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"
$absolute = [IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
$directory = Split-Path -Parent $absolute
New-Item -ItemType Directory -Force -Path $directory | Out-Null
New-Item -ItemType File -Force -Path $absolute | Out-Null
