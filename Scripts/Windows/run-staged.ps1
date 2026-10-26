[CmdletBinding()]
param(
    [switch]$Editor,
    [string]$Architecture = "",
    [string]$ProjectPath = "",
    [string]$Configuration = "DevDist"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$project = Get-ProjectConfig
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$kind = if ($Editor) { "editor" } else { "hub" }
$target = if ($Editor) { $project.CLIENT_TARGET } else { $project.HUB_TARGET }
if (-not $Editor) { $Configuration = 'Dist' }
if ($Configuration -notin @('DevDist', 'Dist')) { throw 'Staged Editor launch requires DevDist or Dist.' }
$stage = Join-Path (Get-RepositoryRoot) "Build\Distributions\$($project.ARTIFACT_PREFIX)-$kind-windows-$Architecture-$Configuration"
$executable = Join-Path $stage "bin\$target.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    $stageCommand = "Scripts\project.bat stage-$kind"
    if ($Editor) { $stageCommand += " -Configuration $Configuration" }
    throw "No staged $kind executable exists. Run $stageCommand first. Expected: $executable"
}
if ($ProjectPath -and -not $Editor) { throw "-ProjectPath requires run-staged-editor." }
if ($Configuration -eq 'DevDist') {
    . (Join-Path $PSScriptRoot 'ffmpeg-runtime-contract.ps1')
    . (Join-Path $PSScriptRoot 'editor-development-stage.ps1')
    Assert-EditorDevelopmentStageReady -Root (Get-RepositoryRoot) -Stage $stage
}
$arguments = @()
if ($ProjectPath) {
    $resolvedProject = (Resolve-Path -LiteralPath $ProjectPath).Path
    $arguments = @("--project", $resolvedProject)
}
# Preserve spaces, Unicode and trailing backslashes using Windows CRT quoting rules.
$start = [Diagnostics.ProcessStartInfo]::new($executable)
$start.UseShellExecute = $false
$start.WorkingDirectory = if ($Configuration -eq 'DevDist') { Get-RepositoryRoot } else { $stage }
$quotedArguments = foreach ($argument in $arguments) {
    $escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
    '"' + [regex]::Replace($escaped, '(\\+)$', '$1$1') + '"'
}
$start.Arguments = $quotedArguments -join ' '
$process = [Diagnostics.Process]::Start($start)
$process.Dispose()
Write-Host "==> Launched staged ${kind}: $executable"
