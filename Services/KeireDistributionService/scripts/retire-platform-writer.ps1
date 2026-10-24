[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $SpoolDirectory,
    [int] $StoppedProcessId = 0
)

$ErrorActionPreference = 'Stop'
if (-not [IO.Path]::IsPathRooted($SpoolDirectory)) {
    throw 'An absolute spool path is required.'
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
if ($StoppedProcessId -le 0) {
    $processIdIsInteger = $marker.processId -is [int] -or $marker.processId -is [long]
    if ($marker.schemaVersion -ne 1 -or -not $processIdIsInteger -or
        $marker.processId -le 0 -or $marker.processId -gt [int]::MaxValue) {
        throw 'Writer marker does not contain a valid stopped process identity.'
    }
    $StoppedProcessId = [int] $marker.processId
}
if (Get-Process -Id $StoppedProcessId -ErrorAction SilentlyContinue) {
    throw 'The writer process still exists; refusing to retire its marker.'
}
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
