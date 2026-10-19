[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $SettingsDirectory
)

# This is a private child-process protocol. Its stdout must only go to the parent service's pipe.
$ErrorActionPreference = 'Stop'
$settingsBytes = $null
$keyBytes = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Protected settings directory must be absolute.'
    }
    $settingsBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'mysql-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $keyBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'account-record-key.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.AccountEncryption.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    if ($keyBytes.Length -ne 32)
    {
        throw 'Invalid protected account key.'
    }
    $settings = [Text.Encoding]::UTF8.GetString($settingsBytes) | ConvertFrom-Json
    # The runtime must never receive the separate migration account.
    @{
        settings = @{
            host = $settings.host
            port = $settings.port
            database = $settings.database
            certificateAuthorityPath = $settings.certificateAuthorityPath
            runtime = $settings.runtime
        }
        accountEncryptionKey = [Convert]::ToBase64String($keyBytes)
    } | ConvertTo-Json -Depth 8 -Compress | Write-Output
}
catch
{
    [Console]::Error.WriteLine('Protected platform runtime settings could not be loaded.')
    exit 1
}
finally
{
    foreach ($buffer in @($settingsBytes, $keyBytes))
    {
        if ($null -ne $buffer)
        {
            [Array]::Clear($buffer, 0, $buffer.Length)
        }
    }
    $settings = $null
}
