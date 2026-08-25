#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
LOCK="$ROOT/Config/Dependencies.lock"
INCLUDE_PROFILE_DEPENDENCIES=0
[[ "${1:-}" == --include-profile-dependencies ]] && INCLUDE_PROFILE_DEPENDENCIES=1
command -v git >/dev/null 2>&1 || { printf 'Git is required.\n' >&2; exit 1; }
git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 || git -C "$ROOT" init

install_dependency() {
    local name="$1" path="$2" url="$3" commit="$4" directory="$ROOT/$2" entry index_commit actual
    entry="$(git -C "$ROOT" ls-files --stage -- "$path" 2>/dev/null || true)"
    if [[ "$entry" == 160000\ * ]]; then
        index_commit="$(printf '%s\n' "$entry" | awk '{print $2}')"
        [[ "$index_commit" == "$commit" ]] || { printf '%s lock is %s; committed submodule pointer is %s.\n' "$name" "$commit" "$index_commit" >&2; return 1; }
        git -C "$ROOT" submodule update --init --recursive -- "$path"
    elif [[ ! -e "$directory" ]]; then git clone --quiet "$url" "$directory"; git -C "$directory" checkout --quiet "$commit"
    elif ! git -C "$directory" rev-parse --is-inside-work-tree >/dev/null 2>&1; then printf '%s is not a Git repository.\n' "$path" >&2; return 1; fi
    actual="$(git -C "$directory" rev-parse HEAD)"
    if [[ "$actual" != "$commit" && ( "$entry" == 160000\ * || "$path" == Build/Dependencies/tracy ) ]]; then
        printf '==> Restoring %s working tree to committed hash %s\n' "$name" "$commit"
        git -C "$directory" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$directory" fetch --no-tags origin "$commit"
        git -C "$directory" checkout --quiet --detach "$commit"
        actual="$(git -C "$directory" rev-parse HEAD)"
    fi
    [[ "$actual" == "$commit" ]] || { printf '%s is at %s; expected %s.\n' "$name" "$actual" "$commit" >&2; return 1; }
    printf '==> %s verified at %s\n' "$name" "$actual"
}

install_dependency spdlog Vendor/spdlog "$(config_value "$LOCK" SPDLOG_URL)" "$(config_value "$LOCK" SPDLOG_COMMIT)"
install_dependency doctest Vendor/doctest "$(config_value "$LOCK" DOCTEST_URL)" "$(config_value "$LOCK" DOCTEST_COMMIT)"
install_dependency SDL Vendor/SDL "$(config_value "$LOCK" SDL_URL)" "$(config_value "$LOCK" SDL_COMMIT)"
install_dependency json Vendor/json "$(config_value "$LOCK" JSON_URL)" "$(config_value "$LOCK" JSON_COMMIT)"
install_dependency imgui Vendor/imgui "$(config_value "$LOCK" IMGUI_URL)" "$(config_value "$LOCK" IMGUI_COMMIT)"
install_dependency zstd Vendor/zstd "$(config_value "$LOCK" ZSTD_URL)" "$(config_value "$LOCK" ZSTD_COMMIT)"
install_dependency entt Vendor/entt "$(config_value "$LOCK" ENTT_URL)" "$(config_value "$LOCK" ENTT_COMMIT)"
install_dependency glm Vendor/glm "$(config_value "$LOCK" GLM_URL)" "$(config_value "$LOCK" GLM_COMMIT)"
install_dependency SDL_shadercross Vendor/SDL_shadercross "$(config_value "$LOCK" SDL_SHADERCROSS_URL)" "$(config_value "$LOCK" SDL_SHADERCROSS_COMMIT)"
install_dependency assimp Vendor/assimp "$(config_value "$LOCK" ASSIMP_URL)" "$(config_value "$LOCK" ASSIMP_COMMIT)"
install_dependency stb Vendor/stb "$(config_value "$LOCK" STB_URL)" "$(config_value "$LOCK" STB_COMMIT)"
if [[ $INCLUDE_PROFILE_DEPENDENCIES -eq 1 ]]; then
  install_dependency Tracy Build/Dependencies/tracy "$(config_value "$LOCK" TRACY_URL)" \
    "$(config_value "$LOCK" TRACY_COMMIT)"
fi
install_dependency ffmpeg Vendor/ffmpeg "$(config_value "$LOCK" FFMPEG_URL)" "$(config_value "$LOCK" FFMPEG_COMMIT)"
git -C "$ROOT/Vendor/SDL_shadercross" submodule update --init --recursive
imgui_files=(
    Scripts/Premake/DearImGui.lua
    Vendor/imgui/imgui.cpp
    Vendor/imgui/imgui_demo.cpp
    Vendor/imgui/imgui_draw.cpp
    Vendor/imgui/imgui_tables.cpp
    Vendor/imgui/imgui_widgets.cpp
    Vendor/imgui/backends/imgui_impl_sdl3.cpp
    Vendor/imgui/backends/imgui_impl_sdlgpu3.cpp
    Vendor/imgui/misc/cpp/imgui_stdlib.cpp
)
for file in "${imgui_files[@]}"; do
    [[ -f "$ROOT/$file" ]] || { printf 'Dear ImGui build integration is incomplete: %s\n' "$file" >&2; exit 1; }
done
zstd_files=(Scripts/Premake/Zstd.lua Vendor/zstd/lib/zstd.h Vendor/zstd/lib/compress/zstd_compress.c Vendor/zstd/lib/decompress/zstd_decompress.c Vendor/zstd/LICENSE)
for file in "${zstd_files[@]}"; do
    [[ -f "$ROOT/$file" ]] || { printf 'Zstandard build integration is incomplete: %s\n' "$file" >&2; exit 1; }
done
header_dependency_files=(Scripts/Premake/HeaderDependencies.lua Vendor/entt/src/entt/entt.hpp Vendor/entt/LICENSE Vendor/glm/glm/glm.hpp Vendor/glm/copying.txt)
for file in "${header_dependency_files[@]}"; do
    [[ -f "$ROOT/$file" ]] || { printf 'ECS/math dependency integration is incomplete: %s\n' "$file" >&2; exit 1; }
done
shadercross_gitlink_paths=(
  external/DirectXShaderCompiler
  external/SPIRV-Cross
  external/SPIRV-Headers
  external/SPIRV-Tools
)
shadercross_gitlink_commits=(
  "$(config_value "$LOCK" SDL_SHADERCROSS_DXC_COMMIT)"
  "$(config_value "$LOCK" SDL_SHADERCROSS_SPIRV_CROSS_COMMIT)"
  "$(config_value "$LOCK" SDL_SHADERCROSS_SPIRV_HEADERS_COMMIT)"
  "$(config_value "$LOCK" SDL_SHADERCROSS_SPIRV_TOOLS_COMMIT)"
)
for ((index = 0; index < ${#shadercross_gitlink_paths[@]}; ++index)); do
  path="${shadercross_gitlink_paths[$index]}"
  expected="${shadercross_gitlink_commits[$index]}"
  actual="$(git -C "$ROOT/Vendor/SDL_shadercross" rev-parse "HEAD:$path")"
  [[ "$actual" == "$expected" ]] || { printf 'SDL_shadercross gitlink %s is %s; expected %s.\n' "$path" "$actual" "$expected" >&2; exit 1; }
done
shadercross_files=(CMakeLists.txt LICENSE.txt src/cli.c include/SDL3_shadercross/SDL_shadercross.h)
for file in "${shadercross_files[@]}"; do
  [[ -f "$ROOT/Vendor/SDL_shadercross/$file" ]] || { printf 'SDL_shadercross source integration is incomplete: %s\n' "$file" >&2; exit 1; }
done
asset_dependency_files=(Vendor/assimp/CMakeLists.txt Vendor/assimp/LICENSE Vendor/assimp/include/assimp/Importer.hpp Vendor/stb/stb_image.h)
for file in "${asset_dependency_files[@]}"; do
  [[ -f "$ROOT/$file" ]] || { printf 'Renderable asset dependency integration is incomplete: %s\n' "$file" >&2; exit 1; }
done
if [[ $INCLUDE_PROFILE_DEPENDENCIES -eq 1 ]]; then
  tracy_files=(Build/Dependencies/tracy/public/TracyClient.cpp Build/Dependencies/tracy/public/tracy/Tracy.hpp Build/Dependencies/tracy/LICENSE)
  for file in "${tracy_files[@]}"; do
    [[ -f "$ROOT/$file" ]] || { printf 'Tracy Profile integration is incomplete: %s\n' "$file" >&2; exit 1; }
  done
fi
printf '==> Vendor libraries are ready; Git staging was not modified\n'
