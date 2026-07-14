#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
PREMAKE_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_VERSION)"
PREMAKE_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_X86_64_SHA256)"
PREMAKE_ARM_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_ARM64_SOURCE_SHA256)"
PREMAKE_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_X86_64_URL)"
PREMAKE_ARM_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_ARM64_SOURCE_URL)"
PREMAKE="$ROOT/Tools/Linux/premake5"
PACKAGE_MANAGER=""; PACKAGE_INDEX_UPDATED=0
TEMPORARY=""

cleanup_temporary() { [[ -z "$TEMPORARY" ]] || rm -rf "$TEMPORARY"; }
trap cleanup_temporary EXIT

step() { printf '==> %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
sudo_cmd() { if [[ ${EUID:-$(id -u)} -eq 0 ]]; then "$@"; else sudo "$@"; fi; }
detect_package_manager() {
    [[ -n "$PACKAGE_MANAGER" ]] && return
    for candidate in apt-get dnf pacman zypper; do if have "$candidate"; then PACKAGE_MANAGER="$candidate"; return; fi; done
    printf 'No supported package manager found.\n' >&2; exit 1
}
install_packages() {
    detect_package_manager
    local flags=()
    mapfile -t flags < <(package_install_arguments "$PACKAGE_MANAGER")
    case "$PACKAGE_MANAGER" in
        apt-get) [[ $PACKAGE_INDEX_UPDATED -eq 1 ]] || { sudo_cmd apt-get update; PACKAGE_INDEX_UPDATED=1; }; sudo_cmd apt-get install "${flags[@]}" "$@" ;;
        dnf|pacman|zypper) sudo_cmd "$PACKAGE_MANAGER" "${flags[@]}" "$@" ;;
    esac
}
install_logical_packages() {
    local logical mapped
    local packages=()
    detect_package_manager
    for logical in "$@"; do
        mapped="$(package_name "$PACKAGE_MANAGER" "$logical")"
        read -r -a mapped_parts <<< "$mapped"
        packages+=("${mapped_parts[@]}")
    done
    install_packages "${packages[@]}"
}
ensure_command() {
    local command="$1"; shift
    if ! have "$command" || [[ $UPDATE -eq 1 ]]; then
        step "Installing or updating $command"
        install_logical_packages "$@"
    else
        step "$command already available"
    fi
}
check_version() {
    local name="$1" actual="$2" minimum="$3"
    version_at_least "$actual" "$minimum" || { printf '%s %s is older than required %s; rerun with --update.\n' "$name" "$actual" "$minimum" >&2; exit 1; }
}

install_premake() {
    mkdir -p "$(dirname "$PREMAKE")"
    if [[ $FORCE -eq 0 && -x "$PREMAKE" ]] && "$PREMAKE" --version 2>/dev/null | grep -q "$PREMAKE_VERSION"; then step "Premake $PREMAKE_VERSION already installed"; return; fi
    ensure_command curl curl
    ensure_command tar tar
    local archive expected url
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/premake.tar.gz"
    if [[ "$ARCHITECTURE" == x86_64 ]]; then
        expected="$PREMAKE_X64_HASH"
        url="$PREMAKE_X64_URL"
        curl -fsSL "$url" -o "$archive"
        [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$expected" ]] || { printf 'Premake checksum mismatch.\n' >&2; exit 1; }
        tar -xf "$archive" -C "$TEMPORARY"
    else
        expected="$PREMAKE_ARM_HASH"
        url="$PREMAKE_ARM_URL"
        install_logical_packages build uuid
        curl -fsSL "$url" -o "$archive"
        [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$expected" ]] || { printf 'Premake source checksum mismatch.\n' >&2; exit 1; }
        tar -xf "$archive" -C "$TEMPORARY"
        local source
        source="$(find "$TEMPORARY" -maxdepth 1 -type d -name 'premake-core-*' -print -quit)"
        make -C "$source" -f Bootstrap.mak linux PLATFORM=ARM64
    fi
    local downloaded
    downloaded="$(find_premake_binary "$TEMPORARY")"
    [[ -n "$downloaded" ]] || { printf 'Premake executable was not produced.\n' >&2; exit 1; }
    cp "$downloaded" "$PREMAKE"; chmod +x "$PREMAKE"
    "$PREMAKE" --version | grep -q "$PREMAKE_VERSION"
    rm -rf "$TEMPORARY"; TEMPORARY=""
}

install_premake
ensure_command git git
check_version Git "$(git --version | extract_version)" 2.40
ensure_command cmake cmake
check_version CMake "$(cmake --version | extract_version)" 3.20

ensure_command ninja ninja
check_version Ninja "$(ninja --version)" 1.11
ensure_command pkg-config pkg-config
install_logical_packages sdl-video
if ! pkg-config --exists x11 && ! pkg-config --exists wayland-client; then
    printf 'SDL video requires at least one available X11 or Wayland development backend.\n' >&2
    exit 1
fi
[[ "$GENERATOR" == compilecommands ]] && ensure_command python3 python
if [[ "$GENERATOR" == gmake ]]; then
    install_logical_packages build
    check_version Make "$(make --version | extract_version)" 4.3
fi
case "$TOOLSET" in
    default|gcc)
        ensure_command g++ cxx
        check_version GCC "$(g++ -dumpfullversion -dumpversion)" 12
        ;;
    clang)
        ensure_command clang++ clang
        check_version Clang "$(clang++ --version | extract_version)" 16
        if [[ $UPDATE -eq 1 ]] || ! resolve_llvm_tool llvm-profdata >/dev/null 2>&1 || ! resolve_llvm_tool llvm-cov >/dev/null 2>&1; then
            install_logical_packages llvm
        fi
        profdata_path="$(resolve_llvm_tool llvm-profdata)" || exit 1
        cov_path="$(resolve_llvm_tool llvm-cov)" || exit 1
        step "LLVM coverage tools match Clang: $profdata_path, $cov_path"
        ;;
    *) printf "Unsupported Linux toolset '%s'.\n" "$TOOLSET" >&2; exit 1 ;;
esac
ensure_command objcopy binutils

if [[ $INSTALL_OPTIONAL -eq 1 ]]; then install_logical_packages git curl tar ninja clang make python; fi
bash "$ROOT/Scripts/Linux/vendor.sh"
step "Linux prerequisites are ready for $ARCHITECTURE"
