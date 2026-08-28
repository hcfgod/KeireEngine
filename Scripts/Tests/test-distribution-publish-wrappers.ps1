$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Wrapper = Join-Path $Root 'Services\KeireDistributionService\scripts\publish-snapshot.ps1'
$FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('keire-distribution-publish-wrapper-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($FixtureRoot) | Out-Null

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-ExpectedFailure([scriptblock] $Action, [string] $ExpectedMessage) {
    try {
        & $Action
    }
    catch {
        if (-not $_.Exception.Message.Contains($ExpectedMessage)) {
            throw "Expected failure '$ExpectedMessage', received '$($_.Exception.Message)'."
        }
        return
    }
    throw "Expected failure '$ExpectedMessage' did not occur."
}

$capturePath = Join-Path $FixtureRoot 'captured-arguments.txt'
$fakeDotnet = Join-Path $FixtureRoot 'dotnet.cmd'
$fakeContents = @(
    '@echo off',
    'echo %* > "%KEIRE_PUBLISH_WRAPPER_CAPTURE%"',
    'exit /b 0'
) -join "`r`n"
[IO.File]::WriteAllText($fakeDotnet, "$fakeContents`r`n", [Text.Encoding]::ASCII)
$env:KEIRE_PUBLISH_WRAPPER_CAPTURE = $capturePath

$common = @{
    Source = Join-Path $FixtureRoot 'prepared'
    DistributionRoot = Join-Path $FixtureRoot 'distribution'
    SnapshotId = 'release-test-sequence-18'
    PublicKey = Join-Path $FixtureRoot 'trusted-public-key.json'
    Dotnet = $fakeDotnet
}

try {
    Invoke-ExpectedFailure {
        & $Wrapper @common -Activate
    } 'Activation requires an explicit minimum sequence'
    Assert-True (-not (Test-Path -LiteralPath $capturePath)) `
        'Activation without a minimum sequence reached the publisher.'

    Invoke-ExpectedFailure {
        & $Wrapper @common -MinimumSequence 0
    } 'Minimum sequence must be at least one'
    Invoke-ExpectedFailure {
        & $Wrapper @common -MinimumValidityHours ([double]::PositiveInfinity)
    } 'Minimum validity hours must be finite'

    & $Wrapper @common -MinimumSequence 18 -MinimumValidityHours 48.5 -Activate
    Assert-True (Test-Path -LiteralPath $capturePath -PathType Leaf) `
        'The valid publish invocation did not reach the publisher.'
    $captured = Get-Content -LiteralPath $capturePath -Raw
    foreach ($expected in @(
            'publish',
            '--minimum-sequence 18',
            '--minimum-validity-hours 48.5',
            '--activate'
        )) {
        Assert-True ($captured.Contains($expected)) `
            "The Windows publish wrapper did not forward '$expected'."
    }

    Remove-Item -LiteralPath $capturePath -Force
    & $Wrapper @common
    $captured = Get-Content -LiteralPath $capturePath -Raw
    Assert-True ($captured.Contains('--minimum-validity-hours 24')) `
        'The Windows publish wrapper did not forward its default validity floor.'
    Assert-True (-not $captured.Contains('--minimum-sequence')) `
        'A non-activating publish invented a minimum sequence.'

    Write-Host 'Windows distribution publish wrapper checks passed.'
}
finally {
    Remove-Item Env:KEIRE_PUBLISH_WRAPPER_CAPTURE -ErrorAction SilentlyContinue
    $resolvedFixture = [IO.Path]::GetFullPath($FixtureRoot)
    $temporaryPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedFixture.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}
