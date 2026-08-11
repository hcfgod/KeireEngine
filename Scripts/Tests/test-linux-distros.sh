#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

suite="test"
distro=all
configuration=Debug
jobs="$(build_parallel_jobs)"
pull=missing
current_container=""
declare -a selected=()

usage() {
    cat <<'EOF'
Usage: bash Scripts/Tests/test-linux-distros.sh [options]

Options:
  --suite <bootstrap|test>     Validation depth (default: test)
  --distro <name|all>         ubuntu-22.04, ubuntu-24.04, ubuntu-26.04,
                              debian-12, fedora, arch, tumbleweed, rocky-9,
                              or all (default: all)
  --configuration <name>      Debug or Release for the test suite (default: Debug)
  --jobs <count>              Compiler workers per container (default: at most 4)
  --refresh-images            Pull newer images before testing
  --help                      Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --suite) suite="${2:?--suite requires a value}"; shift 2 ;;
        --distro) distro="${2:?--distro requires a value}"; shift 2 ;;
        --configuration) configuration="$(normalize_configuration "${2:?--configuration requires a value}")"; shift 2 ;;
        --jobs) jobs="${2:?--jobs requires a value}"; shift 2 ;;
        --refresh-images) pull=always; shift ;;
        --help) usage; exit 0 ;;
        *) printf 'Unknown Linux distro test option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$suite" == bootstrap || "$suite" == test ]] || {
    printf 'Linux distro suite must be bootstrap or test.\n' >&2
    exit 2
}
[[ "$configuration" == Debug || "$configuration" == Release ]] || {
    printf 'Linux distro tests support Debug or Release.\n' >&2
    exit 2
}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf '%s\n' '--jobs must be a positive integer.' >&2; exit 2; }
[[ "$(uname -s)" == Linux ]] || { printf 'The Linux distro matrix must run from Linux or WSL2.\n' >&2; exit 1; }
command -v podman >/dev/null 2>&1 || { printf 'Podman is required for the local Linux distro matrix.\n' >&2; exit 1; }
command -v git >/dev/null 2>&1 || { printf 'Git is required for the local Linux distro matrix.\n' >&2; exit 1; }
[[ -d "$ROOT/.git" ]] || {
    printf 'The distro matrix requires a standalone clone whose .git metadata is inside the repository.\n' >&2
    exit 1
}
if git -C "$ROOT" submodule status --recursive | grep -q '^-'; then
    printf 'Initialize all locked submodules before running the Linux distro matrix.\n' >&2
    exit 1
fi

all_distros=(ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 fedora arch tumbleweed rocky-9)
case "$distro" in
    all) selected=("${all_distros[@]}") ;;
    ubuntu-22.04|ubuntu-24.04|ubuntu-26.04|debian-12|fedora|arch|tumbleweed|rocky-9) selected=("$distro") ;;
    *) printf 'Unknown Linux distribution: %s\n' "$distro" >&2; exit 2 ;;
esac

image_for() {
    case "$1" in
        ubuntu-22.04) printf 'docker.io/library/ubuntu:22.04' ;;
        ubuntu-24.04) printf 'docker.io/library/ubuntu:24.04' ;;
        ubuntu-26.04) printf 'docker.io/library/ubuntu:26.04' ;;
        debian-12) printf 'docker.io/library/debian:12-slim' ;;
        fedora) printf 'registry.fedoraproject.org/fedora:latest' ;;
        arch) printf 'docker.io/archlinux:base-devel' ;;
        tumbleweed) printf 'registry.opensuse.org/opensuse/tumbleweed:latest' ;;
        rocky-9) printf 'docker.io/rockylinux/rockylinux:9' ;;
    esac
}

cleanup() {
    if [[ -n "$current_container" ]]; then
        podman rm --force "$current_container" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

for current in "${selected[@]}"; do
    image="$(image_for "$current")"
    cache_prefix="keire-linux-matrix-${current//./-}"
    current_container="${cache_prefix}-$$"
    printf '==> Linux distro matrix: %s (%s, %s, %s jobs)\n' "$current" "$suite" "$configuration" "$jobs"
    if [[ "$pull" == always ]] || ! podman image exists "$image"; then
        podman pull --quiet "$image" >/dev/null
    fi
    project_arguments=(--generator ninja --toolset gcc)
    if [[ "$suite" == test ]]; then
        project_arguments+=(--configuration "$configuration")
    fi
    matrix_arguments=("${project_arguments[@]}")
    if [[ "$suite" == test ]]; then
        matrix_arguments+=(--ci)
    fi
    container_command=(bash Scripts/project.sh "$suite" "${matrix_arguments[@]}")
    if [[ "$suite" == bootstrap ]]; then
        container_command=(bash Scripts/setup-linux.sh --generator ninja --toolset gcc)
    fi
    podman run --rm --name "$current_container" \
        --env "KEIRE_BUILD_JOBS=$jobs" \
        --env DOTNET_CLI_TELEMETRY_OPTOUT=1 \
        --env GIT_CONFIG_COUNT=1 \
        --env GIT_CONFIG_KEY_0=safe.directory \
        --env GIT_CONFIG_VALUE_0=/work \
        --volume "$ROOT:/work:O" \
        --volume "$cache_prefix-build:/work/Build" \
        --volume "$cache_prefix-tools:/work/Tools/Linux" \
        --volume "$cache_prefix-home-cache:/root/.cache" \
        --workdir /work "$image" \
        "${container_command[@]}"
    if [[ "$suite" == test ]]; then
        current_container="${cache_prefix}-$$-smoke"
        podman run --rm --name "$current_container" \
            --env "KEIRE_BUILD_JOBS=$jobs" \
            --env DOTNET_CLI_TELEMETRY_OPTOUT=1 \
            --env GIT_CONFIG_COUNT=1 \
            --env GIT_CONFIG_KEY_0=safe.directory \
            --env GIT_CONFIG_VALUE_0=/work \
            --volume "$ROOT:/work:O" \
            --volume "$cache_prefix-build:/work/Build" \
            --volume "$cache_prefix-tools:/work/Tools/Linux" \
            --volume "$cache_prefix-home-cache:/root/.cache" \
            --workdir /work "$image" \
            bash Scripts/project.sh run "${project_arguments[@]}" --ci --smoke-window
    fi
    current_container=""
    printf '==> Linux distro matrix passed: %s\n' "$current"
done

printf '==> Linux distro matrix passed for %s\n' "${selected[*]}"
