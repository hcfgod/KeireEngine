[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $SiteOrigin
)

$ErrorActionPreference = 'Stop'
$plainBytes = $null
$child = $null
$temporaryPath = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Use an absolute settings directory.'
    }
    # Require a dedicated new directory: never change ACLs on an existing user directory.
    if (Test-Path -LiteralPath $SettingsDirectory)
    {
        throw 'Download key setup already exists.'
    }
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command node.exe).Source)
    $start.ArgumentList.Add([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../DocumentationSite/scripts/generate-download-settings.mjs')))
    $start.ArgumentList.Add($SiteOrigin)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $child = [Diagnostics.Process]::Start($start)
    $output = $child.StandardOutput.ReadToEndAsync()
    $errors = $child.StandardError.ReadToEndAsync()
    if (-not $child.WaitForExit(15000))
    {
        $child.Kill($true)
        throw 'Key generation timed out.'
    }
    if ($child.ExitCode -ne 0)
    {
        throw 'Key generation failed.'
    }
    $plainBytes = [Text.Encoding]::UTF8.GetBytes($output.GetAwaiter().GetResult())
    $settings = [Text.Encoding]::UTF8.GetString($plainBytes) | ConvertFrom-Json
    if ($settings.schemaVersion -ne 1 -or $settings.siteOrigin -cne $SiteOrigin -or
        $settings.downloadKey -cnotmatch '^[A-Za-z0-9+/]{43}=$')
    {
        throw 'Generated configuration is invalid.'
    }
    [IO.Directory]::CreateDirectory($SettingsDirectory) | Out-Null
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($identity)
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($principal in @($identity, [Security.Principal.SecurityIdentifier]::new('S-1-5-18')))
    {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $principal, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    Set-Acl -LiteralPath $SettingsDirectory -AclObject $acl
    $protectedBytes = [Security.Cryptography.ProtectedData]::Protect(
        $plainBytes, [Text.Encoding]::UTF8.GetBytes('Keire.Platform.Downloads.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $temporaryPath = Join-Path $SettingsDirectory ([Guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllBytes($temporaryPath, $protectedBytes)
    [IO.File]::Move($temporaryPath, (Join-Path $SettingsDirectory 'download-settings.dpapi'), $false)
    $temporaryPath = $null
    @{ status = 'download-key-protected' } | ConvertTo-Json -Compress
}
catch
{
    # Never forward child output or parser errors containing key material.
    [Console]::Error.WriteLine('Download key setup failed; existing settings were not replaced. Inspect the dedicated setup directory before retrying.')
    exit 1
}
finally
{
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
    $settings = $null
    $output = $null
    if ($null -ne $child)
    {
        $child.Dispose()
    }
}
