[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $repository 'Services/KeireDistributionService/scripts/PlatformMigrationBackup.psm1') -Force
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-backup-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$json = '{"timestamp":"2026-10-19T10:21:53.931108Z","largeInteger":9223372036854775807,"name":"Kéire"}'
$plain = [Text.Encoding]::UTF8.GetBytes($json)
$digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($plain))
$part = Join-Path $fixture 'database-export-0000.dpapi'
$protected = [Security.Cryptography.ProtectedData]::Protect($plain,
    [Text.Encoding]::UTF8.GetBytes('Keire.DatabaseMigration.v1'),
    [Security.Cryptography.DataProtectionScope]::CurrentUser)
[IO.File]::WriteAllBytes($part, $protected)

function Assert-Rejected([scriptblock] $Action)
{
    $rejected = $false
    try
    {
        & $Action | Out-Null
    }
    catch
    {
        $rejected = $true
    }
    if (-not $rejected)
    {
        throw 'The malformed backup was accepted.'
    }
}

try
{
    $arguments = @{ Directory = $fixture; Kind = 'database'; ExpectedSha256 = $digest; ExpectedParts = 1 }
    if ((Read-PlatformMigrationBackup @arguments) -cne $json)
    {
        throw 'Round trip changed the original UTF-8, timestamp, or integer representation.'
    }
    $arguments.ExpectedParts = 2
    Assert-Rejected { Read-PlatformMigrationBackup @arguments }
    $arguments.ExpectedParts = 1
    $arguments.ExpectedSha256 = '0' * 64
    Assert-Rejected { Read-PlatformMigrationBackup @arguments }
    $arguments.ExpectedSha256 = $digest
    [IO.File]::WriteAllBytes($part, [byte[]]@(1, 2, 3, 4))
    Assert-Rejected { Read-PlatformMigrationBackup @arguments }
    [IO.File]::WriteAllBytes($part, $protected)
    Move-Item -LiteralPath $part -Destination (Join-Path $fixture 'database-export-0001.dpapi')
    Assert-Rejected { Read-PlatformMigrationBackup @arguments }
    Write-Output 'PASS: exact round trip, missing part, wrong digest, corruption, and numbering checks.'
}
finally
{
    [Array]::Clear($plain, 0, $plain.Length)
    $resolved = [IO.Path]::GetFullPath($fixture)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'keire-backup-test-*')
    {
        throw 'Refusing cleanup outside the temporary fixture.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
