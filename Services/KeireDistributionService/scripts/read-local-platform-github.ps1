[CmdletBinding()]
param ([Parameter(Mandatory)] [string] $SettingsDirectory)

# Private subprocess protocol. Invoke only with stdout connected to the host's private pipe.
$ErrorActionPreference = 'Stop'
$plainBytes = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Use an absolute settings directory.'
    }
    $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'github-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.GitHub.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    [Console]::Out.Write([Text.Encoding]::UTF8.GetString($plainBytes))
}
catch
{
    [Console]::Error.WriteLine('Protected GitHub settings could not be loaded.')
    exit 1
}
finally
{
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
}
