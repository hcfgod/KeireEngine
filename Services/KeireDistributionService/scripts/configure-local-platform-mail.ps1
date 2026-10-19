[CmdletBinding()]
param
(
    [string] $SettingsDirectory = (Join-Path $env:LOCALAPPDATA 'Keire/PlatformMail')
)

$ErrorActionPreference = 'Stop'
$securePassword = $null
$passwordPointer = [IntPtr]::Zero
$plainBytes = $null
$temporaryPath = $null
try
{
    if (-not [IO.Path]::IsPathFullyQualified($SettingsDirectory))
    {
        throw 'Settings directory must be absolute.'
    }

    $destination = Join-Path $SettingsDirectory 'smtp-settings.dpapi'
    if (Test-Path -LiteralPath $destination)
    {
        throw 'Mail settings already exist. This setup does not overwrite an existing credential.'
    }

    Write-Host 'Kéire website email setup'
    Write-Host 'Create an app password at https://myaccount.google.com/apppasswords'
    Write-Host 'The password is masked and saved encrypted for this Windows account. No email is sent by setup.'
    $email = (Read-Host 'Google email address used to send verification codes').Trim()
    $address = [Net.Mail.MailAddress]::new($email)
    if ($address.Address -cne $email -or $email -match '[\r\n]' -or $email.Length -gt 254)
    {
        throw 'Enter a plain email address without a display name.'
    }

    $securePassword = Read-Host 'Paste the Google app password (not your normal password)' -AsSecureString
    $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
    $appPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer).Replace(' ', '')
    if ($appPassword -cnotmatch '^[a-zA-Z0-9]{16}$')
    {
        throw 'The Google app password must contain 16 letters or digits, optionally separated by spaces.'
    }

    [IO.Directory]::CreateDirectory($SettingsDirectory) | Out-Null
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($identity)
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($principal in @($identity, [Security.Principal.SecurityIdentifier]::new('S-1-5-18')))
    {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $principal, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $SettingsDirectory -AclObject $acl

    $settings = @{
        schemaVersion = 1
        provider = 'gmail'
        host = 'smtp.gmail.com'
        port = 465
        secure = $true
        username = $email
        fromAddress = $email
        password = $appPassword
    }
    $plainBytes = [Text.Encoding]::UTF8.GetBytes(($settings | ConvertTo-Json -Compress))
    $protectedBytes = [Security.Cryptography.ProtectedData]::Protect(
        $plainBytes,
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.Smtp.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $temporaryPath = Join-Path $SettingsDirectory ([Guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllBytes($temporaryPath, $protectedBytes)
    [IO.File]::Move($temporaryPath, $destination, $false)
    $temporaryPath = $null
    Write-Host 'Email credentials saved encrypted. Delivery and sign-in integration still need validation.' -ForegroundColor Green
}
catch
{
    # Never print exceptions from credential parsing or persistence: they may contain submitted values.
    Write-Host 'Setup did not save new credentials. Check the email/app password and whether settings already exist.' -ForegroundColor Red
}
finally
{
    if ($passwordPointer -ne [IntPtr]::Zero)
    {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
    }
    if ($null -ne $plainBytes)
    {
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
    }
    if ($null -ne $securePassword)
    {
        $securePassword.Dispose()
    }
    $appPassword = $null
    $settings = $null
    if ($temporaryPath -and (Test-Path -LiteralPath $temporaryPath))
    {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}

Read-Host 'Press Enter to close this setup window' | Out-Null
