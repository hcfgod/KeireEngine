[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$Root = Get-RepositoryRoot
$Project = Get-ProjectConfig
$required = @("PROJECT_IDENTIFIER", "PROJECT_DISPLAY_NAME", "PROJECT_VERSION", "PROJECT_NAMESPACE", "PROJECT_MACRO_PREFIX", "CORE_TARGET", "CORE_DIRECTORY", "CLIENT_TARGET", "CLIENT_DIRECTORY", "HUB_TARGET", "HUB_DIRECTORY", "TESTS_TARGET", "TESTS_DIRECTORY", "ARTIFACT_PREFIX", "REPOSITORY_SLUG")
foreach ($key in $required) {
    if (-not $Project.ContainsKey($key)) { throw "Project configuration is missing '$key'." }
    if ([string]$Project[$key] -match '[\r\n]') { throw "Project configuration '$key' contains a line break." }
}
if (-not (Test-SemanticVersion $Project.PROJECT_VERSION)) {
    throw "PROJECT_VERSION must be a valid Semantic Version 2.0.0 value."
}

function ConvertTo-CStringContent {
    param([string]$Value)
    $builder = [Text.StringBuilder]::new()
    foreach ($character in $Value.ToCharArray()) {
        switch ([int]$character) {
            9 { [void]$builder.Append('\t') }
            34 { [void]$builder.Append('\"') }
            92 { [void]$builder.Append('\\') }
            default {
                $code = [int]$character
                if ($code -lt 32 -or $code -eq 127) { [void]$builder.Append(('\{0:000}' -f $code)) }
                else { [void]$builder.Append($character) }
            }
        }
    }
    return $builder.ToString()
}

$commit = if (Test-GitRepository $Root) { Get-GitHeadCommit $Root "unknown" } else { "unknown" }
$dirty = $false
if (Test-GitRepository $Root) {
    $status = (& git -C $Root status --porcelain --untracked-files=normal) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "Unable to inspect Git working-tree state." }
    $dirty = -not [string]::IsNullOrEmpty($status)
}

$version = ConvertTo-CStringContent $Project.PROJECT_VERSION
$displayName = ConvertTo-CStringContent $Project.PROJECT_DISPLAY_NAME
$commit = ConvertTo-CStringContent $commit
$dirtyLiteral = if ($dirty) { "true" } else { "false" }
$content = @"
#pragma once

#define KEIRE_BUILD_PROJECT_VERSION "$version"
#define KEIRE_BUILD_PROJECT_NAME "$displayName"
#define KEIRE_BUILD_GIT_COMMIT "$commit"
#define KEIRE_BUILD_GIT_DIRTY $dirtyLiteral
"@
$content += "`n"

$directory = Join-Path $Root "Build\Generated\$($Project.PROJECT_NAMESPACE)"
$output = Join-Path $directory "BuildInfo.generated.h"
[IO.Directory]::CreateDirectory($directory) | Out-Null
$existing = if (Test-Path -LiteralPath $output) { [IO.File]::ReadAllText($output, [Text.Encoding]::UTF8) } else { $null }
if ($existing -cne $content) {
    $temporary = "$output.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllText($temporary, $content, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $output -Force
    }
    finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}
