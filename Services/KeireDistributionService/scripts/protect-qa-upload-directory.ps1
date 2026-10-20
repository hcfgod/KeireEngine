[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $Directory
)
$ErrorActionPreference = 'Stop'
$resolved = [IO.Path]::GetFullPath($Directory)
if ([IO.Path]::GetDirectoryName($resolved).TrimEnd('\') -ne [IO.Path]::GetTempPath().TrimEnd('\') -or
    -not [IO.Path]::GetFileName($resolved).StartsWith('keire-http-upload-'))
{
    throw 'The upload fixture must be a newly created temporary QA directory.'
}
$item = Get-Item -LiteralPath $resolved -Force
if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
    @(Get-ChildItem -LiteralPath $resolved -Force).Count -ne 0)
{
    throw 'The upload fixture must be an empty ordinary directory.'
}
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetOwner($sid)
$acl.SetAccessRuleProtection($true, $false)
$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
    $sid, 'FullControl', 'ContainerInherit, ObjectInherit', 'None', 'Allow'))
Set-Acl -LiteralPath $resolved -AclObject $acl
