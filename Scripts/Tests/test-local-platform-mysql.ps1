[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$provision = Join-Path $repository 'Services/KeireDistributionService/scripts/initialize-local-platform-mysql.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-mysql-guards-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$credentialPath = Join-Path $fixture 'bootstrap.dpapi'
$certificatePath = Join-Path $fixture 'ca.pem'
$existingOutput = Join-Path $fixture 'existing'
$newOutput = Join-Path $fixture 'new'
[IO.Directory]::CreateDirectory($existingOutput) | Out-Null
[IO.File]::WriteAllText($certificatePath, 'not a real certificate; no server is contacted')
$plain = [Text.Encoding]::UTF8.GetBytes('{"username":"invalid username","password":"fixture-only"}')
try
{
    [IO.File]::WriteAllBytes($credentialPath, [Security.Cryptography.ProtectedData]::Protect(
        $plain, [Text.Encoding]::UTF8.GetBytes('Keire.Test.Bootstrap'),
        [Security.Cryptography.DataProtectionScope]::LocalMachine))
}
finally
{
    [Array]::Clear($plain, 0, $plain.Length)
}

$arguments = @{
    BootstrapCredentialPath = $credentialPath
    BootstrapEntropy = 'Keire.Test.Bootstrap'
    CertificateAuthorityPath = $certificatePath
    MySqlExecutable = [Environment]::ProcessPath
}

function Assert-Refused([string] $Output, [string] $ExpectedMessage)
{
    $refused = $false
    try
    {
        & $provision @arguments -OutputDirectory $Output
    }
    catch
    {
        if ($_.Exception.Message -notlike $ExpectedMessage)
        {
            throw
        }
        $refused = $true
    }
    if (-not $refused)
    {
        throw 'Unsafe provisioning input was not rejected.'
    }
}

try
{
    $originalCredentialHash = (Get-FileHash -LiteralPath $credentialPath).Hash
    Assert-Refused $existingOutput 'Use a new absolute output directory*'
    Assert-Refused 'relative-output' 'Use a new absolute output directory*'
    Assert-Refused $newOutput 'The protected bootstrap credential has an invalid format*'
    if ((Test-Path -LiteralPath $newOutput) -or
        (Get-FileHash -LiteralPath $credentialPath).Hash -ne $originalCredentialHash -or
        @(Get-ChildItem -LiteralPath $existingOutput -Force).Count -ne 0)
    {
        throw 'Rejected input changed the credential or destination.'
    }
    Write-Output 'PASS: existing output, relative output, and malformed credential rejected without mutation.'
}
finally
{
    # Only remove this test's exact, freshly created temporary directory.
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedFixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike 'keire-mysql-guards-*')
    {
        throw 'Refusing cleanup outside the test fixture.'
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
