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
        for source_root in "$CORE_DIRECTORY" "$CLIENT_DIRECTORY" "$HUB_DIRECTORY" "$TESTS_DIRECTORY" AssetTool Scripts/Premake; do
            [[ -d "$root/$source_root" ]] || continue
            find "$root/$source_root" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.lua' \) -print
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

validate_package_stage() {
    local stage="$1" client="$2" hub="$3" core="$4" namespace="$5" path
    local required=("bin/$client" "bin/$hub" "lib/lib$core.a" "lib/lib${namespace}ImGui.a" "Config/Client.json" "include/$namespace/Core.h" "include/$namespace/Log.h" "include/$namespace/Api.h" "include/$namespace/Application.h" "include/$namespace/Assert.h" "include/$namespace/BuildInfo.h" "include/$namespace/EntryPoint.h" "include/$namespace/Event.h" "include/$namespace/Layer.h" "include/$namespace/Ref.h" "include/$namespace/Time.h" "include/$namespace/Project/Project.h" "include/$namespace/Scenes/Scene.h" "include/$namespace/Scenes/SceneAsset.h" "include/$namespace/Scenes/SceneSystem.h" "include/$namespace/Ui.h" "include/$namespace/UiWorkspace.h" "include/$namespace/Window.h" "include/$namespace/WindowConfig.h" "samples/KeireSandbox/ProjectSettings/Project.keireproject" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput" "samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene" "examples/consumer/Main.cpp" "examples/consumer/Client.json" "examples/consumer/CMakeLists.txt" "examples/consumer/README.md" "examples/managed-consumer/ClientApplication.cpp" "examples/managed-consumer/CMakeLists.txt" "examples/managed-consumer/README.md" "third-party/spdlog/spdlog.h" "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst" "third-party/licenses/doctest-LICENSE.txt" "third-party/licenses/nlohmann-json-LICENSE.MIT.txt" "third-party/licenses/dear-imgui-LICENSE.txt" "third-party/SDL3/include/SDL3/SDL.h" "third-party/SDL3/lib/libSDL3.a" "third-party/SDL3/cmake/SDL3Config.cmake" "third-party/SDL3/licenses/SDL3/LICENSE.txt" README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json)
    for path in "${required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required content: %s\n' "$path" >&2; return 1; }
    done
    local asset_required=("bin/${namespace}AssetTool" "lib/lib${namespace}Zstd.a" "include/$namespace/Assets/Asset.h" "include/$namespace/Assets/AssetSystem.h" "include/$namespace/Assets/AssetPipeline.h" "include/$namespace/Assets/InputActionAsset.h" "include/$namespace/Input/Input.h" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput.keiremeta" "third-party/licenses/zstandard-LICENSE.txt")
    for path in "${asset_required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required asset content: %s\n' "$path" >&2; return 1; }
    done
    [[ ! -e "$stage/include/KeireInternal" ]] || { printf 'Package contains private KeireInternal headers.\n' >&2; return 1; }
    find "$stage/lib/cmake" -type f -name '*Config.cmake' -print -quit 2>/dev/null | grep -q . || { printf 'Package is missing its CMake package configuration.\n' >&2; return 1; }
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
            --install-optional) INSTALL_OPTIONAL=1; shift ;;
            *) printf "Unknown argument '%s'.\n" "$1" >&2; return 1 ;;
        esac
    done
}
