[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $Directory
)

$ErrorActionPreference = 'Stop'
try
{
    if (-not [IO.Path]::IsPathFullyQualified($Directory) -or $Directory.StartsWith('\\'))
    {
        throw 'Upload storage must be an absolute local directory.'
    }
    $item = Get-Item -LiteralPath $Directory -Force
    if (-not $item.PSIsContainer)
    {
        throw 'Upload storage must be a directory.'
    }
    $ancestor = $item
    while ($null -ne $ancestor)
    {
        if (($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw 'Upload storage must not traverse a reparse point.'
        }
        $ancestor = $ancestor.Parent
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $allowed = @($identity.User.Value, 'S-1-5-18', 'S-1-5-32-544')
    $acl = Get-Acl -LiteralPath $item.FullName
    $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
    if ($owner -notin $allowed)
    {
        throw 'Upload storage has an untrusted owner.'
    }
    $rules = $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])
    foreach ($rule in $rules)
    {
        if ($rule.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow -and
            $rule.IdentityReference.Value -notin $allowed)
        {
            throw 'Upload storage grants access to other accounts.'
        }
    }
    @{ privateDirectory = $true } | ConvertTo-Json -Compress
}
catch
{
    Write-Error 'Private upload directory validation failed. Configure a local directory restricted to the service account, SYSTEM, and administrators.'
    exit 1
}
