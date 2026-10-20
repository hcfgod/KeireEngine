[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [ValidatePattern('^[a-f0-9]{32}$')] [string] $FixtureId,
    [Parameter(Mandatory)] [string] $BootstrapCredentialPath,
    [Parameter(Mandatory)] [string] $BootstrapEntropy,
    [Parameter(Mandatory)] [string] $CertificateAuthorityPath,
    [string] $MySqlExecutable = 'C:/Program Files/MySQL/MySQL Server 8.0/bin/mysql.exe'
)

$ErrorActionPreference = 'Stop'
$credentialBytes = $null
$credential = $null
$child = $null
try
{
    foreach ($inputPath in @($BootstrapCredentialPath, $CertificateAuthorityPath, $MySqlExecutable))
    {
        if (-not [IO.Path]::IsPathFullyQualified($inputPath) -or -not (Test-Path -LiteralPath $inputPath -PathType Leaf))
        {
            throw 'QA cleanup requires existing absolute configuration paths.'
        }
    }
    $credentialBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes($BootstrapCredentialPath),
        [Text.Encoding]::UTF8.GetBytes($BootstrapEntropy),
        [Security.Cryptography.DataProtectionScope]::LocalMachine)
    $credential = [Text.Encoding]::UTF8.GetString($credentialBytes) | ConvertFrom-Json
    if ($credential.username -notmatch '^[A-Za-z0-9_]{1,32}$' -or [string]::IsNullOrEmpty($credential.password))
    {
        throw 'Protected QA cleanup credential is invalid.'
    }
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
    $output = $child.StandardOutput.ReadToEndAsync()
    $errors = $child.StandardError.ReadToEndAsync()
    $tables = @("qa_publications_$FixtureId", "qa_jobs_$FixtureId", "qa_counter_$FixtureId")
    foreach ($table in $tables)
    {
        $child.StandardInput.WriteLine("DROP TABLE IF EXISTS keire_platform.$table;")
    }
    $child.StandardInput.WriteLine("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='keire_platform' AND table_name IN ('$($tables -join "','")');")
    $child.StandardInput.Close()
    if (-not $child.WaitForExit(30000))
    {
        $child.Kill($true)
        throw 'QA table cleanup timed out.'
    }
    if ($child.ExitCode -ne 0 -or $output.GetAwaiter().GetResult().Trim() -ne '0')
    {
        throw 'QA table cleanup could not be confirmed; private diagnostics suppressed.'
    }
    Write-Output '{"status":"publication-fixtures-removed"}'
}
finally
{
    if ($null -ne $credentialBytes)
    {
        [Array]::Clear($credentialBytes)
    }
    $credential = $null
    if ($null -ne $child)
    {
        $child.Dispose()
    }
}
