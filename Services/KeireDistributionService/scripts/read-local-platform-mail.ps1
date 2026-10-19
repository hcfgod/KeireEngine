[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $SettingsDirectory
)

# Private child-process protocol: stdout must only go to the parent service's pipe.
$ErrorActionPreference = 'Stop'
$plainBytes = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Protected settings directory must be absolute.'
    }
    $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'smtp-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.Smtp.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    [Text.Encoding]::UTF8.GetString($plainBytes) | Write-Output
}
catch
{
    [Console]::Error.WriteLine('Protected email settings could not be loaded.')
    exit 1
}
finally
{
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
}
