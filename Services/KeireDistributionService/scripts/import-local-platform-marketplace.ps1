[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $BackupDirectory,
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $ResultPath,
    [ValidateSet('marketplace-core', 'marketplace-access')] [string] $Section = 'marketplace-core',
    [switch] $CreateSchema,
    [switch] $VerifyOnly,
    [switch] $AcceptanceOnly
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PlatformMigrationBackup.psm1') -Force
$settingsBytes = $null
$child = $null
try
{
    if (($CreateSchema -and ($VerifyOnly -or $AcceptanceOnly)) -or ($VerifyOnly -and $AcceptanceOnly))
    {
        throw 'Schema creation cannot be combined with read-only verification.'
    }
    foreach ($path in @($BackupDirectory, $SettingsDirectory, $ResultPath))
    {
        if (-not [IO.Path]::IsPathFullyQualified($path))
        {
            throw 'Migration paths must be absolute.'
        }
    }
    if (Test-Path -LiteralPath $ResultPath)
    {
        throw 'Use a new result path to preserve previous migration evidence.'
    }
    $databaseDigest = '06b919023460f377210e1eabcec008256a5353d1d6299791d92392f1695c8373'
    $schemaDigest = '0efce2823c55c4097628e51097eee3f9d5bce335e3ab48395f7db08191f3c753'
    $rawSnapshot = Read-PlatformMigrationBackup -Directory $BackupDirectory -Kind database `
        -ExpectedSha256 $databaseDigest -ExpectedParts 21
    $rawSchema = Read-PlatformMigrationBackup -Directory $BackupDirectory -Kind schema `
        -ExpectedSha256 $schemaDigest -ExpectedParts 35
    $settingsBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'mysql-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $request = @{
        section = $Section
        settings = ([Text.Encoding]::UTF8.GetString($settingsBytes) | ConvertFrom-Json)
        rawSnapshot = $rawSnapshot
        rawSchema = $rawSchema
        expectedDigest = $databaseDigest
        schemaDigest = $schemaDigest
    } | ConvertTo-Json -Depth 100 -Compress
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command node.exe).Source)
    $start.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../DocumentationSite'))
    $start.ArgumentList.Add('scripts/import-platform-marketplace.mjs')
    if ($CreateSchema)
    {
        $start.ArgumentList.Add('--create-schema')
    }
    if ($VerifyOnly)
    {
        $start.ArgumentList.Add('--verify-only')
    }
    if ($AcceptanceOnly)
    {
        $start.ArgumentList.Add('--acceptance-only')
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
        throw 'Migration timed out; inspect the isolated schema and import ledger before retrying.'
    }
    if ($child.ExitCode -ne 0)
    {
        $failure = $errors.GetAwaiter().GetResult() | ConvertFrom-Json
        @{ status = 'failed'; stage = $failure.stage; code = $failure.code; constraintCode = $failure.constraintCode } | ConvertTo-Json |
            Set-Content -LiteralPath $ResultPath -Encoding utf8
        throw "Marketplace migration failed at '$($failure.stage)' ($($failure.code)); inspect before retrying."
    }
    $report = $output.GetAwaiter().GetResult()
    $report | ConvertFrom-Json | Out-Null
    $report | Set-Content -LiteralPath $ResultPath -Encoding utf8
    Write-Output $report
}
finally
{
    if ($null -ne $settingsBytes)
    {
        [Array]::Clear($settingsBytes, 0, $settingsBytes.Length)
    }
    $request = $null
    $rawSnapshot = $null
    $rawSchema = $null
    if ($null -ne $child)
    {
        $child.Dispose()
    }
}
