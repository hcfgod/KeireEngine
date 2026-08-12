[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Validator,

    [Parameter(Mandatory = $true)]
    [string]$AssetTool,

    [Parameter(Mandatory = $true)]
    [string]$MalwareScanner,

    [Parameter(Mandatory = $true)]
    [string]$Dotnet,

    [string]$Attestation
)

$ErrorActionPreference = "Stop"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell session."
}

$programs = [ordered]@{
    "Keire Marketplace Validator - Offline Worker" = $Validator
    "Keire Marketplace Validator - Asset Tool" = $AssetTool
    "Keire Marketplace Validator - Malware Scanner" = $MalwareScanner
    "Keire Marketplace Validator - Managed Compiler" = $Dotnet
}
$attestedPrograms = @()

foreach ($entry in $programs.GetEnumerator()) {
    $resolved = (Resolve-Path -LiteralPath $entry.Value -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Validator firewall target is not a file: $resolved"
    }

    $existing = Get-NetFirewallRule -DisplayName $entry.Key -ErrorAction SilentlyContinue
    if ($null -eq $existing) {
        New-NetFirewallRule -DisplayName $entry.Key -Direction Outbound -Action Block -Enabled True `
            -Profile Any -Program $resolved | Out-Null
    }
    else {
        $existing | Set-NetFirewallRule -Direction Outbound -Action Block -Enabled True -Profile Any | Out-Null
        $existing | Get-NetFirewallApplicationFilter | Set-NetFirewallApplicationFilter -Program $resolved | Out-Null
    }

    $rules = @(Get-NetFirewallRule -DisplayName $entry.Key -ErrorAction Stop)
    if ($rules.Count -ne 1) {
        throw "Validator firewall rule is missing or ambiguous after configuration: $($entry.Key)"
    }
    $rule = $rules[0]
    $application = $rule | Get-NetFirewallApplicationFilter
    if ($rule.Enabled -ne "True" -or $rule.Direction -ne "Outbound" -or $rule.Action -ne "Block" -or
        -not [string]::Equals($application.Program, $resolved, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Validator firewall rule failed post-configuration verification: $($entry.Key)"
    }

    $attestedPrograms += [ordered]@{
        displayName = $entry.Key
        path = $resolved
        sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

if (-not [string]::IsNullOrWhiteSpace($Attestation)) {
    $attestationPath = [IO.Path]::GetFullPath($Attestation)
    $parent = Split-Path -Parent $attestationPath
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "The firewall attestation parent directory does not exist: $parent"
    }

    $document = [ordered]@{
        schemaVersion = 1
        generatedAtUtc = [DateTimeOffset]::UtcNow.ToString("O", [Globalization.CultureInfo]::InvariantCulture)
        programs = $attestedPrograms
    }
    $temporary = Join-Path $parent (".{0}.{1}.tmp" -f ([IO.Path]::GetFileName($attestationPath)),
        [Guid]::NewGuid().ToString("N"))
    try {
        [IO.File]::WriteAllText($temporary, ($document | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $attestationPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }

    $security = [Security.AccessControl.FileSecurity]::new()
    $security.SetAccessRuleProtection($true, $false)
    $rules = @(
        @([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::LocalSystemSid, $null),
            [Security.AccessControl.FileSystemRights]::FullControl),
        @([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null),
            [Security.AccessControl.FileSystemRights]::FullControl),
        @([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::LocalServiceSid, $null),
            [Security.AccessControl.FileSystemRights]::Read)
    )
    foreach ($rule in $rules) {
        $security.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
                $rule[0], $rule[1], [Security.AccessControl.AccessControlType]::Allow))
    }
    Set-Acl -LiteralPath $attestationPath -AclObject $security
}

Write-Host "Configured and verified outbound-deny rules for the validator and every untrusted-content child process."
