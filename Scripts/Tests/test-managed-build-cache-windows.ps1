$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$fixture = Join-Path $temporaryRoot ('keire-managed-cache-' + [guid]::NewGuid().ToString('N'))

try {
    $sourceRoots = @('KeireManaged', 'KeireEditorManaged', 'KeireManaged.Generators', 'Scripts/ManagedDesignTime')
    foreach ($directory in @('Scripts/Windows', 'Config', 'Build/Managed/DesignTime') + $sourceRoots) {
        New-Item -ItemType Directory -Path (Join-Path $fixture $directory) -Force | Out-Null
    }
    foreach ($script in @('build-managed.ps1', 'common.ps1')) {
        Copy-Item -LiteralPath (Join-Path $root "Scripts/Windows/$script") `
            -Destination (Join-Path $fixture "Scripts/Windows/$script")
    }
    Copy-Item -LiteralPath (Join-Path $root 'Config/Dependencies.lock') `
        -Destination (Join-Path $fixture 'Config/Dependencies.lock')
    $inputs = @('KeireManaged/Runtime.cs', 'KeireManaged.Generators/AnalyzerReleases.Shipped.md',
        'Scripts/ManagedDesignTime/Keire.Managed.DesignTime.csproj')
    foreach ($inputFile in $inputs) {
        Set-Content -LiteralPath (Join-Path $fixture $inputFile) -Value 'input' -Encoding ASCII
    }
    $outputs = @('Keire.Managed.dll', 'Keire.Editor.Managed.dll', 'Keire.Managed.Generators.dll',
        'DesignTime/Keire.Managed.dll', 'DesignTime/Keire.Editor.Managed.dll')
    foreach ($outputFile in $outputs) {
        Set-Content -LiteralPath (Join-Path $fixture "Build/Managed/$outputFile") -Value 'output' -Encoding ASCII
    }
    $launcher = Join-Path $fixture 'Scripts/Windows/build-managed.ps1'
    function Reset-FixtureTimestamps {
        foreach ($file in Get-ChildItem -LiteralPath $fixture -Recurse -Force) {
            $file.LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(-3)
        }
        foreach ($outputFile in $outputs) {
            (Get-Item -LiteralPath (Join-Path $fixture "Build/Managed/$outputFile")).LastWriteTimeUtc =
                [DateTime]::UtcNow.AddMinutes(-1)
        }
    }
    function Assert-NeedsSdk([string]$description) {
        try {
            & $launcher
        }
        catch {
            if ($_.Exception.Message.Contains('locked Editor .NET SDK is missing')) {
                return
            }
            throw
        }
        throw "$description did not invalidate the managed build cache."
    }

    Reset-FixtureTimestamps
    $global:LASTEXITCODE = 91
    & $launcher
    if ($LASTEXITCODE -ne 0) { throw 'Unchanged managed outputs did not take the successful fast path.' }
    foreach ($inputFile in $inputs + @('Config/Dependencies.lock')) {
        Reset-FixtureTimestamps
        (Get-Item -LiteralPath (Join-Path $fixture $inputFile)).LastWriteTimeUtc = [DateTime]::UtcNow
        Assert-NeedsSdk $inputFile
    }
    Reset-FixtureTimestamps
    foreach ($outputFile in @('DesignTime/Keire.Managed.dll', 'DesignTime/Keire.Editor.Managed.dll')) {
        $path = Join-Path $fixture "Build/Managed/$outputFile"
        Move-Item -LiteralPath $path -Destination "$path.missing"
        try { Assert-NeedsSdk $outputFile }
        finally { Move-Item -LiteralPath "$path.missing" -Destination $path }
    }
    Write-Host 'Managed build cache checks passed: warm outputs, source/project/release/lock edits, missing design APIs.'
}
finally {
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -eq $temporaryRoot -and
        [IO.Path]::GetFileName($fixture).StartsWith('keire-managed-cache-')) {
        Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}
