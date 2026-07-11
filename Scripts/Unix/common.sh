#!/usr/bin/env bash

config_value() {
    local file="$1" key="$2"
    awk -v key="$key" 'index($0, key "=") == 1 { print substr($0, length(key) + 2); exit }' "$file"
}

load_project_config() {
    local root="$1" file="$1/Config/Project.conf"
    PROJECT_IDENTIFIER="$(config_value "$file" PROJECT_IDENTIFIER)"
    PROJECT_DISPLAY_NAME="$(config_value "$file" PROJECT_DISPLAY_NAME)"
    PROJECT_NAMESPACE="$(config_value "$file" PROJECT_NAMESPACE)"
    CORE_TARGET="$(config_value "$file" CORE_TARGET)"
    CORE_DIRECTORY="$(config_value "$file" CORE_DIRECTORY)"
    CLIENT_TARGET="$(config_value "$file" CLIENT_TARGET)"
    CLIENT_DIRECTORY="$(config_value "$file" CLIENT_DIRECTORY)"
    TESTS_TARGET="$(config_value "$file" TESTS_TARGET)"
    TESTS_DIRECTORY="$(config_value "$file" TESTS_DIRECTORY)"
    ARTIFACT_PREFIX="$(config_value "$file" ARTIFACT_PREFIX)"
    REPOSITORY_SLUG="$(config_value "$file" REPOSITORY_SLUG)"
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
        *:*) printf '%s' "$logical" ;;
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
