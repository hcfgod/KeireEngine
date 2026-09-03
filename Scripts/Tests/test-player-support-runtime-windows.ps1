$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$script = Join-Path $root 'Scripts\Windows\player-support.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-player-support-runtime-" + [guid]::NewGuid().ToString('N'))
$source = Join-Path $fixture 'source'
$licenseSource = Join-Path $fixture 'licenses'
$vcRedistSource = Join-Path $fixture 'vc-redist'
$licenseSources = [ordered]@{
    'Keire-LICENSE.txt' = 'LICENSE.txt'
    'Keire-THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'Coral-LICENSE.txt' = 'Build\Dependencies\coral\LICENSE'
    'dotnet-LICENSE.txt' = 'Build\Dependencies\dotnet-sdk\LICENSE.txt'
    'dotnet-ThirdPartyNotices.txt' = 'Build\Dependencies\dotnet-sdk\ThirdPartyNotices.txt'
    'SDL-LICENSE.txt' = 'Build\Dependencies\windows-x86_64-msc\Release\install\licenses\SDL3\LICENSE.txt'
    'assimp-LICENSE.txt' = 'Vendor\assimp\LICENSE'
    'assimp-zlib-LICENSE.txt' = 'Vendor\assimp\contrib\zlib\LICENSE'
    'stb-LICENSE.txt' = 'Vendor\stb\LICENSE'
    'Jolt-LICENSE.txt' = 'Build\Dependencies\windows-x86_64-msc\Release\install\share\licenses\keire\Jolt-LICENSE.txt'
    'Recast-LICENSE.txt' = 'Build\Dependencies\windows-x86_64-msc\Release\install\share\licenses\keire\Recast-LICENSE.txt'
    'miniaudio-LICENSE.txt' = 'Build\Dependencies\windows-x86_64-msc\Release\install\share\licenses\keire\miniaudio-LICENSE.txt'
    'spdlog-LICENSE.txt' = 'Vendor\spdlog\LICENSE'
    'fmt-LICENSE.rst' = 'Vendor\spdlog\include\spdlog\fmt\bundled\fmt.license.rst'
    'nlohmann-json-LICENSE.MIT.txt' = 'Vendor\json\LICENSE.MIT'
    'dear-imgui-LICENSE.txt' = 'Vendor\imgui\LICENSE.txt'
    'zstandard-LICENSE.txt' = 'Vendor\zstd\LICENSE'
    'entt-LICENSE.txt' = 'Vendor\entt\LICENSE'
    'glm-COPYING.txt' = 'Vendor\glm\copying.txt'
}
$licenseNames = @($licenseSources.Keys)
$vcRuntimeNames = @('MSVCP140.dll', 'MSVCP140_ATOMIC_WAIT.dll', 'MSVCP140_1.dll',
                    'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')
$previousLicenseSource = [Environment]::GetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE')
$previousVCRedistSource = [Environment]::GetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_VC_REDIST_ROOT')

function Write-FixtureFile([string]$Relative, [string]$Value = 'fixture') {
    $path = Join-Path $source $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, $Value, [Text.UTF8Encoding]::new($false))
}

try {
    foreach ($license in $licenseSources.Keys) {
        $path = Join-Path $licenseSource $licenseSources[$license]
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        [IO.File]::WriteAllText($path, "license-$license",
                                [Text.UTF8Encoding]::new($false))
    }
    $vcRuntimeDirectory = Join-Path $vcRedistSource 'x64\Microsoft.VC143.CRT'
    New-Item -ItemType Directory -Force -Path $vcRuntimeDirectory | Out-Null
    foreach ($runtime in $vcRuntimeNames) {
        [IO.File]::WriteAllText((Join-Path $vcRuntimeDirectory $runtime), "runtime-$runtime",
                                [Text.UTF8Encoding]::new($false))
    }
    [Environment]::SetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE', $licenseSource)
    [Environment]::SetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_VC_REDIST_ROOT', $vcRedistSource)
    foreach ($file in @('KeireRuntime.exe', 'nethost.dll', 'Managed\Coral.Managed.dll',
                         'Managed\Coral.Managed.deps.json', 'Managed\Coral.Managed.runtimeconfig.json',
                         'Managed\Keire.Managed.dll', 'Managed\Dotnet\host\fxr\10.0.10\hostfxr.dll',
                         'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.9\coreclr.dll',
                         'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.11\coreclr.dll',
                         'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.11\hostpolicy.dll',
                         'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.11\System.Private.CoreLib.dll')) {
        Write-FixtureFile $file
    }
    $destination = Join-Path $fixture 'staged'
    & $script -RuntimeClosureSource $source -RuntimeClosureDestination $destination
    if (-not (Test-Path (Join-Path $destination 'KeireRuntime.exe')) -or
        (Test-Path (Join-Path $destination 'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.9')) -or
        -not (Test-Path (Join-Path $destination 'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.11\coreclr.dll'))) {
        throw 'The Windows runtime allowlist did not select one current CoreCLR generation.'
    }
    foreach ($license in $licenseNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $destination "Licenses\$license") -PathType Leaf)) {
            throw "The Windows runtime closure omitted required license $license."
        }
    }
    foreach ($runtime in $vcRuntimeNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $destination $runtime) -PathType Leaf)) {
            throw "The Windows runtime closure omitted required VC runtime $runtime."
        }
    }
    $hostPolicy = Join-Path $source 'Managed\Dotnet\shared\Microsoft.NETCore.App\10.0.11\hostpolicy.dll'
    Remove-Item -LiteralPath $hostPolicy
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'hostpolicy-stage')
        throw 'The Windows runtime closure accepted a missing hostpolicy library.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('regular non-reparse file')) { throw }
    }
    [IO.File]::WriteAllText($hostPolicy, 'fixture', [Text.UTF8Encoding]::new($false))

    Write-FixtureFile 'Managed\Dotnet\sdk\10.0.302\MSBuild.dll'
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'sdk-stage')
        throw 'The Windows runtime allowlist accepted SDK content.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('prohibited .NET content')) { throw }
    }
    Remove-Item (Join-Path $source 'Managed\Dotnet\sdk') -Recurse -Force
    Write-FixtureFile 'KeireAssetTool.exe'
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'tool-stage')
        throw 'The Windows runtime allowlist accepted an unexpected executable.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('unexpected executable')) { throw }
    }

    Remove-Item -LiteralPath (Join-Path $source 'KeireAssetTool.exe') -Force
    $junctionTarget = Join-Path $fixture 'junction-target'
    New-Item -ItemType Directory -Force -Path $junctionTarget | Out-Null
    Write-FixtureFile 'Managed\Dotnet\host\fxr\10.0.11\placeholder'
    Remove-Item -LiteralPath (Join-Path $source 'Managed\Dotnet\host\fxr\10.0.11') -Recurse -Force
    $junction = Join-Path $source 'Managed\Dotnet\host\fxr\10.0.11'
    New-Item -ItemType Junction -Path $junction -Target $junctionTarget | Out-Null
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'junction-stage')
        throw 'The Windows runtime allowlist accepted a junction root.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('non-reparse directory')) { throw }
    }
    Remove-Item -LiteralPath $junction -Force

    $missingLicense = Join-Path $licenseSource $licenseSources['Coral-LICENSE.txt']
    Remove-Item -LiteralPath $missingLicense -Force
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'license-stage')
        throw 'The Windows runtime closure accepted a missing required license.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('regular non-reparse file')) { throw }
    }
    [IO.File]::WriteAllText($missingLicense, 'restored', [Text.UTF8Encoding]::new($false))

    $missingRuntime = Join-Path $vcRuntimeDirectory 'MSVCP140_1.dll'
    Remove-Item -LiteralPath $missingRuntime -Force
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'vc-runtime-stage')
        throw 'The Windows runtime closure accepted a missing VC runtime DLL.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('regular non-reparse file')) { throw }
    }
    [IO.File]::WriteAllText($missingRuntime, 'restored', [Text.UTF8Encoding]::new($false))
    $realVCRuntimeDirectory = Join-Path $fixture 'vc-runtime-real'
    Move-Item -LiteralPath $vcRuntimeDirectory -Destination $realVCRuntimeDirectory
    New-Item -ItemType Junction -Path $vcRuntimeDirectory -Target $realVCRuntimeDirectory | Out-Null
    try {
        & $script -RuntimeClosureSource $source -RuntimeClosureDestination (Join-Path $fixture 'vc-junction-stage')
        throw 'The Windows runtime closure accepted a redirected VC runtime directory.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('non-reparse directory')) { throw }
    }
    Remove-Item -LiteralPath $vcRuntimeDirectory -Force
    Move-Item -LiteralPath $realVCRuntimeDirectory -Destination $vcRuntimeDirectory

    $publication = Join-Path $fixture 'publication'
    $firstArchive = Join-Path $fixture 'first.keireplayersupport'
    [IO.File]::WriteAllText($firstArchive, 'first', [Text.UTF8Encoding]::new($false))
    & $script -CatalogPublishSource $firstArchive -CatalogPublishOutput $publication `
        -CatalogPublishId windows-x86_64-test | Out-Null
    $catalogPath = Join-Path $publication 'player-support-catalog.json'
    $catalogBeforeFailure = Get-Content -LiteralPath $catalogPath -Raw
    $replacement = Join-Path $fixture 'replacement.keireplayersupport'
    [IO.File]::WriteAllText($replacement, 'replacement', [Text.UTF8Encoding]::new($false))
    try {
        & $script -CatalogPublishSource $replacement -CatalogPublishOutput $publication `
            -CatalogPublishId windows-x86_64-test -TestFailCatalogPublish | Out-Null
        throw 'The Windows catalog failure injection unexpectedly succeeded.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('catalog publication failure')) { throw }
    }
    if ((Get-Content -LiteralPath $catalogPath -Raw) -ne $catalogBeforeFailure) {
        throw 'A failed Windows publication changed the live catalog.'
    }
    $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
    if (-not (Test-Path -LiteralPath (Join-Path $publication $catalog.packages[0].file) -PathType Leaf)) {
        throw 'The Windows live catalog no longer references an available archive.'
    }

    $invalidOutput = Join-Path $fixture 'invalid-catalog'
    $invalidBase = Join-Path $fixture 'invalid-base.keireplayersupport'
    [IO.File]::WriteAllText($invalidBase, 'invalid-base', [Text.UTF8Encoding]::new($false))
    & $script -CatalogPublishSource $invalidBase -CatalogPublishOutput $invalidOutput `
        -CatalogPublishId windows-x86_64-invalid-base | Out-Null
    $invalidCatalogPath = Join-Path $invalidOutput 'player-support-catalog.json'
    $invalidCatalog = Get-Content -LiteralPath $invalidCatalogPath -Raw | ConvertFrom-Json
    $invalidCatalog.packages[0].sha256 = $invalidCatalog.packages[0].sha256.ToUpperInvariant()
    [IO.File]::WriteAllText($invalidCatalogPath, (($invalidCatalog | ConvertTo-Json -Depth 6) + "`n"),
                            [Text.UTF8Encoding]::new($false))
    $invalidCandidate = Join-Path $fixture 'invalid-candidate.keireplayersupport'
    [IO.File]::WriteAllText($invalidCandidate, 'invalid-candidate', [Text.UTF8Encoding]::new($false))
    try {
        & $script -CatalogPublishSource $invalidCandidate -CatalogPublishOutput $invalidOutput `
            -CatalogPublishId windows-x86_64-invalid-candidate | Out-Null
        throw 'The Windows publisher accepted an invalid existing catalog entry.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('invalid or duplicated')) { throw }
    }

    $duplicateOutput = Join-Path $fixture 'duplicate-catalog'
    $duplicateBase = Join-Path $fixture 'duplicate-base.keireplayersupport'
    [IO.File]::WriteAllText($duplicateBase, 'duplicate-base', [Text.UTF8Encoding]::new($false))
    & $script -CatalogPublishSource $duplicateBase -CatalogPublishOutput $duplicateOutput `
        -CatalogPublishId windows-x86_64-duplicate-base | Out-Null
    $duplicateCatalogPath = Join-Path $duplicateOutput 'player-support-catalog.json'
    $duplicateCatalog = Get-Content -LiteralPath $duplicateCatalogPath -Raw | ConvertFrom-Json
    $duplicateCatalog.packages = @($duplicateCatalog.packages) + @($duplicateCatalog.packages[0])
    [IO.File]::WriteAllText($duplicateCatalogPath, (($duplicateCatalog | ConvertTo-Json -Depth 6) + "`n"),
                            [Text.UTF8Encoding]::new($false))
    $duplicateCandidate = Join-Path $fixture 'duplicate-candidate.keireplayersupport'
    [IO.File]::WriteAllText($duplicateCandidate, 'duplicate-candidate', [Text.UTF8Encoding]::new($false))
    try {
        & $script -CatalogPublishSource $duplicateCandidate -CatalogPublishOutput $duplicateOutput `
            -CatalogPublishId windows-x86_64-duplicate-candidate | Out-Null
        throw 'The Windows publisher accepted duplicate existing catalog entries.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('invalid or duplicated')) { throw }
    }

    $missingOutput = Join-Path $fixture 'missing-catalog'
    $missingBase = Join-Path $fixture 'missing-base.keireplayersupport'
    [IO.File]::WriteAllText($missingBase, 'missing-base', [Text.UTF8Encoding]::new($false))
    & $script -CatalogPublishSource $missingBase -CatalogPublishOutput $missingOutput `
        -CatalogPublishId windows-x86_64-missing-base | Out-Null
    $missingCatalogPath = Join-Path $missingOutput 'player-support-catalog.json'
    $missingCatalog = Get-Content -LiteralPath $missingCatalogPath -Raw | ConvertFrom-Json
    Remove-Item -LiteralPath (Join-Path $missingOutput $missingCatalog.packages[0].file)
    $missingCandidate = Join-Path $fixture 'missing-candidate.keireplayersupport'
    [IO.File]::WriteAllText($missingCandidate, 'missing-candidate', [Text.UTF8Encoding]::new($false))
    try {
        & $script -CatalogPublishSource $missingCandidate -CatalogPublishOutput $missingOutput `
            -CatalogPublishId windows-x86_64-missing-candidate | Out-Null
        throw 'The Windows publisher accepted a missing referenced archive.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('missing or redirected archive')) { throw }
    }

    $parallelOutput = Join-Path $fixture 'parallel-publication'
    $parallelOne = Join-Path $fixture 'parallel-one.keireplayersupport'
    $parallelTwo = Join-Path $fixture 'parallel-two.keireplayersupport'
    [IO.File]::WriteAllText($parallelOne, 'parallel-one', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($parallelTwo, 'parallel-two', [Text.UTF8Encoding]::new($false))
    $shell = (Get-Process -Id $PID).Path
    $commonArguments = @('-NoProfile', '-File', $script)
    $first = Start-Process -FilePath $shell -ArgumentList ($commonArguments + @(
        '-CatalogPublishSource', $parallelOne, '-CatalogPublishOutput', $parallelOutput,
        '-CatalogPublishId', 'windows-x86_64-test')) -PassThru -WindowStyle Hidden
    $second = Start-Process -FilePath $shell -ArgumentList ($commonArguments + @(
        '-CatalogPublishSource', $parallelTwo, '-CatalogPublishOutput', $parallelOutput,
        '-CatalogPublishId', 'windows-arm64-test', '-CatalogPublishArchitecture', 'arm64')) `
        -PassThru -WindowStyle Hidden
    $first, $second | Wait-Process
    if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0) {
        throw 'Concurrent Windows catalog publication failed.'
    }
    $parallelCatalog = Get-Content -LiteralPath (Join-Path $parallelOutput 'player-support-catalog.json') -Raw |
        ConvertFrom-Json
    if (@($parallelCatalog.packages).Count -ne 2) { throw 'Concurrent Windows catalog publication lost an entry.' }

    $cleanupProbe = Join-Path $fixture '.staging-cleanup-probe'
    New-Item -ItemType Directory -Force -Path $cleanupProbe | Out-Null
    [IO.File]::WriteAllText((Join-Path $cleanupProbe '.keire-player-support-operation'), 'owned')
    try {
        & $script -CleanupProbeDirectory $cleanupProbe -TestFailCleanup
        throw 'A successful Windows operation ignored its cleanup failure.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('cleanup failure')) { throw }
    }
    try {
        & $script -CleanupProbeDirectory $cleanupProbe -TestFailCleanup -TestPrimaryFailure
        throw 'A Windows cleanup failure replaced neither error.'
    }
    catch {
        if (-not $_.Exception.Message.Contains('primary failure')) { throw }
    }
    & $script -CleanupProbeDirectory $cleanupProbe
    Write-Host 'Windows Player Support runtime closure tests passed.'
}
finally {
    [Environment]::SetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_LICENSE_SOURCE', $previousLicenseSource)
    [Environment]::SetEnvironmentVariable('KEIRE_PLAYER_SUPPORT_VC_REDIST_ROOT', $previousVCRedistSource)
    Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}
