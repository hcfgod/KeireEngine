[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceHostRoot,
    [Parameter(Mandatory = $true)]
    [string] $SourceDistributionRoot,
    [Parameter(Mandatory = $true)]
    [string] $DestinationRoot,
    [string] $TaskName = 'Keire Distribution Host',
    [switch] $Resume,
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NormalizedDirectoryPrefix([string] $Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
}

function Assert-OutsideDirectory([string] $Candidate, [string] $Forbidden, [string] $Message) {
    $candidatePrefix = Get-NormalizedDirectoryPrefix $Candidate
    $forbiddenPrefix = Get-NormalizedDirectoryPrefix $Forbidden
    if ($candidatePrefix.StartsWith($forbiddenPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw $Message
    }
}

function Get-RequiredFile([string] $Root, [string] $RelativePath) {
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The Windows host source is missing '$RelativePath': '$path'."
    }
    return $path
}

function Get-RequiredDirectory([string] $Root, [string] $RelativePath) {
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "The Windows host source is missing '$RelativePath': '$path'."
    }
    return $path
}

function Stop-SourceProcess([string] $ExecutablePath) {
    $resolvedExecutable = [IO.Path]::GetFullPath($ExecutablePath)
    foreach ($process in Get-Process -ErrorAction SilentlyContinue) {
        try {
            if ([string]::Equals($process.Path, $resolvedExecutable, [StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                $process.WaitForExit(15000) | Out-Null
            }
        }
        catch [System.ComponentModel.Win32Exception] {
            continue
        }
    }
}

function Set-ProtectedHostAcl([string] $Root) {
    & icacls.exe $Root /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' `
        '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' /Q | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to set the protected host root ACL: '$Root'."
    }

    & icacls.exe (Join-Path $Root '*') /reset /T /C /Q | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to propagate the protected host ACL below '$Root'."
    }
}

$sourceHost = [IO.Path]::GetFullPath($SourceHostRoot)
$sourceDistribution = [IO.Path]::GetFullPath($SourceDistributionRoot)
$destination = [IO.Path]::GetFullPath($DestinationRoot)
if (-not (Test-Path -LiteralPath $sourceHost -PathType Container)) {
    throw "The source host root does not exist: '$sourceHost'."
}
if (-not (Test-Path -LiteralPath $sourceDistribution -PathType Container)) {
    throw "The source distribution root does not exist: '$sourceDistribution'."
}

Assert-OutsideDirectory $destination $sourceHost 'The destination may not be inside the source host root.'
Assert-OutsideDirectory $destination $sourceDistribution 'The destination may not be inside the source distribution root.'
Assert-OutsideDirectory $sourceHost $destination 'The source host root may not be inside the destination.'
Assert-OutsideDirectory $sourceDistribution $destination 'The source distribution root may not be inside the destination.'
$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
Assert-OutsideDirectory $destination $userProfile `
    'The pre-login host destination must be outside every interactive user profile.'

$settingsPath = Get-RequiredFile $sourceHost 'host-settings.json'
$serviceExecutable = Get-RequiredFile $sourceHost 'KeireDistributionService.exe'
$caddyExecutable = Get-RequiredFile $sourceHost 'caddy.exe'
Get-RequiredFile $sourceHost 'Caddyfile' | Out-Null
foreach ($relativePath in @('Website', 'scripts', 'tools')) {
    Get-RequiredDirectory $sourceHost $relativePath | Out-Null
}
Get-RequiredFile $sourceHost 'scripts\start-windows-host.ps1' | Out-Null
Get-RequiredFile $sourceHost 'scripts\install-windows-startup-task.ps1' | Out-Null
Get-RequiredDirectory $sourceDistribution 'snapshots' | Out-Null
Get-RequiredFile $sourceDistribution 'current' | Out-Null

$sourceSettings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
foreach ($requiredSetting in @('schemaVersion', 'host', 'httpPort', 'httpsPort')) {
    if ($null -eq $sourceSettings.PSObject.Properties[$requiredSetting]) {
        throw "The source host settings are missing '$requiredSetting'."
    }
}
if ([int] $sourceSettings.schemaVersion -ne 1) {
    throw "Unsupported Windows host settings schema '$($sourceSettings.schemaVersion)'."
}

if ($ValidateOnly) {
    Write-Host "Windows host migration inputs are valid for '$destination'."
    exit 0
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Migrating the Windows host and installing its pre-login task requires an elevated PowerShell session.'
}
if ((Test-Path -LiteralPath $destination) -and -not $Resume) {
    throw "The destination already exists; refusing to merge or overwrite it: '$destination'."
}
if ($Resume -and -not (Test-Path -LiteralPath $destination -PathType Container)) {
    throw "The requested migration destination does not exist to resume: '$destination'."
}
$destinationParent = Split-Path -Parent $destination
[IO.Directory]::CreateDirectory($destinationParent) | Out-Null
$staging = Join-Path $destinationParent ('.{0}.staging-{1}' -f (Split-Path -Leaf $destination),
    [Guid]::NewGuid().ToString('N'))
$oldTaskXml = $null
$oldTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($oldTask) {
    $oldTaskXml = Export-ScheduledTask -TaskName $TaskName
}

try {
    if (-not $Resume) {
        [IO.Directory]::CreateDirectory($staging) | Out-Null
        foreach ($directoryName in @('Website', 'scripts', 'tools')) {
            Copy-Item -LiteralPath (Join-Path $sourceHost $directoryName) -Destination $staging -Recurse
        }
        $previewDownloads = Join-Path $sourceHost 'PreviewDownloads'
        if (Test-Path -LiteralPath $previewDownloads -PathType Container) {
            Copy-Item -LiteralPath $previewDownloads -Destination $staging -Recurse
        }
        foreach ($fileName in @('KeireDistributionService.exe', 'caddy.exe', 'Caddyfile')) {
            Copy-Item -LiteralPath (Join-Path $sourceHost $fileName) -Destination $staging
        }
        foreach ($optionalFileName in @('KeireDistributionService.pdb', 'release-public-key.json', 'README.md')) {
            $optionalFile = Join-Path $sourceHost $optionalFileName
            if (Test-Path -LiteralPath $optionalFile -PathType Leaf) {
                Copy-Item -LiteralPath $optionalFile -Destination $staging
            }
        }

        $stagedDistribution = Join-Path $staging 'DistributionRoot'
        [IO.Directory]::CreateDirectory($stagedDistribution) | Out-Null
        Copy-Item -LiteralPath (Join-Path $sourceDistribution 'snapshots') -Destination $stagedDistribution -Recurse
        Copy-Item -LiteralPath (Join-Path $sourceDistribution 'current') -Destination $stagedDistribution
        [IO.Directory]::CreateDirectory((Join-Path $staging 'Logs')) | Out-Null

        $newSettings = [ordered]@{
            schemaVersion = 1
            host = [string] $sourceSettings.host
            storageRoot = 'DistributionRoot'
            httpPort = [int] $sourceSettings.httpPort
            httpsPort = [int] $sourceSettings.httpsPort
            serviceExecutable = 'KeireDistributionService.exe'
            caddyExecutable = 'caddy.exe'
            caddyConfig = 'Caddyfile'
            logDirectory = 'Logs'
        }
        [IO.File]::WriteAllText(
            (Join-Path $staging 'host-settings.json'),
            ($newSettings | ConvertTo-Json) + [Environment]::NewLine,
            [Text.UTF8Encoding]::new($false))

        $stagedSettings = Join-Path $staging 'host-settings.json'
        & (Join-Path $staging 'scripts\start-windows-host.ps1') -SettingsPath $stagedSettings -ValidateOnly
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged Windows host failed settings validation.'
        }
        $stagedPublisher = @(
            (Join-Path $staging 'tools\publisher\KeireDistributionPublisher.exe'),
            (Join-Path $staging 'tools\publisher\KeireDistributionPublisher')
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if (-not $stagedPublisher) {
            throw 'The staged host does not contain its packaged distribution validator.'
        }
        & $stagedPublisher validate --root $stagedDistribution
        if ($LASTEXITCODE -ne 0) {
            throw 'The staged distribution root failed validation.'
        }

        Move-Item -LiteralPath $staging -Destination $destination
    }

    Set-ProtectedHostAcl $destination
    Copy-Item -LiteralPath $PSCommandPath `
        -Destination (Join-Path $destination 'scripts\migrate-windows-host.ps1') -Force

    $destinationSettings = Join-Path $destination 'host-settings.json'
    & (Join-Path $destination 'scripts\start-windows-host.ps1') `
        -SettingsPath $destinationSettings -ValidateOnly
    if ($LASTEXITCODE -ne 0) {
        throw 'The protected Windows host failed settings validation.'
    }
    $destinationDistribution = Join-Path $destination 'DistributionRoot'
    $destinationPublisher = @(
        (Join-Path $destination 'tools\publisher\KeireDistributionPublisher.exe'),
        (Join-Path $destination 'tools\publisher\KeireDistributionPublisher')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $destinationPublisher) {
        throw 'The protected host does not contain its packaged distribution validator.'
    }
    & $destinationPublisher validate --root $destinationDistribution
    if ($LASTEXITCODE -ne 0) {
        throw 'The protected distribution root failed validation.'
    }

    if ($oldTask) {
        Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    }
    Stop-SourceProcess $caddyExecutable
    Stop-SourceProcess $serviceExecutable

    $installer = Join-Path $destination 'scripts\install-windows-startup-task.ps1'
    & $installer -SettingsPath $destinationSettings -TaskName $TaskName
    if ($LASTEXITCODE -ne 0) {
        throw 'The migrated Windows host startup task installation failed.'
    }

    $healthCheck = Join-Path $destination 'scripts\health-check.ps1'
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    do {
        try {
            & $healthCheck -BaseUrl ("https://{0}" -f $sourceSettings.host) -TimeoutSeconds 5
            if ($LASTEXITCODE -eq 0) {
                Write-Host "Windows distribution host migrated to '$destination' and is publicly ready."
                exit 0
            }
        }
        catch {
            Start-Sleep -Seconds 2
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The migrated Windows host did not become publicly ready within 90 seconds.'
}
catch {
    Stop-SourceProcess (Join-Path $destination 'caddy.exe')
    Stop-SourceProcess (Join-Path $destination 'KeireDistributionService.exe')
    if ($oldTaskXml) {
        try {
            Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
            Register-ScheduledTask -TaskName $TaskName -Xml $oldTaskXml -Force | Out-Null
            Start-ScheduledTask -TaskName $TaskName
        }
        catch {
            Write-Warning "Automatic startup-task rollback failed: $($_.Exception.Message)"
        }
    }
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    throw
}
