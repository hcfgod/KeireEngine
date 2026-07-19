[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Lock = Get-DependencyLock
$Dependencies = @(
    @{ Name = "spdlog"; Path = "Vendor/spdlog"; Url = $Lock.SPDLOG_URL; Commit = $Lock.SPDLOG_COMMIT },
    @{ Name = "doctest"; Path = "Vendor/doctest"; Url = $Lock.DOCTEST_URL; Commit = $Lock.DOCTEST_COMMIT },
    @{ Name = "SDL"; Path = "Vendor/SDL"; Url = $Lock.SDL_URL; Commit = $Lock.SDL_COMMIT },
    @{ Name = "json"; Path = "Vendor/json"; Url = $Lock.JSON_URL; Commit = $Lock.JSON_COMMIT },
    @{ Name = "imgui"; Path = "Vendor/imgui"; Url = $Lock.IMGUI_URL; Commit = $Lock.IMGUI_COMMIT },
    @{ Name = "zstd"; Path = "Vendor/zstd"; Url = $Lock.ZSTD_URL; Commit = $Lock.ZSTD_COMMIT },
    @{ Name = "entt"; Path = "Vendor/entt"; Url = $Lock.ENTT_URL; Commit = $Lock.ENTT_COMMIT },
    @{ Name = "glm"; Path = "Vendor/glm"; Url = $Lock.GLM_URL; Commit = $Lock.GLM_COMMIT },
    @{ Name = "SDL_shadercross"; Path = "Vendor/SDL_shadercross"; Url = $Lock.SDL_SHADERCROSS_URL; Commit = $Lock.SDL_SHADERCROSS_COMMIT }
)

function Invoke-Git([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git command failed: git $($Arguments -join ' ')" }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git is required to install vendor dependencies." }
if (-not (Get-GitWorktreeRoot $Root)) { Invoke-Git -C $Root init }

foreach ($dependency in $Dependencies) {
    $directory = Join-Path $Root $dependency.Path
    $indexEntry = (& git -C $Root ls-files --stage -- $dependency.Path 2>$null) -join ""
    if ($indexEntry.StartsWith("160000 ")) {
        $indexCommit = ($indexEntry -split '\s+')[1]
        if ($indexCommit -ne $dependency.Commit) {
            throw "$($dependency.Name) lock is $($dependency.Commit); committed submodule pointer is $indexCommit."
        }
        Write-Host "==> Restoring $($dependency.Name) from the committed submodule pointer"
        Invoke-Git -C $Root submodule update --init --recursive -- $dependency.Path
    }
    elseif (-not (Test-Path $directory)) {
        Write-Host "==> Cloning $($dependency.Name) at its pinned commit"
        Invoke-Git clone --quiet $dependency.Url $directory
        Invoke-Git -C $directory checkout --quiet $dependency.Commit
    }
    elseif (-not (Test-GitRepository $directory)) {
        throw "$($dependency.Path) exists but is not a Git repository."
    }

    $actualCommit = (& git -C $directory rev-parse HEAD).Trim()
    if ($actualCommit -ne $dependency.Commit -and $indexEntry.StartsWith("160000 ")) {
        Write-Host "==> Restoring $($dependency.Name) working tree to committed hash $($dependency.Commit)"
        & git -C $directory cat-file -e "$($dependency.Commit)^{commit}" 2>$null
        if ($LASTEXITCODE -ne 0) { Invoke-Git -C $directory fetch --no-tags origin $dependency.Commit }
        Invoke-Git -C $directory checkout --quiet --detach $dependency.Commit
        $actualCommit = (& git -C $directory rev-parse HEAD).Trim()
    }
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $dependency.Commit) {
        throw "$($dependency.Name) is at $actualCommit; expected pinned commit $($dependency.Commit)."
    }
    Write-Host "==> $($dependency.Name) verified at $actualCommit"
}

Invoke-Git -C (Join-Path $Root "Vendor\SDL_shadercross") submodule update --init --recursive

$imguiIntegration = Join-Path $Root "Scripts\Premake\DearImGui.lua"
$imguiFiles = @(
    $imguiIntegration,
    (Join-Path $Root "Vendor\imgui\imgui.cpp"),
    (Join-Path $Root "Vendor\imgui\imgui_demo.cpp"),
    (Join-Path $Root "Vendor\imgui\imgui_draw.cpp"),
    (Join-Path $Root "Vendor\imgui\imgui_tables.cpp"),
    (Join-Path $Root "Vendor\imgui\imgui_widgets.cpp"),
    (Join-Path $Root "Vendor\imgui\backends\imgui_impl_sdl3.cpp"),
    (Join-Path $Root "Vendor\imgui\backends\imgui_impl_sdlgpu3.cpp"),
    (Join-Path $Root "Vendor\imgui\misc\cpp\imgui_stdlib.cpp")
)
foreach ($file in $imguiFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Dear ImGui build integration is incomplete: $file"
    }
}

$zstdFiles = @(
    (Join-Path $Root "Scripts\Premake\Zstd.lua"),
    (Join-Path $Root "Vendor\zstd\lib\zstd.h"),
    (Join-Path $Root "Vendor\zstd\lib\compress\zstd_compress.c"),
    (Join-Path $Root "Vendor\zstd\lib\decompress\zstd_decompress.c"),
    (Join-Path $Root "Vendor\zstd\LICENSE")
)
foreach ($file in $zstdFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Zstandard build integration is incomplete: $file"
    }
}

$headerDependencyFiles = @(
    (Join-Path $Root "Scripts\Premake\HeaderDependencies.lua"),
    (Join-Path $Root "Vendor\entt\src\entt\entt.hpp"),
    (Join-Path $Root "Vendor\entt\LICENSE"),
    (Join-Path $Root "Vendor\glm\glm\glm.hpp"),
    (Join-Path $Root "Vendor\glm\copying.txt")
)
foreach ($file in $headerDependencyFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "ECS/math dependency integration is incomplete: $file"
    }
}

$shaderCrossGitlinks = @{
    "external/DirectXShaderCompiler" = $Lock.SDL_SHADERCROSS_DXC_COMMIT
    "external/SPIRV-Cross" = $Lock.SDL_SHADERCROSS_SPIRV_CROSS_COMMIT
    "external/SPIRV-Headers" = $Lock.SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT
    "external/SPIRV-Tools" = $Lock.SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT
}
foreach ($entry in $shaderCrossGitlinks.GetEnumerator()) {
    $actual = (& git -C (Join-Path $Root "Vendor\SDL_shadercross") rev-parse "HEAD:$($entry.Key)").Trim()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $entry.Value) {
        throw "SDL_shadercross gitlink $($entry.Key) is $actual; expected $($entry.Value)."
    }
}
foreach ($file in @("CMakeLists.txt", "LICENSE.txt", "src\cli.c", "include\SDL3_shadercross\SDL_shadercross.h")) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root "Vendor\SDL_shadercross\$file") -PathType Leaf)) {
        throw "SDL_shadercross source integration is incomplete: $file"
    }
}

Write-Host "==> Vendor libraries are ready; Git staging was not modified"
