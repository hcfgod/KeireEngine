[CmdletBinding()]
param(
    [string] $BaseUrl = 'http://127.0.0.1:5088',
    [ValidateRange(1, 120)]
    [int] $TimeoutSeconds = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$uri = ([Uri]::new([Uri]$BaseUrl, '/health/ready')).AbsoluteUri
$response = Invoke-RestMethod -Method Get -Uri $uri -TimeoutSec $TimeoutSeconds
if ($response.status -notin @('ready', 'ready-degraded')) {
    throw "Distribution service is not ready: '$($response.status)'."
}

Write-Host "Distribution service is $($response.status); snapshot $($response.snapshot)."
