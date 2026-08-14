[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Validator,

    [Parameter(Mandatory = $true)]
    [string]$ExchangeRoot,

    [Parameter(Mandatory = $true)]
    [string]$WorkRoot,

    [Parameter(Mandatory = $true)]
    [string]$AssetTool,

    [Parameter(Mandatory = $true)]
    [string]$MalwareScanner,

    [Parameter(Mandatory = $true)]
    [string]$Dotnet,

    [Parameter(Mandatory = $true)]
    [string]$ManagedApi,

    [Parameter(Mandatory = $true)]
    [string]$ProtectedAttestationKey,

    [string]$FirewallAttestation,

    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Security

foreach ($path in @($Validator, $AssetTool, $MalwareScanner, $Dotnet, $ManagedApi, $ProtectedAttestationKey)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required validator file is missing: $path"
    }
}

$attestationKeyPath = (Resolve-Path -LiteralPath $ProtectedAttestationKey).Path
$attestationKeyAcl = Get-Acl -LiteralPath $attestationKeyPath
$expectedKeyAccess = @{
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::LocalSystemSid, $null).Value) =
        [int][Security.AccessControl.FileSystemRights]::FullControl
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null).Value) =
        [int][Security.AccessControl.FileSystemRights]::FullControl
    ([Security.Principal.SecurityIdentifier]::new(
            [Security.Principal.WellKnownSidType]::LocalServiceSid, $null).Value) =
        [int]([Security.AccessControl.FileSystemRights]::Read -bor
            [Security.AccessControl.FileSystemRights]::Synchronize)
}
if (-not $attestationKeyAcl.AreAccessRulesProtected -or
    @($attestationKeyAcl.Access).Count -ne $expectedKeyAccess.Count) {
    throw "The validator attestation key does not have its exact LOCAL SERVICE ACL."
}
foreach ($entry in $expectedKeyAccess.GetEnumerator()) {
    $matches = @($attestationKeyAcl.Access | Where-Object {
            $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq $entry.Key
        })
    if ($matches.Count -ne 1 -or $matches[0].IsInherited -or
        $matches[0].AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
        [int]$matches[0].FileSystemRights -ne $entry.Value) {
        throw "The validator attestation key does not have its exact LOCAL SERVICE ACL."
    }
}
foreach ($path in @($ExchangeRoot, $WorkRoot)) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required validator directory is missing: $path"
    }
}

$requiredRules = [ordered]@{
    "Keire Marketplace Validator - Offline Worker" = (Resolve-Path -LiteralPath $Validator).Path
    "Keire Marketplace Validator - Asset Tool" = (Resolve-Path -LiteralPath $AssetTool).Path
    "Keire Marketplace Validator - Malware Scanner" = (Resolve-Path -LiteralPath $MalwareScanner).Path
    "Keire Marketplace Validator - Managed Compiler" = (Resolve-Path -LiteralPath $Dotnet).Path
}
$firewall = Get-Service -Name "MpsSvc" -ErrorAction Stop
if ($firewall.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
    throw "Windows Defender Firewall must be running before the offline validator starts."
}

if ([string]::IsNullOrWhiteSpace($FirewallAttestation)) {
    foreach ($entry in $requiredRules.GetEnumerator()) {
        $rules = @(Get-NetFirewallRule -DisplayName $entry.Key -ErrorAction SilentlyContinue)
        if ($rules.Count -ne 1) {
            throw "Required outbound-deny firewall rule is missing or ambiguous: $($entry.Key)"
        }
        $rule = $rules[0]
        $application = $rule | Get-NetFirewallApplicationFilter
        if ($rule.Enabled -ne "True" -or $rule.Direction -ne "Outbound" -or $rule.Action -ne "Block" -or
            -not [string]::Equals($application.Program, $entry.Value, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Required outbound-deny firewall rule is misconfigured: $($entry.Key)"
        }
    }
}
else {
    if (-not (Test-Path -LiteralPath $FirewallAttestation -PathType Leaf)) {
        throw "The firewall attestation is missing: $FirewallAttestation"
    }
    $attestationPath = (Resolve-Path -LiteralPath $FirewallAttestation).Path
    $attestationFile = Get-Item -LiteralPath $attestationPath -Force
    if (($attestationFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $attestationFile.Length -le 0 -or $attestationFile.Length -gt 65536) {
        throw "The firewall attestation is unsafe or exceeds its size limit."
    }
    $attestationAcl = Get-Acl -LiteralPath $attestationPath
    if (-not $attestationAcl.AreAccessRulesProtected) {
        throw "The firewall attestation inherits filesystem permissions."
    }
    $expectedAccess = @{
        ([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::LocalSystemSid, $null).Value) =
            [int][Security.AccessControl.FileSystemRights]::FullControl
        ([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::BuiltinAdministratorsSid, $null).Value) =
            [int][Security.AccessControl.FileSystemRights]::FullControl
        ([Security.Principal.SecurityIdentifier]::new(
                [Security.Principal.WellKnownSidType]::LocalServiceSid, $null).Value) =
            [int]([Security.AccessControl.FileSystemRights]::Read -bor
                [Security.AccessControl.FileSystemRights]::Synchronize)
    }
    if (@($attestationAcl.Access).Count -ne $expectedAccess.Count) {
        throw "The firewall attestation ACL has an unexpected rule count."
    }
    foreach ($entry in $expectedAccess.GetEnumerator()) {
        $matches = @($attestationAcl.Access | Where-Object {
                $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -eq $entry.Key
            })
        if ($matches.Count -ne 1 -or $matches[0].IsInherited -or
            $matches[0].AccessControlType -ne [Security.AccessControl.AccessControlType]::Allow -or
            [int]$matches[0].FileSystemRights -ne $entry.Value) {
            throw "The firewall attestation ACL does not match its exact least-privilege contract."
        }
    }

    $attestation = Get-Content -LiteralPath $attestationPath -Raw | ConvertFrom-Json
    if ($attestation.schemaVersion -ne 1 -or @($attestation.programs).Count -ne $requiredRules.Count) {
        throw "The firewall attestation has an unsupported schema or program count."
    }
    foreach ($entry in $requiredRules.GetEnumerator()) {
        $matches = @($attestation.programs | Where-Object { $_.displayName -ceq $entry.Key })
        if ($matches.Count -ne 1) {
            throw "The firewall attestation is missing an exact program entry: $($entry.Key)"
        }
        $attested = $matches[0]
        if (-not [string]::Equals($attested.path, $entry.Value, [StringComparison]::OrdinalIgnoreCase) -or
            $attested.sha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw "The firewall attestation does not match the configured program: $($entry.Key)"
        }
        $actualHash = (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash.ToLowerInvariant()
        if (-not [string]::Equals($actualHash, $attested.sha256, [StringComparison]::Ordinal)) {
            throw "A firewall-attested validator program has changed: $($entry.Key)"
        }
    }
}

if ($ValidateOnly) {
    Write-Host "Windows offline marketplace validator inputs and firewall isolation are valid."
    exit 0
}

$env:KEIRE_VALIDATOR_NETWORK_ISOLATED = "1"
$protectedBytes = [IO.File]::ReadAllBytes($attestationKeyPath)
$privateBytes = $null
try {
    $privateBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        $protectedBytes,
        [Text.Encoding]::UTF8.GetBytes("KeireMarketplaceValidatorAttestation/v1"),
        [Security.Cryptography.DataProtectionScope]::LocalMachine)
    $privateKey = [Text.Encoding]::UTF8.GetString($privateBytes)
    if ($privateKey.Length -lt 40 -or $privateKey.Length -gt 32768 -or
        $privateKey -cnotmatch '^[A-Za-z0-9+/]+={0,2}$') {
        throw "The protected validator attestation private key has an invalid shape."
    }
    $env:KEIRE_VALIDATOR_ATTESTATION_PRIVATE_KEY = $privateKey
    & $Validator watch --exchange-root $ExchangeRoot --work-root $WorkRoot --asset-tool $AssetTool `
        --malware-scanner $MalwareScanner --dotnet $Dotnet --managed-api $ManagedApi
    exit $LASTEXITCODE
}
finally {
    Remove-Item Env:KEIRE_VALIDATOR_ATTESTATION_PRIVATE_KEY -ErrorAction SilentlyContinue
    if ($null -ne $privateBytes) { [Array]::Clear($privateBytes, 0, $privateBytes.Length) }
    [Array]::Clear($protectedBytes, 0, $protectedBytes.Length)
}
