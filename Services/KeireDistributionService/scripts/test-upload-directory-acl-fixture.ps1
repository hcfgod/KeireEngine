$ErrorActionPreference = 'Stop'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('keire-upload-acl-' + [Guid]::NewGuid().ToString('N'))
$verificationScript = Join-Path $PSScriptRoot 'test-private-upload-directory.ps1'
try
{
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($sid)
    $acl.SetAccessRuleProtection($true, $false)
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $rule = [Security.AccessControl.FileSystemAccessRule]::new(
        $sid, 'FullControl', $inheritance, 'None', 'Allow')
    $acl.AddAccessRule($rule)
    Set-Acl -LiteralPath $fixtureRoot -AclObject $acl
    $result = & (Get-Process -Id $PID).Path -NoProfile -NonInteractive -File $verificationScript -Directory $fixtureRoot
    if ($LASTEXITCODE -ne 0 -or -not ($result | ConvertFrom-Json).privateDirectory)
    {
        throw 'The private fixture was rejected.'
    }
    $modulePath = Join-Path $PSScriptRoot '../DocumentationSite/Source/lib/platform-upload-directory.mjs'
    $nodeCheck = @'
import { pathToFileURL } from "node:url";
const [modulePath, directory, powerShellExecutable, verificationScriptPath] = process.argv.slice(1);
const { verifyPrivateUploadDirectory } = await import(pathToFileURL(modulePath).href);
await verifyPrivateUploadDirectory(directory, { powerShellExecutable, verificationScriptPath });
'@
    & node --input-type=module -e $nodeCheck $modulePath $fixtureRoot (Get-Process -Id $PID).Path $verificationScript
    if ($LASTEXITCODE -ne 0)
    {
        throw 'The Node startup check rejected the private fixture.'
    }
    & icacls.exe $fixtureRoot /grant '*S-1-1-0:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw 'Could not configure the shared-directory rejection fixture.'
    }
    & (Get-Process -Id $PID).Path -NoProfile -NonInteractive -File $verificationScript -Directory $fixtureRoot 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0)
    {
        throw 'The shared fixture was accepted.'
    }
    Write-Output '{"status":"upload-directory-acl-passed","checks":3}'
}
finally
{
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    $parent = [IO.Path]::GetDirectoryName($resolved).TrimEnd('\')
    if ($parent -ne [IO.Path]::GetTempPath().TrimEnd('\') -or
        -not [IO.Path]::GetFileName($resolved).StartsWith('keire-upload-acl-'))
    {
        throw 'Refusing cleanup outside the temporary fixture directory.'
    }
    if (Test-Path -LiteralPath $resolved)
    {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
