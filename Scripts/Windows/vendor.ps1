[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Dependencies = @(
    @{ Name = "spdlog"; Path = "Vendor/spdlog"; Url = "https://github.com/gabime/spdlog.git"; Commit = "79524ddd08a4ec981b7fea76afd08ee05f83755d" },
    @{ Name = "doctest"; Path = "Vendor/doctest"; Url = "https://github.com/doctest/doctest.git"; Commit = "2d0a9359a60c51affe2a9bebb1be1dca47868151" }
)

function Invoke-Git([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git command failed: git $($Arguments -join ' ')" }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git is required to install vendor dependencies." }
$insideRepository = ((& git -C $Root rev-parse --is-inside-work-tree 2>&1) -join "") -eq "true"
if (-not $insideRepository) { Invoke-Git -C $Root init }

foreach ($dependency in $Dependencies) {
    $directory = Join-Path $Root $dependency.Path
    $indexEntry = (& git -C $Root ls-files --stage -- $dependency.Path 2>$null) -join ""
    if ($indexEntry.StartsWith("160000 ")) {
        Write-Host "==> Restoring $($dependency.Name) from the committed submodule pointer"
        Invoke-Git -C $Root submodule update --init --recursive -- $dependency.Path
    }
    elseif (-not (Test-Path $directory)) {
        Write-Host "==> Cloning $($dependency.Name) at its pinned commit"
        Invoke-Git clone --quiet $dependency.Url $directory
        Invoke-Git -C $directory checkout --quiet $dependency.Commit
    }
    elseif (-not (((& git -C $directory rev-parse --is-inside-work-tree 2>&1) -join "") -eq "true")) {
        throw "$($dependency.Path) exists but is not a Git repository."
    }

    $actualCommit = (& git -C $directory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $dependency.Commit) {
        throw "$($dependency.Name) is at $actualCommit; expected pinned commit $($dependency.Commit)."
    }
    Write-Host "==> $($dependency.Name) verified at $actualCommit"
}

Write-Host "==> Vendor libraries are ready; Git staging was not modified"
