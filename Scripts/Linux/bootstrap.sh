#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
PREMAKE_VERSION=5.0.0-beta8
PREMAKE="$ROOT/Tools/Linux/premake5"
PACKAGE_MANAGER=""; PACKAGE_INDEX_UPDATED=0

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
    case "$PACKAGE_MANAGER" in
        apt-get) [[ $PACKAGE_INDEX_UPDATED -eq 1 ]] || { sudo_cmd apt-get update; PACKAGE_INDEX_UPDATED=1; }; sudo_cmd apt-get install -y "$@" ;;
        dnf) sudo_cmd dnf install -y "$@" ;;
        pacman) sudo_cmd pacman -Sy --needed --noconfirm "$@" ;;
        zypper) sudo_cmd zypper --non-interactive install "$@" ;;
    esac
}
ensure_command() {
    local command="$1"; shift
    if ! have "$command" || [[ $UPDATE -eq 1 ]]; then
        step "Installing or updating $command"
        install_packages "$@"
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
    local temporary archive expected url
    temporary="$(mktemp -d)"; trap 'rm -rf "$temporary"' RETURN
    archive="$temporary/premake.tar.gz"
    if [[ "$ARCHITECTURE" == x86_64 ]]; then
        expected=63edd3e7461eebdd45b500a3c7e8ad4e7a67d68f230010f9a97cbb71b4ec59c8
        url="https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-linux.tar.gz"
        curl -fsSL "$url" -o "$archive"
        [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$expected" ]] || { printf 'Premake checksum mismatch.\n' >&2; exit 1; }
        tar -xf "$archive" -C "$temporary"
    else
        expected=2a55195fd2b27e5aa3de8ff6d22cdb127232a86f801d06e7f673d30a0eba09ac
        url="https://github.com/premake/premake-core/archive/refs/tags/v$PREMAKE_VERSION.tar.gz"
        install_packages gcc g++ make 2>/dev/null || install_packages gcc-c++ make
        curl -fsSL "$url" -o "$archive"
        [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$expected" ]] || { printf 'Premake source checksum mismatch.\n' >&2; exit 1; }
        tar -xf "$archive" -C "$temporary"
        local source
        source="$(find "$temporary" -maxdepth 1 -type d -name 'premake-core-*' | head -n 1)"
        make -C "$source" -f Bootstrap.mak linux PLATFORM=ARM64
    fi
    local downloaded
    downloaded="$(find "$temporary" -type f -name premake5 -perm -u+x | head -n 1)"
    [[ -n "$downloaded" ]] || { printf 'Premake executable was not produced.\n' >&2; exit 1; }
    cp "$downloaded" "$PREMAKE"; chmod +x "$PREMAKE"
    "$PREMAKE" --version | grep -q "$PREMAKE_VERSION"
    rm -rf "$temporary"; trap - RETURN
}

install_premake
ensure_command git git
check_version Git "$(git --version | grep -Eo '[0-9]+(\.[0-9]+)+' | head -n 1)" 2.40

if [[ "$GENERATOR" == ninja || "$GENERATOR" == compilecommands ]]; then ensure_command ninja ninja-build; check_version Ninja "$(ninja --version)" 1.11; fi
[[ "$GENERATOR" == compilecommands ]] && ensure_command python3 python3
if [[ "$GENERATOR" == gmake ]]; then
    detect_package_manager
    case "$PACKAGE_MANAGER" in apt-get) install_packages build-essential;; dnf|zypper) install_packages gcc-c++ make;; pacman) install_packages base-devel;; esac
    check_version Make "$(make --version | head -n 1 | grep -Eo '[0-9]+(\.[0-9]+)+')" 4.3
fi
case "$TOOLSET" in
    default|gcc)
        ensure_command g++ g++
        check_version GCC "$(g++ -dumpfullversion -dumpversion)" 12
        ;;
    clang)
        ensure_command clang++ clang
        check_version Clang "$(clang++ --version | head -n 1 | grep -Eo '[0-9]+(\.[0-9]+)+' | head -n 1)" 16
        ;;
    *) printf "Unsupported Linux toolset '%s'.\n" "$TOOLSET" >&2; exit 1 ;;
esac

if [[ $INSTALL_OPTIONAL -eq 1 ]]; then install_packages git curl tar ninja-build clang make; fi
bash "$ROOT/Scripts/Linux/vendor.sh"
step "Linux prerequisites are ready for $ARCHITECTURE"
