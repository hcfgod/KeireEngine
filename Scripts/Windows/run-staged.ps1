[CmdletBinding()]
param([switch]$Editor, [string]$Architecture = "", [string]$ProjectPath = "")

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$kind = if ($Editor) { "editor" } else { "hub" }
$target = if ($Editor) { $project.CLIENT_TARGET } else { $project.HUB_TARGET }
$stage = Join-Path (Get-RepositoryRoot) "Build\Distributions\$($project.ARTIFACT_PREFIX)-$kind-windows-$Architecture-Dist"
$executable = Join-Path $stage "bin\$target.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "No staged $kind executable exists. Run Scripts\project.bat stage-$kind first. Expected: $executable"
}
if ($ProjectPath -and -not $Editor) { throw "-ProjectPath requires run-staged-editor." }
$arguments = @()
if ($ProjectPath) {
    $resolvedProject = (Resolve-Path -LiteralPath $ProjectPath).Path
    $arguments = @("--project", $resolvedProject)
}
# Preserve spaces, Unicode and trailing backslashes using Windows CRT quoting rules.
$start = [Diagnostics.ProcessStartInfo]::new($executable)
$start.UseShellExecute = $false
$start.WorkingDirectory = $stage
$quotedArguments = foreach ($argument in $arguments) {
    $escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
    '"' + [regex]::Replace($escaped, '(\\+)$', '$1$1') + '"'
}
$start.Arguments = $quotedArguments -join ' '
$process = [Diagnostics.Process]::Start($start)
$process.Dispose()
Write-Host "==> Launched staged ${kind}: $executable"
