[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $BootstrapCredentialPath,
    [Parameter(Mandatory)] [string] $BootstrapEntropy,
    [Parameter(Mandatory)] [string] $CertificateAuthorityPath,
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string] $MySqlExecutable = 'C:/Program Files/MySQL/MySQL Server 8.0/bin/mysql.exe'
)

$ErrorActionPreference = 'Stop'
$schemaName = 'keire_platform'
$runtimeUser = 'keire_platform_web'
$migrationUser = 'keire_platform_migrate'
$plainBootstrap = $null
$credential = $null
$stage = 'validate-inputs'

function Invoke-PrivateMySql([string] $Sql)
{
    $start = [Diagnostics.ProcessStartInfo]::new($MySqlExecutable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('--no-defaults', '--host=127.0.0.1', '--port=3306', '--protocol=TCP',
        '--ssl-mode=VERIFY_IDENTITY', "--ssl-ca=$CertificateAuthorityPath",
        "--user=$($credential.username)", '--batch', '--skip-column-names'))
    {
        $start.ArgumentList.Add($argument)
    }
    $start.Environment['MYSQL_PWD'] = $credential.password
    $child = [Diagnostics.Process]::Start($start)
    $start.Environment.Remove('MYSQL_PWD') | Out-Null
    try
    {
        $output = $child.StandardOutput.ReadToEndAsync()
        $errors = $child.StandardError.ReadToEndAsync()
        $child.StandardInput.Write($Sql)
        $child.StandardInput.Close()
        if (-not $child.WaitForExit(30000))
        {
            $child.Kill($true)
            throw 'MySQL operation timed out. Inspect the provisioning journal before retrying.'
        }
        if ($child.ExitCode -ne 0)
        {
            throw 'MySQL operation failed. Sensitive SQL and server diagnostics were suppressed.'
        }
        return $output.GetAwaiter().GetResult().Trim()
    }
    finally
    {
        $child.Dispose()
    }
}

function Save-ProvisioningState([string] $Status)
{
    [ordered]@{
        status = $Status
        stage = $stage
        schema = $schemaName
        runtimeUser = $runtimeUser
        migrationUser = $migrationUser
        recordedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'provisioning.json') -Encoding utf8
}

try
{
    foreach ($path in @($BootstrapCredentialPath, $CertificateAuthorityPath, $MySqlExecutable))
    {
        if (-not [IO.Path]::IsPathFullyQualified($path) -or -not (Test-Path -LiteralPath $path -PathType Leaf))
        {
            throw 'All input files must exist at absolute paths.'
        }
    }
    if (-not [IO.Path]::IsPathFullyQualified($OutputDirectory) -or (Test-Path -LiteralPath $OutputDirectory))
    {
        throw 'Use a new absolute output directory. Existing provisioning must be inspected, never overwritten.'
    }

    $plainBootstrap = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes($BootstrapCredentialPath),
        [Text.Encoding]::UTF8.GetBytes($BootstrapEntropy),
        [Security.Cryptography.DataProtectionScope]::LocalMachine)
    $credential = [Text.Encoding]::UTF8.GetString($plainBootstrap) | ConvertFrom-Json
    if ($credential.username -notmatch '^[A-Za-z0-9_]{1,32}$' -or
        [string]::IsNullOrEmpty($credential.password))
    {
        throw 'The protected bootstrap credential has an invalid format.'
    }

    $stage = 'check-existing-identities'
    $existing = Invoke-PrivateMySql @"
SELECT (SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name = '$schemaName')
     + (SELECT COUNT(*) FROM mysql.user WHERE User IN ('$runtimeUser', '$migrationUser'));
"@
    if ($existing -ne '0')
    {
        throw 'A Kéire schema or account already exists. No existing schema or credentials will be adopted.'
    }

    [IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
    $directory = [IO.DirectoryInfo]::new($OutputDirectory)
    $acl = [IO.FileSystemAclExtensions]::GetAccessControl($directory)
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($rule in $acl.GetAccessRules($true, $false, [Security.Principal.SecurityIdentifier]))
    {
        $acl.RemoveAccessRuleSpecific($rule)
    }
    foreach ($sid in @([Security.Principal.WindowsIdentity]::GetCurrent().User,
        [Security.Principal.SecurityIdentifier]::new('S-1-5-18'),
        [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')))
    {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    [IO.FileSystemAclExtensions]::SetAccessControl($directory, $acl)

    $runtimePassword = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(48))
    $migrationPassword = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(48))
    $settings = @{
        host = '127.0.0.1'
        port = 3306
        database = $schemaName
        certificateAuthorityPath = $CertificateAuthorityPath
        runtime = @{ user = $runtimeUser; password = $runtimePassword }
        migration = @{ user = $migrationUser; password = $migrationPassword }
    }
    $settingsBytes = [Text.Encoding]::UTF8.GetBytes(($settings | ConvertTo-Json -Depth 5))
    try
    {
        # Persist recoverable credentials before any nontransactional MySQL DDL.
        $protected = [Security.Cryptography.ProtectedData]::Protect(
            $settingsBytes, [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
            [Security.Cryptography.DataProtectionScope]::CurrentUser)
        [IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'mysql-settings.dpapi'), $protected)
    }
    finally
    {
        [Array]::Clear($settingsBytes, 0, $settingsBytes.Length)
    }

    $stage = 'create-schema'
    Save-ProvisioningState 'in-progress'
    Invoke-PrivateMySql "CREATE DATABASE $schemaName CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;" | Out-Null
    $stage = 'create-migration-account'
    Save-ProvisioningState 'in-progress'
    Invoke-PrivateMySql @"
CREATE USER '$migrationUser'@'127.0.0.1' IDENTIFIED BY '$migrationPassword' REQUIRE SSL;
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES, CREATE VIEW, SHOW VIEW
ON $schemaName.* TO '$migrationUser'@'127.0.0.1';
"@ | Out-Null
    $stage = 'create-runtime-account'
    Save-ProvisioningState 'in-progress'
    Invoke-PrivateMySql @"
CREATE USER '$runtimeUser'@'127.0.0.1' IDENTIFIED BY '$runtimePassword' REQUIRE SSL;
GRANT SELECT, INSERT, UPDATE, DELETE ON $schemaName.* TO '$runtimeUser'@'127.0.0.1';
"@ | Out-Null
    $stage = 'completed'
    Save-ProvisioningState 'provisioned-empty'
    Write-Output 'Isolated Kéire database and scoped accounts created. No application data has been imported.'
}
catch
{
    if ($stage -notin @('validate-inputs', 'check-existing-identities') -and
        (Test-Path -LiteralPath $OutputDirectory -PathType Container))
    {
        Save-ProvisioningState 'failed-inspect-before-retry'
    }
    throw
}
finally
{
    $credential = $null
    $settings = $null
    $runtimePassword = $null
    $migrationPassword = $null
    if ($null -ne $plainBootstrap)
    {
        [Array]::Clear($plainBootstrap, 0, $plainBootstrap.Length)
    }
}
