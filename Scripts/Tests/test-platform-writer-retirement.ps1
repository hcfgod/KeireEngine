$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot '../../Services/KeireDistributionService/scripts/retire-platform-writer.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-writer-retirement-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$markerPath = Join-Path $fixture '.keire-platform-writer.lock'
function Write-Marker([int] $OwnerProcess) {
    @{ schemaVersion = 1; processId = $OwnerProcess; owner = [Guid]::NewGuid().ToString() } |
        ConvertTo-Json | Set-Content -LiteralPath $markerPath
}
function Assert-Rejected([int] $OwnerProcess) {
    $before = [IO.File]::ReadAllText($markerPath)
    $rejected = $false
    try {
        & $helper -SpoolDirectory $fixture -StoppedProcessId $OwnerProcess
    } catch {
        $rejected = $true
    }
    if (-not $rejected -or [IO.File]::ReadAllText($markerPath) -cne $before) {
        throw 'Retirement failed to preserve rejected marker.'
    }
}
try {
    Write-Marker $PID
    Assert-Rejected $PID
    Assert-Rejected 2147483647
    Write-Marker 2147483647
    $contents = [IO.File]::ReadAllText($markerPath)
    & $helper -SpoolDirectory $fixture -StoppedProcessId 2147483647
    $archives = @(Get-ChildItem -LiteralPath $fixture -Force -Filter '*.retired-*')
    if ((Test-Path -LiteralPath $markerPath) -or $archives.Count -ne 1 -or
        [IO.File]::ReadAllText($archives[0].FullName) -cne $contents) {
        throw 'Stopped marker was not preserved in its archive.'
    }
    & $helper -SpoolDirectory $fixture -StoppedProcessId 2147483647
    Set-Content -LiteralPath $markerPath -Value '{invalid-json'
    Assert-Rejected 2147483647
    Write-Host 'Writer retirement checks passed: live owner, wrong owner, archive, missing marker, malformed marker.'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $prefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
