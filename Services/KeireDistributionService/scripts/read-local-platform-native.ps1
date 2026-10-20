[CmdletBinding()]
param ([Parameter(Mandatory)] [string] $SettingsDirectory)

# Private subprocess protocol; stdout must only reach the host's private pipe.
$ErrorActionPreference = 'Stop'
$plainBytes = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Use an absolute settings directory.'
    }
    $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'native-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.NativeIdentity.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    [Console]::Out.Write([Text.Encoding]::UTF8.GetString($plainBytes))
}
catch
{
    [Console]::Error.WriteLine('Protected native identity settings could not be loaded.')
    exit 1
}
finally
{
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
}
