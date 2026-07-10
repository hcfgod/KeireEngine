[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$VendorDir = Join-Path $Root "Vendor"
$Dependencies = @(
    @{
        Name = "spdlog"
        Path = "Vendor/spdlog"
        Url = "https://github.com/gabime/spdlog.git"
        Tag = "v1.17.0"
    },
    @{
        Name = "doctest"
        Path = "Vendor/doctest"
        Url = "https://github.com/doctest/doctest.git"
        Tag = "v2.5.3"
    }
)

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message"
}

function Test-Command {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-Git {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$GitArgs
    )

    & git @GitArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed: git $($GitArgs -join ' ')"
    }
}

function Test-GitRepository {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return $false
    }

    if (-not (Test-Path (Join-Path $Path ".git"))) {
        return $false
    }

    $inside = & git -C $Path rev-parse --is-inside-work-tree 2>$null
    return $LASTEXITCODE -eq 0 -and ($inside -join "") -eq "true"
}

function Test-GitIndexPath {
    param([string]$Path)

    & git -C $Root ls-files --error-unmatch $Path *> $null
    return $LASTEXITCODE -eq 0
}

if (-not (Test-Command "git")) {
    throw "Git is required to install vendor submodules."
}

if (-not (Test-GitRepository $Root)) {
    Write-Step "Initializing Git repository"
    Invoke-Git -C $Root init
}

New-Item -ItemType Directory -Force -Path $VendorDir | Out-Null

function Install-VendorDependency {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Dependency
    )

    $dependencyPath = $Dependency.Path
    $dependencyDir = Join-Path $Root $dependencyPath
    $dependencyName = $Dependency.Name
    $dependencyUrl = $Dependency.Url
    $dependencyTag = $Dependency.Tag

    if (Test-Path $dependencyDir) {
        if (-not (Test-GitRepository $dependencyDir)) {
            throw "$dependencyPath already exists but is not a Git repository or submodule. Move it aside before bootstrapping vendor libraries."
        }

        Write-Step "Updating $dependencyName submodule"
        Invoke-Git -C $Root submodule update --init --recursive -- $dependencyPath
    }
    elseif (Test-GitIndexPath $dependencyPath) {
        Write-Step "Restoring $dependencyName submodule"
        Invoke-Git -C $Root submodule update --init --recursive -- $dependencyPath
    }
    else {
        Write-Step "Adding $dependencyName submodule"
        Invoke-Git -C $Root submodule add $dependencyUrl $dependencyPath
    }

    Write-Step "Checking out $dependencyName $dependencyTag"
    Invoke-Git -C $dependencyDir fetch --tags --force --quiet
    Invoke-Git -C $dependencyDir checkout --quiet $dependencyTag

    Write-Step "Recording $dependencyName submodule pointer"
    Invoke-Git -C $Root add .gitmodules $dependencyPath
}

foreach ($dependency in $Dependencies) {
    Install-VendorDependency -Dependency $dependency
}

Write-Step "Vendor libraries are ready"
