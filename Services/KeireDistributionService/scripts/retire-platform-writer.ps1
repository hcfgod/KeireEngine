[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $SpoolDirectory,
    [Parameter(Mandatory)] [int] $StoppedProcessId
)

$ErrorActionPreference = 'Stop'
if ($StoppedProcessId -le 0 -or -not [IO.Path]::IsPathRooted($SpoolDirectory)) {
    throw 'An absolute spool path and stopped process identity are required.'
}
if (Get-Process -Id $StoppedProcessId -ErrorAction SilentlyContinue) {
    throw 'The writer process still exists; refusing to retire its marker.'
}
$markerPath = Join-Path $SpoolDirectory '.keire-platform-writer.lock'
if (-not (Test-Path -LiteralPath $markerPath)) {
    return
}
$markerFile = Get-Item -LiteralPath $markerPath -Force
if ($markerFile.Length -gt 4096 -or ($markerFile.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Unexpected writer marker file.'
}
$contents = [IO.File]::ReadAllText($markerPath)
$marker = $contents | ConvertFrom-Json
if ($marker.schemaVersion -ne 1 -or $marker.processId -ne $StoppedProcessId -or
    $marker.owner -notmatch '^[a-f0-9-]{36}$') {
    throw 'Writer marker does not belong to the stopped process.'
}
if ([IO.File]::ReadAllText($markerPath) -cne $contents -or
    (Get-Process -Id $StoppedProcessId -ErrorAction SilentlyContinue)) {
    throw 'Writer state changed during retirement.'
}
# The caller must keep the host supervisor stopped throughout this operation.
$archivePath = $markerPath + '.retired-' + [Guid]::NewGuid().ToString('N')
[IO.File]::Move($markerPath, $archivePath)
