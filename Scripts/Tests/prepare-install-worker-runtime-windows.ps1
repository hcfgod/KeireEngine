[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"
. (Join-Path $Windows "common.ps1")

$Project = Get-ProjectConfig
$build = Join-Path $Windows "build.ps1"
$configuration = "Debug"
$architecture = "x86_64"
$targets = @(
    "$($Project.PROJECT_NAMESPACE)InstallWorker",
    "$($Project.PROJECT_NAMESPACE)InstallVerifyFixture"
)

foreach ($target in $targets) {
    Invoke-CheckedWindowsCommand {
        & $build -Generator ninja -Configuration $configuration -Architecture $architecture `
            -Toolset msc -Target $target
    } "$target installer regression prerequisite build"
}

foreach ($target in $targets) {
    $executable = Join-Path $Root `
        "Build\Bin\$configuration-windows-$architecture\$target\$target.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "The installer regression prerequisite build output is missing: $executable"
    }
}

Write-Host "Windows installer runtime prerequisites are ready."
