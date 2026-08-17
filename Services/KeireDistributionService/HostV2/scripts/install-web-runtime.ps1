[CmdletBinding()]
param(
    [string] $WebRoot = '',
    [string] $NodeExecutable = 'node',
    [string] $NpmExecutable = 'npm'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($WebRoot)) {
    $WebRoot = Join-Path $PSScriptRoot '..\Web'
}
$WebRoot = [IO.Path]::GetFullPath($WebRoot)
foreach ($requiredFile in @('package.json', 'package-lock.json', 'dist\server\entry.mjs')) {
    $path = Join-Path $WebRoot $requiredFile
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The packaged web runtime is missing '$path'."
    }
}

$nodeVersion = (& $NodeExecutable -p 'process.versions.node').Trim()
if ($LASTEXITCODE -ne 0 -or [version] $nodeVersion -lt [version] '22.12.0') {
    throw "Kéire Web requires Node.js 22.12.0 or newer; found '$nodeVersion'."
}

$npmVersion = (& $NpmExecutable --version).Trim()
if ($LASTEXITCODE -ne 0 -or [version] $npmVersion -lt [version] '10.8.2') {
    throw "Kéire Web requires npm 10.8.2 or newer; found '$npmVersion'."
}

& $NpmExecutable --prefix $WebRoot ci --omit=dev --ignore-scripts --no-audit --no-fund
if ($LASTEXITCODE -ne 0) {
    throw 'The locked Kéire Web runtime dependency installation failed.'
}
& $NodeExecutable --check (Join-Path $WebRoot 'dist\server\entry.mjs')
if ($LASTEXITCODE -ne 0) {
    throw 'The packaged Kéire Web server entry point is invalid.'
}

Write-Host "Kéire Web runtime dependencies are installed in '$WebRoot'."
