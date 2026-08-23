[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$windows = Join-Path $root "Scripts\Windows"
$helperPath = Join-Path $windows "generated-content-cache.ps1"
$helperSource = Get-Content -LiteralPath $helperPath -Raw

function Assert-True {
    param([bool]$Condition, [string]$Message)

    if (-not $Condition) {
        throw "$Message failed."
    }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)

    try {
        & $Action
    }
    catch {
        return
    }
    throw "$Message did not throw."
}

Assert-True ($helperSource.Contains('[Threading.Mutex]::new') -and
             $helperSource.Contains('[TimeSpan]$Timeout = [TimeSpan]::FromMinutes(10)') -and
             $helperSource.Contains('[Security.Cryptography.SHA256]::Create()') -and
             -not $helperSource.Contains('Get-FileHash') -and
             $helperSource.Contains('keire-generated-v1')) `
    "Windows generated content uses portable, bounded inter-process caching"

$buildScript = Get-Content -LiteralPath (Join-Path $windows "build.ps1") -Raw
$copyScript = Join-Path $windows "copy-file-if-changed.ps1"
Assert-True ($buildScript.Contains('Enter-GeneratedContentLock -Name "native-build"') -and
             $buildScript.Contains('[TimeSpan]::FromHours(2)') -and
             $buildScript.Contains('Exit-KeireBuildLock -Mutex $BuildLock') -and
             $buildScript.Contains('copy-file-if-changed.ps1')) `
    "Windows launcher builds serialize shared checkout outputs"
Assert-True (Test-Path -LiteralPath $copyScript -PathType Leaf) "Content-aware runtime staging script exists"

foreach ($generatorName in @("builtin-shaders.ps1", "builtin-skinning.ps1", "builtin-vfx.ps1")) {
    $generator = Get-Content -LiteralPath (Join-Path $windows $generatorName) -Raw
    Assert-True ($generator.Contains('Get-GeneratedContentFingerprint') -and
                 $generator.Contains('Test-GeneratedContentCurrent') -and
                 $generator.Contains('Enter-GeneratedContentLock') -and
                 $generator.Contains('Write-GeneratedContentStamp')) `
        "$generatorName uses the shared incremental cache"
}

$fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-generated-cache-" + [Guid]::NewGuid().ToString("N"))
$job = $null
$mutex = $null
$lockedDestination = $null
try {
    New-Item -ItemType Directory -Force -Path $fixture | Out-Null
    $cacheInput = Join-Path $fixture "input.txt"
    $cacheOutput = Join-Path $fixture "output.h"
    $cacheStamp = Join-Path $fixture "output.stamp"
    $ready = Join-Path $fixture "ready"
    [IO.File]::WriteAllText($cacheInput, "first", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($cacheOutput, "generated", [Text.UTF8Encoding]::new($false))

    . {
        . $helperPath
        $fingerprint = Get-GeneratedContentFingerprint -Schema "test-v1" -Inputs @($cacheInput)
        Assert-True (-not (Test-GeneratedContentCurrent -Output $cacheOutput -Stamp $cacheStamp `
                    -Fingerprint $fingerprint)) "Missing generated-content stamp is stale"
        Write-GeneratedContentStamp -Stamp $cacheStamp -Fingerprint $fingerprint
        Assert-True (Test-GeneratedContentCurrent -Output $cacheOutput -Stamp $cacheStamp `
                    -Fingerprint $fingerprint) "Matching generated-content stamp is current"

        $mutex = Enter-GeneratedContentLock -Name "regression" -RepositoryRoot $fixture
        $job = Start-Job -ScriptBlock {
            param($CacheHelper, $RepositoryRoot, $ReadyPath)

            . $CacheHelper
            New-Item -ItemType File -Force -Path $ReadyPath | Out-Null
            $held = Enter-GeneratedContentLock -Name "regression" -RepositoryRoot $RepositoryRoot
            Exit-GeneratedContentLock -Mutex $held
        } -ArgumentList $helperPath, $fixture, $ready

        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while (-not (Test-Path -LiteralPath $ready) -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 25
        }
        Assert-True (Test-Path -LiteralPath $ready) "Concurrent cache waiter started"
        Start-Sleep -Milliseconds 200
        Assert-True ($job.State -eq "Running") "Concurrent cache waiter remains blocked"
        Exit-GeneratedContentLock -Mutex $mutex
        $mutex = $null
        Wait-Job -Job $job -Timeout 15 | Out-Null
        Assert-True ($job.State -eq "Completed") "Concurrent cache waiter completes after release"
        Receive-Job -Job $job -ErrorAction Stop | Out-Null
        Remove-Job -Job $job
        $job = $null

        [IO.File]::WriteAllText($cacheInput, "second", [Text.UTF8Encoding]::new($false))
        $changed = Get-GeneratedContentFingerprint -Schema "test-v1" -Inputs @($cacheInput)
        Assert-True (-not (Test-GeneratedContentCurrent -Output $cacheOutput -Stamp $cacheStamp `
                    -Fingerprint $changed)) "Changed generated-content input invalidates the stamp"

        $runtimeSource = Join-Path $fixture "runtime-source.dll"
        $runtimeDestination = Join-Path $fixture "runtime-destination.dll"
        [IO.File]::WriteAllText($runtimeSource, "runtime-v1", [Text.UTF8Encoding]::new($false))
        & $copyScript -Source $runtimeSource -Destination $runtimeDestination
        $lockedDestination = [IO.File]::Open($runtimeDestination, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::Read)
        & $copyScript -Source $runtimeSource -Destination $runtimeDestination
        [IO.File]::WriteAllText($runtimeSource, "runtime-v2-changed", [Text.UTF8Encoding]::new($false))
        Assert-Throws { & $copyScript -Source $runtimeSource -Destination $runtimeDestination } `
            "Changed locked runtime staging"
        $lockedDestination.Dispose()
        $lockedDestination = $null
        & $copyScript -Source $runtimeSource -Destination $runtimeDestination
        Assert-True (([IO.File]::ReadAllText($runtimeDestination)) -eq "runtime-v2-changed") `
            "Changed unlocked runtime is staged"
    }
}
finally {
    if ($lockedDestination) {
        $lockedDestination.Dispose()
    }
    if ($mutex) {
        Exit-GeneratedContentLock -Mutex $mutex
    }
    if ($job) {
        Stop-Job -Job $job -ErrorAction SilentlyContinue
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Windows generated-content cache regression tests passed."
