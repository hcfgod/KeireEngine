[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BaseUrl,
    [string] $NotificationWebhook = '',
    [string] $StatePath = '',
    [int] $TimeoutSeconds = 10,
    [int] $IntervalSeconds = 60,
    [switch] $Once
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 60 -or $IntervalSeconds -lt 15 -or $IntervalSeconds -gt 3600) {
    throw 'Monitor timeout or interval is outside the supported range.'
}
$base = [Uri] $BaseUrl
if ($base.Scheme -ne 'https' -or -not [string]::IsNullOrEmpty($base.UserInfo)) {
    throw 'The external monitor requires an HTTPS base URL without embedded credentials.'
}
$healthUri = ([Uri]::new($base, '/health/ready')).AbsoluteUri
if ([string]::IsNullOrWhiteSpace($StatePath)) {
    $StatePath = Join-Path $PSScriptRoot 'monitor-state.json'
}
$StatePath = [IO.Path]::GetFullPath($StatePath)
[IO.Directory]::CreateDirectory((Split-Path -Parent $StatePath)) | Out-Null

function Read-PreviousState {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        return $null
    }
    try {
        $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        if ($state.status -in @('healthy', 'unhealthy')) {
            return [string] $state.status
        }
    }
    catch {
        return $null
    }
    return $null
}

function Write-State([string] $Status, [string] $Message) {
    $document = [ordered]@{
        schemaVersion = 1
        status = $Status
        checkedAt = [DateTimeOffset]::UtcNow.ToString('o')
        url = $healthUri
        message = $Message
    } | ConvertTo-Json -Compress
    $temporary = "$StatePath.tmp-$PID"
    [IO.File]::WriteAllText($temporary, "$document`n", [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $StatePath -Force
}

function Send-Notification([string] $Status, [string] $Message) {
    if ([string]::IsNullOrWhiteSpace($NotificationWebhook)) {
        return
    }
    $payload = [ordered]@{
        source = 'keire-distribution-monitor'
        event = 'availability-transition'
        status = $Status
        checkedAt = [DateTimeOffset]::UtcNow.ToString('o')
        url = $healthUri
        message = $Message
    } | ConvertTo-Json -Compress
    Invoke-RestMethod -Uri $NotificationWebhook -Method Post -ContentType 'application/json' `
        -Body $payload -TimeoutSec $TimeoutSeconds | Out-Null
}

do {
    $status = 'unhealthy'
    $message = ''
    try {
        $response = Invoke-RestMethod -Uri $healthUri -Method Get -TimeoutSec $TimeoutSeconds
        if ($response.status -notin @('ready', 'ready-degraded')) {
            throw "Unexpected readiness status '$($response.status)'."
        }
        $status = 'healthy'
        $message = "Distribution origin reports $($response.status)."
    }
    catch {
        $message = $_.Exception.Message
    }

    $previous = Read-PreviousState
    Write-State $status $message
    if ($previous -ne $status) {
        Send-Notification $status $message
    }
    Write-Host "$([DateTimeOffset]::UtcNow.ToString('o')) $status $message"
    if (-not $Once) {
        Start-Sleep -Seconds $IntervalSeconds
    }
} while (-not $Once)

if ($status -ne 'healthy') {
    exit 1
}
