[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "generated-content-cache.ps1")

function Test-PinnedDotnetSdk {
    param([string]$Directory, [string]$Version)
    $executable = Join-Path $Directory "dotnet.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $Directory "sdk\$Version") -PathType Container)) { return $false }
    Push-Location $Directory
    try {
        $actual = ((& $executable --version 2>$null) -join '').Trim()
        return $LASTEXITCODE -eq 0 -and $actual -eq $Version
    }
    catch { return $false }
    finally { Pop-Location }
}

function Install-PinnedDotnetSdk {
    param([string]$Directory, [string]$Version, [string]$Url, [string]$Hash)
    if ($Version -notmatch '^\d+\.\d+\.\d+$' -or $Hash -notmatch '^[a-fA-F0-9]{128}$') {
        throw "The locked Windows .NET SDK version or SHA512 is invalid."
    }
    if (Test-PinnedDotnetSdk $Directory $Version) { return }
    if (Test-Path -LiteralPath $Directory) {
        throw "The pinned .NET SDK directory is incomplete. Preserve or move it before retrying: $Directory"
    }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $Directory))
    $destination = [IO.Path]::GetFullPath($Directory)
    if ([IO.Path]::GetDirectoryName($destination) -ne $parent) { throw "Invalid SDK destination." }
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $temporary = Join-Path $parent (".sdk-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    try {
        $archive = Join-Path $temporary "sdk.zip"
        $payload = Join-Path $temporary "payload"
        Invoke-WebRequest -Uri $Url -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ine $Hash) {
            throw "The pinned .NET SDK archive checksum does not match Config/Dependencies.lock."
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $payload
        if (-not (Test-PinnedDotnetSdk $payload $Version)) {
            throw "The downloaded .NET SDK does not provide the locked version $Version."
        }
        Move-Item -LiteralPath $payload -Destination $destination
    }
    finally {
        $resolvedTemporary = [IO.Path]::GetFullPath($temporary)
        if ([IO.Path]::GetDirectoryName($resolvedTemporary) -eq $parent -and
            [IO.Path]::GetFileName($resolvedTemporary).StartsWith('.sdk-')) {
            Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

$Root = Get-RepositoryRoot
$Lock = Get-DependencyLock
$Version = $Lock.DOTNET_SDK_VERSION
$architectureKey = if ((Get-NativeArchitecture) -eq 'ARM64') { 'ARM64' } else { 'X86_64' }
$directory = Join-Path $Root "Build\Tools\dotnet\$Version"
$sdkLock = Enter-GeneratedContentLock -Name "pinned-dotnet-sdk" -RepositoryRoot $Root
try {
    Install-PinnedDotnetSdk -Directory $directory -Version $Version `
        -Url $Lock."DOTNET_WINDOWS_${architectureKey}_URL" -Hash $Lock."DOTNET_WINDOWS_${architectureKey}_SHA512"
    Write-Host "==> Pinned .NET SDK $Version is ready"
}
finally { Exit-GeneratedContentLock -Mutex $sdkLock }
