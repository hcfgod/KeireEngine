Set-StrictMode -Version Latest

function Get-GeneratedContentFileHash {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return -join ($sha256.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") })
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Get-GeneratedContentFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Schema,
        [Parameter(Mandatory = $true)][string[]]$Inputs
    )

    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("schema=$Schema")
    foreach ($inputPath in $Inputs) {
        if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
            throw "Generated-content input is missing: $inputPath"
        }
        $hash = Get-GeneratedContentFileHash -Path $inputPath
        $lines.Add($hash)
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
        return -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") })
    }
    finally {
        $sha256.Dispose()
    }
}

function Test-GeneratedContentCurrent {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string]$Stamp,
        [Parameter(Mandatory = $true)][string]$Fingerprint
    )

    return (Test-Path -LiteralPath $Output -PathType Leaf) -and
        (Test-Path -LiteralPath $Stamp -PathType Leaf) -and
        ((Get-Content -LiteralPath $Stamp -Raw).Trim() -eq "keire-generated-v1|$Fingerprint")
}

function Enter-GeneratedContentLock {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [TimeSpan]$Timeout = [TimeSpan]::FromMinutes(10),
        [string]$WaitMessage = ""
    )

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $rootBytes = [Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($RepositoryRoot).ToLowerInvariant())
        $rootHash = -join ($sha256.ComputeHash($rootBytes) | Select-Object -First 8 |
            ForEach-Object { $_.ToString("x2") })
    }
    finally {
        $sha256.Dispose()
    }
    $safeName = $Name -replace '[^A-Za-z0-9_.-]', '-'
    $mutex = [Threading.Mutex]::new($false, "Local\Keire.Generated.$safeName.$rootHash")
    try {
        try {
            $acquired = $mutex.WaitOne([TimeSpan]::Zero)
        }
        catch [Threading.AbandonedMutexException] {
            $acquired = $true
        }
        if (-not $acquired) {
            if ($WaitMessage) {
                Write-Host $WaitMessage
            }
            try {
                $acquired = $mutex.WaitOne($Timeout)
            }
            catch [Threading.AbandonedMutexException] {
                $acquired = $true
            }
        }
        if (-not $acquired) {
            throw "Timed out waiting for generated content '$Name' from another build."
        }
        return $mutex
    }
    catch {
        $mutex.Dispose()
        throw
    }
}

function Exit-GeneratedContentLock {
    param([Parameter(Mandatory = $true)][Threading.Mutex]$Mutex)

    try {
        $Mutex.ReleaseMutex()
    }
    finally {
        $Mutex.Dispose()
    }
}

function Write-GeneratedContentStamp {
    param(
        [Parameter(Mandatory = $true)][string]$Stamp,
        [Parameter(Mandatory = $true)][string]$Fingerprint
    )

    [IO.Directory]::CreateDirectory((Split-Path -Parent $Stamp)) | Out-Null
    $temporary = "$Stamp.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllText($temporary, "keire-generated-v1|$Fingerprint`n", [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $Stamp -Force
    }
    finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}
