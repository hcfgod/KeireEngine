#!/usr/bin/env bash

config_value() {
    local file="$1" key="$2"
    awk -v key="$key" 'index($0, key "=") == 1 { value = substr($0, length(key) + 2); sub(/\r$/, "", value); print value; exit }' "$file"
}

load_project_config() {
    local root="$1" file="$1/Config/Project.conf"
    local line key required
    declare -A seen=()
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        [[ "$line" != *$'\r'* ]] || { printf 'Configuration contains an embedded carriage return: %s\n' "$file" >&2; return 1; }
        [[ -z "$line" ]] && continue
        [[ "$line" =~ ^([A-Z0-9_]+)=(.*)$ ]] || { printf 'Malformed configuration line in %s: %s\n' "$file" "$line" >&2; return 1; }
        key="${BASH_REMATCH[1]}"
        [[ -z "${seen[$key]+present}" ]] || { printf "Duplicate key '%s' in %s.\n" "$key" "$file" >&2; return 1; }
        seen[$key]=1
    done < "$file"
    for required in PROJECT_IDENTIFIER PROJECT_DISPLAY_NAME PROJECT_VERSION PROJECT_NAMESPACE PROJECT_MACRO_PREFIX CORE_TARGET CORE_DIRECTORY CLIENT_TARGET CLIENT_DIRECTORY HUB_TARGET HUB_DIRECTORY TESTS_TARGET TESTS_DIRECTORY ARTIFACT_PREFIX REPOSITORY_SLUG; do
        [[ -n "${seen[$required]+present}" ]] || { printf "Project configuration is missing '%s'.\n" "$required" >&2; return 1; }
    done
    PROJECT_IDENTIFIER="$(config_value "$file" PROJECT_IDENTIFIER)"
    PROJECT_DISPLAY_NAME="$(config_value "$file" PROJECT_DISPLAY_NAME)"
    PROJECT_VERSION="$(config_value "$file" PROJECT_VERSION)"
    PROJECT_NAMESPACE="$(config_value "$file" PROJECT_NAMESPACE)"
    PROJECT_MACRO_PREFIX="$(config_value "$file" PROJECT_MACRO_PREFIX)"
    CORE_TARGET="$(config_value "$file" CORE_TARGET)"
    CORE_DIRECTORY="$(config_value "$file" CORE_DIRECTORY)"
    CLIENT_TARGET="$(config_value "$file" CLIENT_TARGET)"
    CLIENT_DIRECTORY="$(config_value "$file" CLIENT_DIRECTORY)"
    HUB_TARGET="$(config_value "$file" HUB_TARGET)"
    HUB_DIRECTORY="$(config_value "$file" HUB_DIRECTORY)"
    TESTS_TARGET="$(config_value "$file" TESTS_TARGET)"
    TESTS_DIRECTORY="$(config_value "$file" TESTS_DIRECTORY)"
    ARTIFACT_PREFIX="$(config_value "$file" ARTIFACT_PREFIX)"
    REPOSITORY_SLUG="$(config_value "$file" REPOSITORY_SLUG)"
    is_semantic_version "$PROJECT_VERSION" || { printf 'PROJECT_VERSION must be a valid Semantic Version 2.0.0 value.\n' >&2; return 1; }
}

project_generation_fingerprint() {
    local root="$1" source_root
    {
        for source_root in "$CORE_DIRECTORY" "$CLIENT_DIRECTORY" "$HUB_DIRECTORY" "$TESTS_DIRECTORY" \
          AssetTool KeireAssetWorker KeireRuntime KeireManaged Scripts/Premake; do
            [[ -d "$root/$source_root" ]] || continue
            find "$root/$source_root" -type f \( \
              -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
              -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.lua' \
              -o -name '*.cs' -o -name '*.csproj' \
            \) -print
        done | LC_ALL=C sort
        find "$root" -type f -name 'premake5.lua' -not -path "$root/Build/*" -not -path "$root/Vendor/*" -not -path "$root/Tools/*" -print | LC_ALL=C sort | while IFS= read -r source_root; do
            cksum "$source_root"
        done
        cksum "$root/Config/Project.conf" "$root/Config/Dependencies.lock"
    } | cksum | awk '{ print $1 "-" $2 }'
}

is_semantic_version() {
    [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-((0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(\+([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$ ]]
}

package_worktree_policy() {
    local root="$1" allow_dirty="${2:-0}" ci="${3:-0}" status dirty=false
    [[ "$ci" == 0 || "$allow_dirty" == 0 ]] || { printf '%s\n' '--allow-dirty cannot be used in CI.' >&2; return 1; }
    git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1 || { printf '%s\n' 'Release packaging requires a Git working tree.' >&2; return 1; }
    status="$(git -C "$root" status --porcelain --untracked-files=normal)" || { printf 'Unable to inspect the package worktree at %s.\n' "$root" >&2; return 1; }
    [[ -z "$status" ]] || dirty=true
    [[ "$dirty" == false || "$allow_dirty" == 1 ]] || { printf '%s\n' 'Release packaging requires a clean worktree. Use --allow-dirty only for a local development artifact.' >&2; return 1; }
    printf '%s %s\n' "$dirty" "$([[ "$dirty" == true && "$allow_dirty" == 1 ]] && printf true || printf false)"
}

json_escape() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    value="${value//$'\n'/\\n}"
    value="${value//$'\r'/\\r}"
    value="${value//$'\t'/\\t}"
    printf '%s' "$value"
}

identifier_to_macro_prefix() {
    printf '%s' "$1" | sed -E 's/([A-Z]+)([A-Z][a-z])/\1_\2/g; s/([a-z0-9])([A-Z])/\1_\2/g' | tr '[:lower:]' '[:upper:]'
}

copy_tracked_tree() {
    local repository_root="$1" relative_source="$2" destination="$3" tracked_file relative_path
    git -C "$repository_root" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
        printf 'Tracked package copies require a Git working tree: %s\n' "$repository_root" >&2
        return 1
    }

    mkdir -p "$destination"
    local copied=0
    while IFS= read -r -d '' tracked_file; do
        [[ "$tracked_file" == "$relative_source/"* ]] || {
            printf "Tracked package path escaped '%s': %s\n" "$relative_source" "$tracked_file" >&2
            return 1
        }
        [[ -f "$repository_root/$tracked_file" ]] || continue
        relative_path="${tracked_file#"$relative_source/"}"
        mkdir -p "$destination/$(dirname "$relative_path")"
        cp "$repository_root/$tracked_file" "$destination/$relative_path"
        copied=1
    done < <(git -c core.quotepath=false -C "$repository_root" ls-files -z -- "$relative_source")
    [[ "$copied" == 1 ]] || { printf "No tracked files were found for package source '%s'.\n" "$relative_source" >&2; return 1; }
}

is_generated_package_path() {
    local path="/${1//\\//}/" name
    path="$(printf '%s' "$path" | sed -E 's#^/include/[^/]+/Build/#/include/PublicApi/#')"
    path="$(printf '%s' "$path" | sed -E 's#^/bin/Managed/Dotnet/#/bundled-dotnet/#; s#/bundled-dotnet/(.*)/Build/#/bundled-dotnet/\1/DotnetBuild/#')"
    [[ "$path" =~ /(Library|Logs|Build|Temp|SceneRecovery|Recovery)/ ]] && return 0
    name="${path%/}"; name="${name##*/}"
    [[ "$name" =~ (^|[._-])[Rr][Ee][Cc][Oo][Vv][Ee][Rr][Yy]([._-]|$) || "$name" =~ \.[Tt][Mm][Pp]$ ]]
}

assert_package_generated_data_free() {
    local stage="$1" entry relative
    while IFS= read -r -d '' entry; do
        relative="${entry#"$stage/"}"
        if is_generated_package_path "$relative"; then
            printf 'Package contains generated workspace data: %s\n' "$relative" >&2
            return 1
        fi
    done < <(find "$stage" -mindepth 1 -print0)
}

assert_package_archive_generated_data_free() {
    local archive="$1" entry
    while IFS= read -r entry; do
        entry="${entry#./}"
        if is_generated_package_path "$entry"; then
            printf 'Package archive contains generated workspace data: %s\n' "$entry" >&2
            return 1
        fi
    done < <(tar -tzf "$archive")
}

validate_package_stage() {
    local stage="$1" client="$2" hub="$3" core="$4" namespace="$5" path
    local required=("bin/$client" "bin/$hub" "bin/Managed/Coral.Managed.dll" "bin/Managed/Keire.Managed.dll" "lib/lib$core.a" "lib/lib${namespace}ImGui.a" "Config/Client.json" "Config/SourceModules.premake.lua" "include/$namespace/Core.h" "include/$namespace/Log.h" "include/$namespace/Api.h" "include/$namespace/Application.h" "include/$namespace/Assert.h" "include/$namespace/BuildInfo.h" "include/$namespace/EntryPoint.h" "include/$namespace/Event.h" "include/$namespace/Layer.h" "include/$namespace/Ref.h" "include/$namespace/Time.h" "include/$namespace/Undo.h" "include/$namespace/Project/Project.h" "include/$namespace/Scenes/Scene.h" "include/$namespace/Scenes/SceneAsset.h" "include/$namespace/Scenes/SceneSystem.h" "include/$namespace/Ui.h" "include/$namespace/UiWorkspace.h" "include/$namespace/Window.h" "include/$namespace/WindowConfig.h" "samples/KeireSandbox/ProjectSettings/Project.keireproject" "samples/KeireSandbox/ProjectSettings/Rendering.keiresettings" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput" "samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene" "examples/consumer/Main.cpp" "examples/consumer/Client.json" "examples/consumer/CMakeLists.txt" "examples/consumer/README.md" "examples/managed-consumer/ClientApplication.cpp" "examples/managed-consumer/CMakeLists.txt" "examples/managed-consumer/README.md" "examples/source-module/ClientApplication.cpp" "examples/source-module/GameplayModule.cpp" "examples/source-module/GameplayModule.h" "examples/source-module/CMakeLists.txt" "examples/source-module/README.md" "docs/PlayerBuilds.md" "docs/Diagnostics/KEIRE-AUDIO-0001.md" "docs/Diagnostics/KEIRE-REPLAY-0001.md" "docs/Diagnostics/KEIRE-REPLAY-0002.md" "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst" "third-party/licenses/doctest-LICENSE.txt" "third-party/licenses/nlohmann-json-LICENSE.MIT.txt" "third-party/licenses/dear-imgui-LICENSE.txt" "third-party/licenses/Coral-LICENSE.txt" "third-party/licenses/dotnet-LICENSE.txt" "third-party/licenses/dotnet-ThirdPartyNotices.txt" "third-party/SDL3/include/SDL3/SDL.h" "third-party/SDL3/lib/libSDL3.a" "third-party/SDL3/cmake/SDL3Config.cmake" "third-party/SDL3/licenses/SDL3/LICENSE.txt" README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json)
    required+=("bin/KeireShaderCompiler")
    for path in "${required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required content: %s\n' "$path" >&2; return 1; }
    done
    local asset_required=("bin/${namespace}AssetTool" "bin/${namespace}AssetWorker" "bin/${namespace}Runtime" "lib/lib${namespace}Zstd.a" "include/$namespace/Math/Math.h" "include/$namespace/ECS/Component.h" "include/$namespace/ECS/Entity.h" "include/$namespace/ECS/Components/TransformComponent.h" "include/$namespace/ECS/Components/DirectionalLightComponent.h" "include/$namespace/ECS/Components/AudioComponents.h" "include/$namespace/ECS/Components/RuntimeUiComponents.h" "include/$namespace/Assets/Asset.h" "include/$namespace/Assets/AssetSystem.h" "include/$namespace/Assets/AssetPipeline.h" "include/$namespace/Assets/InputActionAsset.h" "include/$namespace/Input/Input.h" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput.keiremeta" "third-party/licenses/zstandard-LICENSE.txt" "third-party/licenses/entt-LICENSE.txt" "third-party/licenses/glm-COPYING.txt")
    asset_required+=("include/$namespace/ECS/Components/CameraComponent.h" "include/$namespace/ECS/Components/MeshRendererComponent.h" "include/$namespace/Rendering/RenderSystem.h" "include/$namespace/Assets/RenderingAssets.h" "samples/KeireSandbox/Assets/Shaders/DefaultUnlit.keireshader" "samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl" "samples/KeireSandbox/Assets/Materials/DefaultUnlit.keirematerial" "third-party/licenses/SDL-shadercross-LICENSE.txt" "third-party/licenses/DirectXShaderCompiler-LICENSE.txt" "third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt" "third-party/licenses/SPIRV-Cross-LICENSE.txt" "third-party/licenses/SPIRV-Headers-LICENSE.txt" "third-party/licenses/SPIRV-Tools-LICENSE.txt" "third-party/licenses/assimp-LICENSE.txt" "third-party/licenses/assimp-zlib-LICENSE.txt" "third-party/licenses/stb-LICENSE.txt" "third-party/licenses/Jolt-LICENSE.txt" "third-party/licenses/Recast-LICENSE.txt" "third-party/licenses/miniaudio-LICENSE.txt" "lib/libassimp.a" "lib/libzlibstatic.a" "lib/libJolt.a" "lib/libRecast.a" "lib/libDetour.a" "lib/libDetourCrowd.a" "lib/libDetourTileCache.a" "lib/libminiaudio.a" "lib/libCoral.Native.a" "lib/libnethost.a")
    for path in "${asset_required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required asset content: %s\n' "$path" >&2; return 1; }
    done
    [[ ! -e "$stage/include/KeireInternal" ]] || { printf 'Package contains private KeireInternal headers.\n' >&2; return 1; }
    [[ ! -e "$stage/third-party/spdlog" ]] || { printf 'Package contains private spdlog headers.\n' >&2; return 1; }
    [[ ! -e "$stage/third-party/assimp" && ! -e "$stage/third-party/stb" && ! -e "$stage/third-party/SDL3/include/Jolt" && ! -e "$stage/third-party/SDL3/include/recastnavigation" && ! -e "$stage/third-party/SDL3/include/miniaudio" ]] || { printf 'Package contains private implementation headers.\n' >&2; return 1; }
    find "$stage/lib/cmake" -type f -name '*Config.cmake' -print -quit 2>/dev/null | grep -q . || { printf 'Package is missing its CMake package configuration.\n' >&2; return 1; }
    assert_package_generated_data_free "$stage"
}

editor_package_required_paths() {
    local client="$1" hub="$2" namespace="$4"
    local required=(
      "bin/$client" "bin/$hub" "bin/${namespace}AssetTool" "bin/${namespace}AssetWorker"
      "bin/${namespace}Runtime" "bin/KeireShaderCompiler" "bin/Managed/Coral.Managed.dll"
      "bin/Managed/Coral.Managed.deps.json" "bin/Managed/Coral.Managed.runtimeconfig.json"
      "bin/Managed/Keire.Managed.dll" "bin/Managed/Dotnet/dotnet" "Config/Client.json"
      "samples/KeireSandbox/ProjectSettings/Project.keireproject"
      "samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene" "docs/PlayerBuilds.md" "README.md"
      "LICENSE.txt" "THIRD_PARTY_NOTICES.md" "build-manifest.json" "editor-package.json" "launch-editor.sh"
      "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst"
      "third-party/licenses/doctest-LICENSE.txt" "third-party/licenses/nlohmann-json-LICENSE.MIT.txt"
      "third-party/licenses/dear-imgui-LICENSE.txt" "third-party/licenses/zstandard-LICENSE.txt"
      "third-party/licenses/entt-LICENSE.txt" "third-party/licenses/glm-COPYING.txt"
      "third-party/licenses/SDL-shadercross-LICENSE.txt"
      "third-party/licenses/DirectXShaderCompiler-LICENSE.txt"
      "third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt"
      "third-party/licenses/SPIRV-Cross-LICENSE.txt" "third-party/licenses/SPIRV-Headers-LICENSE.txt"
      "third-party/licenses/SPIRV-Tools-LICENSE.txt" "third-party/licenses/assimp-LICENSE.txt"
      "third-party/licenses/assimp-zlib-LICENSE.txt" "third-party/licenses/stb-LICENSE.txt"
      "third-party/licenses/Jolt-LICENSE.txt" "third-party/licenses/Recast-LICENSE.txt"
      "third-party/licenses/miniaudio-LICENSE.txt" "third-party/licenses/Coral-LICENSE.txt"
      "third-party/licenses/dotnet-LICENSE.txt" "third-party/licenses/dotnet-ThirdPartyNotices.txt"
    )
    printf '%s\n' "${required[@]}"
}

validate_editor_package_stage() {
    local stage="$1" client="$2" hub="$3" core="$4" namespace="$5" platform="$6" path pattern
    while IFS= read -r path; do
        [[ -f "$stage/$path" ]] || {
            printf 'Editor package is missing required content: %s\n' "$path" >&2
            return 1
        }
    done < <(editor_package_required_paths "$client" "$hub" "$core" "$namespace")
    for path in "bin/$client" "bin/$hub" "bin/${namespace}AssetTool" "bin/${namespace}AssetWorker" \
      "bin/${namespace}Runtime" "bin/KeireShaderCompiler" "bin/Managed/Dotnet/dotnet" "launch-editor.sh"; do
        [[ -x "$stage/$path" ]] || {
            printf 'Editor package entry is not executable: %s\n' "$path" >&2
            return 1
        }
    done
    find "$stage/bin/Managed/Dotnet/sdk" -mindepth 1 -maxdepth 1 -type d -name '10.*' -print -quit 2>/dev/null |
      grep -q . || { printf 'Editor package does not contain the .NET 10 SDK.\n' >&2; return 1; }
    for pattern in 'libavcodec.*' 'libavformat.*' 'libavutil.*' 'libswresample.*'; do
        find "$stage/bin" -maxdepth 1 -type f -name "$pattern" -print -quit | grep -q . || {
            printf "Editor package is missing an FFmpeg runtime matching '%s'.\n" "$pattern" >&2
            return 1
        }
    done
    for path in include lib examples; do
        [[ ! -e "$stage/$path" ]] || {
            printf 'Editor package contains SDK-only content: %s\n' "$path" >&2
            return 1
        }
    done
    if [[ "$platform" == Mac ]]; then
        [[ -x "$stage/${hub}.app/Contents/MacOS/${hub}Launcher" &&
           -f "$stage/${hub}.app/Contents/Info.plist" ]] || {
            printf 'macOS editor package is missing its application bundle launcher.\n' >&2
            return 1
        }
    fi
    assert_package_generated_data_free "$stage"
}

resolve_unix_toolset() {
    local platform="$1" requested="$2"
    if [[ "$requested" != default ]]; then printf '%s' "$requested"
    elif [[ "$platform" == Mac ]]; then printf 'clang'
    else printf 'gcc'; fi
}

native_architecture() {
    case "$(uname -m)" in
        x86_64|amd64) printf 'x86_64' ;;
        arm64|aarch64) printf 'ARM64' ;;
        *) printf "Unsupported host architecture '%s'.\n" "$(uname -m)" >&2; return 1 ;;
    esac
}

normalize_architecture() {
    local value
    value="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$value" in
        x64|amd64|x86_64) printf 'x86_64' ;;
        arm64|aarch64) printf 'ARM64' ;;
        *) printf "Unsupported architecture '%s'. Expected x86_64 or ARM64.\n" "$1" >&2; return 1 ;;
    esac
}

normalize_configuration() {
    local value
    value="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$value" in
        debug) printf 'Debug' ;;
        release) printf 'Release' ;;
        dist) printf 'Dist' ;;
        debugasan) printf 'DebugASan' ;;
        debugubsan) printf 'DebugUBSan' ;;
        debugtsan) printf 'DebugTSan' ;;
        coverage) printf 'Coverage' ;;
        *) printf "Unsupported configuration '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

premake_architecture() {
    [[ "$(normalize_architecture "$1")" == ARM64 ]] && printf 'aarch64' || printf 'x86_64'
}

architecture_output_name() {
    [[ "$(normalize_architecture "$1")" == ARM64 ]] && printf 'AARCH64' || printf 'x86_64'
}

find_premake_binary() {
    find "$1" -type f -name premake5 -print -quit
}

validate_unix_combination() {
    local platform="$1" generator="$2" toolset="$3"
    if [[ "$toolset" == "msc" ]]; then
        printf 'The MSVC toolset is available only on Windows.\n' >&2
        return 1
    fi
    if [[ "$platform" == "Mac" && "$generator" == "xcode4" && "$toolset" != "default" && "$toolset" != "clang" ]]; then
        printf 'Xcode supports only the default or Clang toolset.\n' >&2
        return 1
    fi
}

mac_requires_full_xcode() {
    [[ "$1" == xcode4 ]]
}

version_at_least() {
    local actual="$1" minimum="$2" index actual_part minimum_part
    local IFS=.
    read -r -a actual_parts <<< "$actual"
    read -r -a minimum_parts <<< "$minimum"
    for ((index = 0; index < ${#actual_parts[@]} || index < ${#minimum_parts[@]}; ++index)); do
        actual_part="${actual_parts[index]:-0}"; actual_part="${actual_part%%[^0-9]*}"
        minimum_part="${minimum_parts[index]:-0}"; minimum_part="${minimum_part%%[^0-9]*}"
        ((10#${actual_part:-0} > 10#${minimum_part:-0})) && return 0
        ((10#${actual_part:-0} < 10#${minimum_part:-0})) && return 1
    done
    return 0
}

extract_version() {
    awk '
        match($0, /[0-9]+([.][0-9]+)*/) && !found {
            print substr($0, RSTART, RLENGTH)
            found = 1
        }
        END { if (!found) exit 1 }
    '
}

resolve_llvm_tool() {
    local tool="$1" compiler="${2:-clang++}" compiler_path compiler_version compiler_major
    local candidate candidate_path tool_version
    compiler_path="$(command -v "$compiler" 2>/dev/null)" || { printf '%s was not found.\n' "$compiler" >&2; return 1; }
    compiler_version="$("$compiler_path" --version | extract_version)" || return 1
    compiler_major="${compiler_version%%.*}"

    for candidate in "$(dirname "$compiler_path")/$tool" "$tool-$compiler_major" "$tool$compiler_major" "$tool"; do
        if [[ "$candidate" == */* ]]; then
            [[ -x "$candidate" ]] || continue
            candidate_path="$candidate"
        else
            candidate_path="$(command -v "$candidate" 2>/dev/null)" || continue
        fi
        tool_version="$("$candidate_path" --version 2>/dev/null | extract_version)" || continue
        if [[ "${tool_version%%.*}" == "$compiler_major" ]]; then
            printf '%s' "$candidate_path"
            return 0
        fi
    done

    printf 'No %s matching Clang major version %s was found.\n' "$tool" "$compiler_major" >&2
    return 1
}

package_name() {
    local manager="$1" logical="$2"
    case "$manager:$logical" in
        apt-get:ninja|dnf:ninja|zypper:ninja) printf 'ninja-build' ;;
        pacman:ninja) printf 'ninja' ;;
        apt-get:cxx) printf 'g++' ;;
        dnf:cxx|zypper:cxx) printf 'gcc-c++' ;;
        pacman:cxx) printf 'gcc' ;;
        apt-get:build) printf 'build-essential' ;;
        dnf:build|zypper:build) printf 'gcc-c++ make' ;;
        pacman:build) printf 'base-devel' ;;
        pacman:python) printf 'python' ;;
        *:python) printf 'python3' ;;
        apt-get:uuid) printf 'uuid-dev' ;;
        dnf:uuid|zypper:uuid) printf 'libuuid-devel' ;;
        pacman:uuid) printf 'util-linux-libs' ;;
        apt-get:sdl-video) printf 'libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libdrm-dev libgbm-dev' ;;
        dnf:sdl-video) printf 'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel wayland-devel libxkbcommon-devel libdrm-devel mesa-libgbm-devel' ;;
        pacman:sdl-video) printf 'libx11 libxext libxrandr libxcursor libxfixes libxi libxss wayland libxkbcommon libdrm mesa' ;;
        zypper:sdl-video) printf 'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXss-devel wayland-devel libxkbcommon-devel libdrm-devel Mesa-libgbm-devel' ;;
        *:llvm) printf 'llvm' ;;
        *:*) printf '%s' "$logical" ;;
    esac
}

package_install_arguments() {
    case "$1" in
        apt-get|dnf) printf '%s\n' -y ;;
        pacman) printf '%s\n' -Syu --needed --noconfirm ;;
        zypper) printf '%s\n' --non-interactive install ;;
        *) printf "Unsupported package manager '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

run_checked() {
    "$@"
    local status=$?
    if [[ $status -ne 0 ]]; then
        printf "Command failed with exit code %s: %s\n" "$status" "$*" >&2
        return "$status"
    fi
}

parse_build_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --generator) GENERATOR="$2"; shift 2 ;;
            --configuration) CONFIGURATION="$(normalize_configuration "$2")"; shift 2 ;;
            --architecture) ARCHITECTURE="$(normalize_architecture "$2")"; shift 2 ;;
            --toolset) TOOLSET="$2"; shift 2 ;;
            --target) TARGET="$2"; shift 2 ;;
            --ci) CI=1; shift ;;
            --update) UPDATE=1; shift ;;
            --force) FORCE=1; shift ;;
            --allow-dirty) ALLOW_DIRTY=1; shift ;;
            --install-optional) INSTALL_OPTIONAL=1; shift ;;
            *) printf "Unknown argument '%s'.\n" "$1" >&2; return 1 ;;
        esac
    done
}
