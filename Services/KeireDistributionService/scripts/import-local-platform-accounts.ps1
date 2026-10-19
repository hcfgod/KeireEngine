[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $BackupDirectory,
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $ResultPath,
    [switch] $CreateSchema,
    [switch] $VerifyOnly
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PlatformMigrationBackup.psm1') -Force
$settingsBytes = $null
$key = $null
$request = $null
$child = $null
try
{
    if ($CreateSchema -and $VerifyOnly)
    {
        throw 'Choose schema creation or read-only verification, not both.'
    }
    foreach ($directory in @($BackupDirectory, $SettingsDirectory))
    {
        if (-not [IO.Path]::IsPathFullyQualified($directory))
        {
            throw 'Migration directories must be absolute paths.'
        }
    }
    if (-not [IO.Path]::IsPathFullyQualified($ResultPath) -or (Test-Path -LiteralPath $ResultPath))
    {
        throw 'Use a new absolute result path to preserve previous import evidence.'
    }
    $settingsBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'mysql-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $key = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'account-record-key.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.AccountEncryption.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $manifest = Get-Content -LiteralPath (Join-Path $BackupDirectory 'database-backup-manifest.json') -Raw |
        ConvertFrom-Json
    $raw = Read-PlatformMigrationBackup -Directory $BackupDirectory -Kind database `
        -ExpectedSha256 $manifest.Sha256 -ExpectedParts $manifest.Parts
    $request = @{
        settings = ([Text.Encoding]::UTF8.GetString($settingsBytes) | ConvertFrom-Json)
        rawSnapshot = $raw
        expectedDigest = $manifest.Sha256
        encryptionKey = [Convert]::ToBase64String($key)
    } | ConvertTo-Json -Depth 100 -Compress
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command node.exe).Source)
    $start.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../DocumentationSite'))
    $start.ArgumentList.Add('scripts/import-platform-accounts.mjs')
    if ($CreateSchema)
    {
        $start.ArgumentList.Add('--create-schema')
    }
    if ($VerifyOnly)
    {
        $start.ArgumentList.Add('--verify-only')
    }
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    $child = [Diagnostics.Process]::Start($start)
    $output = $child.StandardOutput.ReadToEndAsync()
    $errors = $child.StandardError.ReadToEndAsync()
    $child.StandardInput.Write($request)
    $child.StandardInput.Close()
    if (-not $child.WaitForExit(60000))
    {
        $child.Kill($true)
        throw 'Import timed out; inspect the isolated database before retrying.'
    }
    $report = $output.GetAwaiter().GetResult()
    if ($child.ExitCode -ne 0)
    {
        # The importer emits a JSON stage record without account data or driver diagnostics.
        $failure = $errors.GetAwaiter().GetResult() | ConvertFrom-Json
        @{ status = 'failed'; stage = $failure.stage } | ConvertTo-Json |
            Set-Content -LiteralPath $ResultPath -Encoding utf8
        throw "Account import failed during stage '$($failure.stage)'. Inspect before retrying."
    }
    $report | ConvertFrom-Json | Out-Null
    $report | Set-Content -LiteralPath $ResultPath -Encoding utf8
    Write-Output $report
}
finally
{
    foreach ($bytes in @($settingsBytes, $key))
    {
        if ($null -ne $bytes)
        {
            [Array]::Clear($bytes, 0, $bytes.Length)
        }
    }
    $raw = $null
    $request = $null
    if ($null -ne $child)
    {
        $child.Dispose()
    }
}
