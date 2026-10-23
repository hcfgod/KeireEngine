[CmdletBinding()]
param ([Parameter(Mandatory)] [string] $BridgeDirectory)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'platform-service-identity-common.psm1') -Force
$plainBytes = $null
$verifiedBytes = $null
try
{
    if ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -cne 'S-1-5-18')
    {
        throw 'Only SYSTEM may complete this migration.'
    }
    $BridgeDirectory = Resolve-KeireIdentityPath $BridgeDirectory
    if (-not $BridgeDirectory.Equals([IO.Path]::GetFullPath($PSScriptRoot), [StringComparison]::OrdinalIgnoreCase))
    {
        throw 'Run only the protected receiver copied into the selected bridge.'
    }
    $manifest = Read-KeireIdentityManifest $BridgeDirectory
    New-KeireIdentityPrivateDirectory $manifest.destinationRoot
    $createdDirectories = @{}
    $files = @()
    foreach ($entry in $manifest.entries)
    {
        $directory = Join-Path $manifest.destinationRoot $entry.targetDirectory
        if (-not $createdDirectories.ContainsKey($directory))
        {
            New-KeireIdentityPrivateDirectory $directory
            $createdDirectories[$directory] = $true
        }
        $bridgeEntropy = Get-KeireIdentityBridgeEntropy $manifest.migrationId $entry.kind $entry.filename
        $plainBytes = [Security.Cryptography.ProtectedData]::Unprotect(
            [IO.File]::ReadAllBytes((Join-Path $BridgeDirectory $entry.bridgeFile)),
            [Text.Encoding]::UTF8.GetBytes($bridgeEntropy), [Security.Cryptography.DataProtectionScope]::LocalMachine)
        $entropy = [Text.Encoding]::UTF8.GetBytes($entry.entropy)
        $protectedBytes = [Security.Cryptography.ProtectedData]::Protect($plainBytes, $entropy,
            [Security.Cryptography.DataProtectionScope]::CurrentUser)
        $destination = Join-Path $directory $entry.filename
        Write-KeireIdentityNewFile $destination $protectedBytes
        $verifiedBytes = [Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($destination), $entropy,
            [Security.Cryptography.DataProtectionScope]::CurrentUser)
        if (-not (Test-KeireIdentityBytesEqual $verifiedBytes $plainBytes))
        {
            throw 'SYSTEM protected-settings roundtrip failed.'
        }
        Assert-KeireIdentityPrivatePath $destination
        $files += @{ relativePath = $entry.targetDirectory + '/' + $entry.filename
            ciphertextHash = Get-KeireIdentityHash $destination }
        [Array]::Clear($plainBytes, 0, $plainBytes.Length)
        [Array]::Clear($verifiedBytes, 0, $verifiedBytes.Length)
        $plainBytes = $null
        $verifiedBytes = $null
    }
    $receipt = @{ schemaVersion = 1; migrationId = $manifest.migrationId; identitySid = 'S-1-5-18'
        verified = $true; files = $files }
    $receiptBytes = [Text.Encoding]::UTF8.GetBytes(($receipt | ConvertTo-Json -Depth 6))
    Write-KeireIdentityNewFile (Join-Path $BridgeDirectory 'receipt.json') $receiptBytes
}
catch
{
    [Console]::Error.WriteLine('SYSTEM protected-settings migration failed. Original settings were not modified.')
    exit 1
}
finally
{
    foreach ($buffer in @($plainBytes, $verifiedBytes))
    {
        if ($null -ne $buffer)
        {
            [Array]::Clear($buffer, 0, $buffer.Length)
        }
    }
}
