$ErrorActionPreference = "Stop"
$common = Join-Path $PSScriptRoot "..\Windows\common.ps1"
. $common

$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture = Join-Path $temporaryRoot ("keire-lock-contention-" + [Guid]::NewGuid().ToString("N"))
$jobs = @()
try {
    New-Item -ItemType Directory -Path (Join-Path $fixture "Tools\.locks") -Force | Out-Null
    $claim = Join-Path $fixture "exclusive.lock"
    if (-not (New-KeireExclusiveLockDirectory -Path $claim)) { throw "Initial exclusive directory claim failed." }
    if (New-KeireExclusiveLockDirectory -Path $claim) { throw "An empty lock directory was claimed twice." }
    [IO.File]::WriteAllText((Join-Path $claim "sentinel"), "original")
    if (New-KeireExclusiveLockDirectory -Path $claim) { throw "An occupied directory was claimed twice." }
    if ([IO.File]::ReadAllText((Join-Path $claim "sentinel")) -ne "original") {
        throw "Failed directory claim changed the existing owner's data."
    }
    if (@(Get-ChildItem -LiteralPath $fixture -Filter "*.claim.*").Count -ne 0) {
        throw "Failed directory claim left temporary directories."
    }

    foreach ($index in 1..6) {
        $jobs += Start-Job -ArgumentList $common, $fixture, $index -ScriptBlock {
            param($Common, $Fixture, $Index)
            $ErrorActionPreference = "Stop"
            . $Common
            $env:KEIRE_WORKSPACE_LOCK_TOKEN = $null
            $env:KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS = "60"
            $env:KEIRE_WORKSPACE_LOCK_STALE_SECONDS = "60"
            $env:KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS = "1"
            [IO.File]::WriteAllText((Join-Path $Fixture "ready-$Index"), "ready")
            $deadline = [DateTime]::UtcNow.AddSeconds(60)
            while (-not (Test-Path -LiteralPath (Join-Path $Fixture "start"))) {
                if ([DateTime]::UtcNow -ge $deadline) { throw "Contention barrier timed out." }
                Start-Sleep -Milliseconds 10
            }
            foreach ($iteration in 1..3) {
                $lock = Enter-KeireWorkspaceLock -RepositoryRoot $Fixture -CommandName "contender-$Index-$iteration"
                $guard = $null
                try {
                    $owner = Get-KeireWorkspaceLockOwner -LockPath $lock.Path
                    if (-not $lock.Acquired -or $owner.token -ne $lock.Token) {
                        throw "Contender did not receive exclusive lock ownership."
                    }
                    $guard = [IO.File]::Open((Join-Path $Fixture "critical-section"), [IO.FileMode]::CreateNew,
                        [IO.FileAccess]::Write, [IO.FileShare]::None)
                    Start-Sleep -Milliseconds 40
                    $owner = Get-KeireWorkspaceLockOwner -LockPath $lock.Path
                    if ($owner.token -ne $lock.Token) { throw "A waiting contender replaced the live owner." }
                    [IO.File]::WriteAllText((Join-Path $Fixture "completed-$Index-$iteration"), "owned")
                }
                finally {
                    if ($guard) {
                        $guard.Dispose()
                        [IO.File]::Delete((Join-Path $Fixture "critical-section"))
                    }
                    Exit-KeireWorkspaceLock -Lock $lock
                }
            }
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (@(Get-ChildItem -LiteralPath $fixture -Filter "ready-*").Count -ne $jobs.Count) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Contention workers did not become ready." }
        Start-Sleep -Milliseconds 20
    }
    [IO.File]::WriteAllText((Join-Path $fixture "start"), "start")
    $jobs | Wait-Job -Timeout 90 | Out-Null
    foreach ($job in $jobs) {
        Receive-Job -Job $job -ErrorAction Stop | Out-Null
        if ($job.State -ne "Completed") { throw "Contention worker did not complete: $($job.State)." }
    }
    if (@(Get-ChildItem -LiteralPath $fixture -Filter "completed-*").Count -ne 18) {
        throw "Not all contending commands completed."
    }
    if (@(Get-ChildItem -LiteralPath (Join-Path $fixture "Tools\.locks") -Force).Count -ne 0) {
        throw "Contending commands left ownership files or claim directories behind."
    }

    $cleanupPath = Join-Path $fixture "cleanup.lock"
    [IO.Directory]::CreateDirectory($cleanupPath) | Out-Null
    [IO.File]::WriteAllText((Join-Path $cleanupPath "owner"), "token=cleanup-test`n")
    [IO.File]::WriteAllText((Join-Path $cleanupPath "unexpected"), "preserve me")
    [IO.Directory]::CreateDirectory((Join-Path $cleanupPath "heartbeat")) | Out-Null
    [IO.File]::WriteAllText((Join-Path $cleanupPath "heartbeat\unexpected"), "preserve nested data")
    $cleanupJob = Start-Job -ArgumentList $common, $cleanupPath -ScriptBlock {
        param($Common, $CleanupPath)
        $ErrorActionPreference = "Stop"
        . $Common
        $env:KEIRE_WORKSPACE_LOCK_TOKEN = "cleanup-test"
        $lock = [pscustomobject]@{
            Acquired = $true; Path = $CleanupPath; Token = "cleanup-test"; Job = $null; PreviousToken = "parent-test"
        }
        Exit-KeireWorkspaceLock -Lock $lock
        if ($env:KEIRE_WORKSPACE_LOCK_TOKEN -ne "parent-test") { throw "Cleanup did not restore the parent token." }
        if ([IO.File]::ReadAllText((Join-Path $CleanupPath "unexpected")) -ne "preserve me") {
            throw "Cleanup changed an unexpected file."
        }
        if ([IO.File]::ReadAllText((Join-Path $CleanupPath "heartbeat\unexpected")) -ne "preserve nested data") {
            throw "Cleanup changed an unexpected directory."
        }
    }
    $jobs += $cleanupJob
    $cleanupJob | Wait-Job -Timeout 30 | Out-Null
    if ($cleanupJob.State -ne "Completed") { throw "Cleanup blocked instead of preserving unexpected files." }
    Receive-Job -Job $cleanupJob -ErrorAction Stop | Out-Null

    $quarantinePath = Join-Path $fixture "quarantine.lock"
    $rejected = $false
    try { Remove-KeireStaleWorkspaceLock -LockPath $cleanupPath -QuarantinePath $quarantinePath | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected -or [IO.File]::ReadAllText((Join-Path $quarantinePath "unexpected")) -ne "preserve me" -or
        [IO.File]::ReadAllText((Join-Path $quarantinePath "heartbeat\unexpected")) -ne "preserve nested data") {
        throw "Stale lock cleanup did not reject and preserve unexpected files."
    }
    Write-Host "Windows workspace lock contention passed: six processes, 18 acquisitions, no overlap or residue."
    Write-Host "Unexpected lock files were preserved without interactive prompts."
}
finally {
    foreach ($job in $jobs) {
        Stop-Job -Job $job -ErrorAction SilentlyContinue
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    if (-not $resolvedFixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike "keire-lock-contention-*") {
        throw "Refusing to clean an unexpected contention fixture path."
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
}
