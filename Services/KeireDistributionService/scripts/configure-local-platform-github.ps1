[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $SiteOrigin,
    [Parameter(Mandatory)] [string] $ClientId,
    [Security.SecureString] $ClientSecret
)

$ErrorActionPreference = 'Stop'
$plainBytes = $null
$secretBytes = $null
$secretPointer = [IntPtr]::Zero
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory) -or
        (Test-Path -LiteralPath $SettingsDirectory))
    {
        throw 'Use a new dedicated absolute settings directory.'
    }
    $origin = [Uri]::new($SiteOrigin)
    if ($origin.Scheme -cne 'https' -or $origin.GetLeftPart([UriPartial]::Authority) -cne $SiteOrigin -or
        $origin.UserInfo -ne '' -or $ClientId -cnotmatch '^[A-Za-z0-9_.-]{10,128}$')
    {
        throw 'Invalid GitHub application configuration.'
    }
    if ($null -eq $ClientSecret)
    {
        $ClientSecret = Read-Host 'GitHub OAuth client secret (hidden)' -AsSecureString
    }
    $secretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($ClientSecret)
    $secret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer)
    if ($secret -cnotmatch '^[A-Za-z0-9_-]{20,512}$')
    {
        throw 'Invalid GitHub application secret.'
    }
    $secretBytes = [Text.Encoding]::UTF8.GetBytes($secret)
    $settings = @{ schemaVersion = 1; siteOrigin = $SiteOrigin; clientId = $ClientId
        clientSecret = [Convert]::ToBase64String($secretBytes) }
    $plainBytes = [Text.Encoding]::UTF8.GetBytes(($settings | ConvertTo-Json -Compress))
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
        $plainBytes, [Text.Encoding]::UTF8.GetBytes('Keire.Platform.GitHub.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $temporaryPath = Join-Path $SettingsDirectory ([Guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllBytes($temporaryPath, $protectedBytes)
    [IO.File]::Move($temporaryPath, (Join-Path $SettingsDirectory 'github-settings.dpapi'), $false)
    @{ status = 'github-settings-protected' } | ConvertTo-Json -Compress
}
catch
{
    [Console]::Error.WriteLine('GitHub setup failed; existing settings were not replaced. Inspect the dedicated setup directory before retrying.')
    exit 1
}
finally
{
    if ($secretPointer -ne [IntPtr]::Zero)
    {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer)
    }
    foreach ($bytes in @($plainBytes, $secretBytes))
    {
        if ($null -ne $bytes)
        {
            [Array]::Clear($bytes, 0, $bytes.Length)
        }
    }
    $settings = $null
    $secret = $null
}
