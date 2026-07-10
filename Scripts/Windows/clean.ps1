[CmdletBinding()]
param(
    [switch]$Generated,
    [switch]$Build,
    [switch]$All
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not $Generated -and -not $Build -and -not $All) {
    $All = $true
}

function Remove-SafePath {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return
    }

    $resolved = Resolve-Path $Path
    if (-not $resolved.Path.StartsWith($Root.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside the repository: $($resolved.Path)"
    }

    Remove-Item -LiteralPath $resolved.Path -Recurse -Force
    Write-Host "Removed $($resolved.Path)"
}

if ($All -or $Build) {
    Remove-SafePath (Join-Path $Root "Build\Bin")
    Remove-SafePath (Join-Path $Root "Build\Intermediates")
}

if ($All -or $Generated) {
    Remove-SafePath (Join-Path $Root "Build\Generated")

    $rootPatterns = @(
        "*.sln",
        "*.slnx",
        "Makefile",
        "build.ninja",
        "compile_commands.json",
        "*.xcodeproj",
        "*.xcworkspace"
    )

    foreach ($pattern in $rootPatterns) {
        Get-ChildItem -Path $Root -Filter $pattern -Force -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-SafePath $_.FullName }
    }

    $projectPatterns = @(
        "*.vcxproj",
        "*.vcxproj.filters",
        "*.vcxproj.user",
        "Makefile",
        "*.make",
        "*.ninja",
        "*.xcodeproj",
        "*.xcworkspace"
    )

    foreach ($projectDir in @("Core", "Client", "Tests")) {
        $path = Join-Path $Root $projectDir
        if (-not (Test-Path $path)) {
            continue
        }
        foreach ($pattern in $projectPatterns) {
            Get-ChildItem -Path $path -Filter $pattern -Force -ErrorAction SilentlyContinue |
                ForEach-Object { Remove-SafePath $_.FullName }
        }
    }
}

Write-Host "==> Clean complete"
