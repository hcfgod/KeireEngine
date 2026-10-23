Set-StrictMode -Version Latest

function Get-KeireIdentityCatalog
{
    return @{
        mysql = @{ Directory = 'PlatformMySql'; Files = @(
            @{ Name = 'mysql-settings.dpapi'; Entropy = 'Keire.Platform.MySql.v1' },
            @{ Name = 'account-record-key.dpapi'; Entropy = 'Keire.Platform.AccountEncryption.v1' }) }
        mail = @{ Directory = 'PlatformMail'; Files = @(@{ Name = 'smtp-settings.dpapi'; Entropy = 'Keire.Platform.Smtp.v1' }) }
        native = @{ Directory = 'PlatformNativeIdentity'; Files = @(@{ Name = 'native-settings.dpapi'; Entropy = 'Keire.Platform.NativeIdentity.v1' }) }
        downloads = @{ Directory = 'PlatformDownloads'; Files = @(@{ Name = 'download-settings.dpapi'; Entropy = 'Keire.Platform.Downloads.v1' }) }
        github = @{ Directory = 'PlatformGitHub'; Files = @(@{ Name = 'github-settings.dpapi'; Entropy = 'Keire.Platform.GitHub.v1' }) }
        workers = @{ Directory = 'PlatformWorkers'; Files = @(@{ Name = 'worker-settings.dpapi'; Entropy = 'Keire.Platform.Workers.v1' }) }
    }
}

function Resolve-KeireIdentityPath([string] $Path)
{
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathFullyQualified($Path))
    {
        throw 'An absolute local filesystem path is required.'
    }
    $resolved = [IO.Path]::GetFullPath($Path)
    if ($resolved.StartsWith('\\') -or $resolved.Substring(2).Contains(':'))
    {
        throw 'Network paths and alternate data streams are not supported.'
    }
    $ancestor = $resolved
    while (-not [string]::IsNullOrEmpty($ancestor))
    {
        if (Test-Path -LiteralPath $ancestor)
        {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            {
                throw 'Identity migration paths must not traverse reparse points.'
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $resolved
}

function Assert-KeireIdentityAdministrator
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or $identity.User.Value -eq 'S-1-5-18')
    {
        throw 'Run the coordinator elevated as the original provisioning user, not SYSTEM.'
    }
}

function New-KeireIdentityPrivateDirectory([string] $Path)
{
    $resolved = Resolve-KeireIdentityPath $Path
    if (Test-Path -LiteralPath $resolved)
    {
        throw 'A new dedicated migration directory is required; existing directories are never overwritten.'
    }
    [IO.Directory]::CreateDirectory($resolved) | Out-Null
    $administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($administrators)
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($sid in @($administrators, [Security.Principal.SecurityIdentifier]::new('S-1-5-18')))
    {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
    }
    Set-Acl -LiteralPath $resolved -AclObject $acl
    Assert-KeireIdentityPrivatePath $resolved
}

function Assert-KeireIdentityPrivatePath([string] $Path)
{
    $resolved = Resolve-KeireIdentityPath $Path
    $acl = Get-Acl -LiteralPath $resolved
    $owner = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
    if ($owner -notin @('S-1-5-18', 'S-1-5-32-544'))
    {
        throw 'Migration material must be owned by SYSTEM or Administrators.'
    }
    $allowed = @()
    foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]))
    {
        if ($rule.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow)
        {
            if ($rule.IdentityReference.Value -notin @('S-1-5-18', 'S-1-5-32-544'))
            {
                throw 'Migration material grants access outside SYSTEM and Administrators.'
            }
            $allowed += $rule.IdentityReference.Value
        }
    }
    if ('S-1-5-18' -notin $allowed -or 'S-1-5-32-544' -notin $allowed)
    {
        throw 'Migration material must remain accessible to SYSTEM and Administrators.'
    }
}

function Write-KeireIdentityNewFile([string] $Path, [byte[]] $Bytes)
{
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try
    {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    }
    finally
    {
        $stream.Dispose()
    }
    $acl = Get-Acl -LiteralPath $Path
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    Set-Acl -LiteralPath $Path -AclObject $acl
    Assert-KeireIdentityPrivatePath $Path
}

function Get-KeireIdentityHash([string] $Path)
{
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-KeireIdentityPlan($InputPlan)
{
    if ($InputPlan.schemaVersion -ne 1 -or
        (($InputPlan.PSObject.Properties.Name | Sort-Object) -join ',') -cne 'schemaVersion,sources')
    {
        throw 'Unsupported migration plan.'
    }
    $catalog = Get-KeireIdentityCatalog
    $seen = @{}
    $entries = @()
    foreach ($source in @($InputPlan.sources))
    {
        if ((($source.PSObject.Properties.Name | Sort-Object) -join ',') -cne 'kind,sourceDirectory' -or
            $source.kind -isnot [string] -or -not $catalog.ContainsKey($source.kind) -or $seen.ContainsKey($source.kind))
        {
            throw 'Migration sources must be unique known settings kinds with an explicit directory.'
        }
        $seen[$source.kind] = $true
        $directory = Resolve-KeireIdentityPath $source.sourceDirectory
        foreach ($file in $catalog[$source.kind].Files)
        {
            $entries += [pscustomobject]@{
                kind = $source.kind
                sourcePath = Join-Path $directory $file.Name
                targetDirectory = $catalog[$source.kind].Directory
                filename = $file.Name
                entropy = $file.Entropy
            }
        }
    }
    if ($entries.Count -eq 0 -or $entries.Count -gt 7)
    {
        throw 'Select at least one protected settings group.'
    }
    return $entries
}

function Read-KeireIdentityManifest([string] $BridgeDirectory)
{
    Assert-KeireIdentityPrivatePath $BridgeDirectory
    $manifestPath = Join-Path $BridgeDirectory 'manifest.json'
    Assert-KeireIdentityPrivatePath $manifestPath
    if ((Get-Item -LiteralPath $manifestPath).Length -gt 65536)
    {
        throw 'Migration manifest is oversized.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.migrationId -cnotmatch '^[a-f0-9]{32}$' -or
        $manifest.sourceSid -cnotmatch '^S-1-5-21-[0-9-]+$' -or @($manifest.entries).Count -eq 0 -or @($manifest.entries).Count -gt 7)
    {
        throw 'Migration manifest is invalid.'
    }
    $catalog = Get-KeireIdentityCatalog
    $seen = @{}
    foreach ($entry in $manifest.entries)
    {
        if (-not $catalog.ContainsKey([string] $entry.kind))
        {
            throw 'Unknown protected setting in migration manifest.'
        }
        $file = @($catalog[$entry.kind].Files | Where-Object { $_.Name -ceq $entry.filename })
        if ($file.Count -ne 1 -or $entry.targetDirectory -cne $catalog[$entry.kind].Directory -or
            $entry.entropy -cne $file[0].Entropy -or $entry.bridgeFile -cnotmatch '^[a-f0-9]{32}\.bridge$' -or
            $entry.sourceHash -cnotmatch '^[a-f0-9]{64}$' -or $entry.bridgeHash -cnotmatch '^[a-f0-9]{64}$')
        {
            throw 'Invalid protected setting mapping.'
        }
        $key = "$($entry.kind)/$($entry.filename)"
        if ($seen.ContainsKey($key))
        {
            throw 'Duplicate protected setting mapping.'
        }
        $seen[$key] = $true
        Resolve-KeireIdentityPath $entry.sourcePath | Out-Null
        Assert-KeireIdentityPrivatePath (Join-Path $BridgeDirectory $entry.bridgeFile)
        if ((Get-Item -LiteralPath (Join-Path $BridgeDirectory $entry.bridgeFile)).Length -gt 1048576 -or
            (Get-KeireIdentityHash (Join-Path $BridgeDirectory $entry.bridgeFile)) -cne $entry.bridgeHash)
        {
            throw 'Encrypted bridge integrity check failed.'
        }
    }
    Resolve-KeireIdentityPath $manifest.destinationRoot | Out-Null
    foreach ($name in @('complete-platform-service-identity.ps1', 'platform-service-identity-common.psm1'))
    {
        $helper = Join-Path $BridgeDirectory $name
        Assert-KeireIdentityPrivatePath $helper
        if ((Get-KeireIdentityHash $helper) -cne $manifest.helperHashes.$name)
        {
            throw 'Protected migration helper integrity check failed.'
        }
    }
    return $manifest
}

function Get-KeireIdentityBridgeEntropy([string] $MigrationId, [string] $Kind, [string] $Filename)
{
    $catalog = Get-KeireIdentityCatalog
    if ($MigrationId -cnotmatch '^[a-f0-9]{32}$' -or -not $catalog.ContainsKey($Kind) -or
        @($catalog[$Kind].Files | Where-Object { $_.Name -ceq $Filename }).Count -ne 1)
    {
        throw 'Invalid identity bridge encryption purpose.'
    }
    return 'Keire.Platform.IdentityBridge.v1:{0}:{1}:{2}' -f $MigrationId, $Kind, $Filename
}

function Test-KeireIdentityBytesEqual([byte[]] $Left, [byte[]] $Right)
{
    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length)
    {
        return $false
    }
    $difference = 0
    for ($index = 0; $index -lt $Left.Length; $index++)
    {
        $difference = $difference -bor ($Left[$index] -bxor $Right[$index])
    }
    return $difference -eq 0
}

Export-ModuleMember -Function *-KeireIdentity*
