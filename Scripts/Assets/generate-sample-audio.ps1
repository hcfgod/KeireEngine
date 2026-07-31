[CmdletBinding()]
param(
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root "Samples\KeireSandbox\Assets\Audio"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Write-FourCC {
    param(
        [Parameter(Mandatory = $true)][IO.BinaryWriter]$Writer,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $Writer.Write([Text.Encoding]::ASCII.GetBytes($Value))
}

function Write-PcmWave {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][double]$DurationSeconds,
        [Parameter(Mandatory = $true)][scriptblock]$Sample
    )

    $sampleRate = 48000
    $channelCount = 1
    $bitsPerSample = 16
    $sampleCount = [int][Math]::Round($sampleRate * $DurationSeconds)
    $dataSize = $sampleCount * $channelCount * ($bitsPerSample / 8)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $writer = [IO.BinaryWriter]::new($stream)
        try {
            Write-FourCC $writer "RIFF"
            $writer.Write([int](36 + $dataSize))
            Write-FourCC $writer "WAVE"
            Write-FourCC $writer "fmt "
            $writer.Write([int]16)
            $writer.Write([int16]1)
            $writer.Write([int16]$channelCount)
            $writer.Write([int]$sampleRate)
            $writer.Write([int]($sampleRate * $channelCount * ($bitsPerSample / 8)))
            $writer.Write([int16]($channelCount * ($bitsPerSample / 8)))
            $writer.Write([int16]$bitsPerSample)
            Write-FourCC $writer "data"
            $writer.Write([int]$dataSize)

            for ($index = 0; $index -lt $sampleCount; ++$index) {
                $time = $index / [double]$sampleRate
                $value = [Math]::Max(-1.0, [Math]::Min(1.0, [double](& $Sample $time)))
                $writer.Write([int16][Math]::Round($value * [int16]::MaxValue))
            }
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

$twoPi = 2.0 * [Math]::PI
Write-PcmWave (Join-Path $OutputDirectory "InterfaceConfirm.wav") 0.32 {
    param([double]$Time)

    $frequency = if ($Time -lt 0.13) { 880.0 } else { 1320.0 }
    $attack = [Math]::Min(1.0, $Time / 0.008)
    $release = [Math]::Min(1.0, (0.32 - $Time) / 0.06)
    $envelope = [Math]::Max(0.0, $attack * $release)
    $fundamental = [Math]::Sin($twoPi * $frequency * $Time)
    $harmonic = [Math]::Sin($twoPi * $frequency * 2.0 * $Time)
    0.22 * $envelope * ($fundamental + 0.25 * $harmonic)
}

Write-PcmWave (Join-Path $OutputDirectory "SpatialEmitter.wav") 1.8 {
    param([double]$Time)

    $frequency = 330.0 + 110.0 * [Math]::Sin($twoPi * 0.5 * $Time)
    $attack = [Math]::Min(1.0, $Time / 0.04)
    $release = [Math]::Min(1.0, (1.8 - $Time) / 0.15)
    $pulse = 0.55 + 0.45 * [Math]::Sin($twoPi * 2.0 * $Time)
    $envelope = [Math]::Max(0.0, $attack * $release * $pulse)
    $fundamental = [Math]::Sin($twoPi * $frequency * $Time)
    $overtone = [Math]::Sin($twoPi * $frequency * 1.5 * $Time)
    0.18 * $envelope * ($fundamental + 0.2 * $overtone)
}

Write-Host "Generated repository-owned sample audio in $OutputDirectory"
