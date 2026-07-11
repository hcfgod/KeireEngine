#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"
PREMAKE_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_VERSION)"
PREMAKE="$ROOT/Tools/Mac/premake5"
HOMEBREW_URL="$(config_value "$ROOT/Config/Dependencies.lock" HOMEBREW_INSTALL_URL)"
HOMEBREW_HASH="$(config_value "$ROOT/Config/Dependencies.lock" HOMEBREW_INSTALL_SHA256)"
PREMAKE_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_MACOS_X86_64_SHA256)"
PREMAKE_ARM_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_MACOS_ARM64_SHA256)"
PREMAKE_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_MACOS_X86_64_URL)"
PREMAKE_ARM_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_MACOS_ARM64_URL)"
TEMPORARY=""

cleanup_temporary() { [[ -z "$TEMPORARY" ]] || rm -rf "$TEMPORARY"; }
trap cleanup_temporary EXIT

step() { printf '==> %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
load_brew() {
    if [[ -x /opt/homebrew/bin/brew ]]; then eval "$(/opt/homebrew/bin/brew shellenv)";
    elif [[ -x /usr/local/bin/brew ]]; then eval "$(/usr/local/bin/brew shellenv)"; fi
}
ensure_brew() {
    load_brew; have brew && return
    local script
    TEMPORARY="$(mktemp -d)"; script="$TEMPORARY/install.sh"
    curl -fsSL "$HOMEBREW_URL" -o "$script"
    [[ "$(shasum -a 256 "$script" | awk '{print $1}')" == "$HOMEBREW_HASH" ]] || { printf 'Homebrew installer checksum mismatch.\n' >&2; exit 1; }
    NONINTERACTIVE=1 /bin/bash "$script"
    rm -rf "$TEMPORARY"; TEMPORARY=""; load_brew
}
brew_install() {
    local command="$1" package="$2"
    ensure_brew
    if ! have "$command"; then
        brew install "$package"
    elif [[ $UPDATE -eq 1 ]]; then
        if brew list --versions "$package" >/dev/null 2>&1; then brew upgrade "$package" || true; else brew install "$package"; fi
    fi
}
check_version() { version_at_least "$2" "$3" || { printf '%s %s is older than required %s; rerun with --update.\n' "$1" "$2" "$3" >&2; exit 1; }; }

install_premake() {
    mkdir -p "$(dirname "$PREMAKE")"
    if [[ $FORCE -eq 0 && -x "$PREMAKE" ]] && "$PREMAKE" --version 2>/dev/null | grep -q "$PREMAKE_VERSION"; then step "Premake $PREMAKE_VERSION already installed"; return; fi
    local archive hash url
    TEMPORARY="$(mktemp -d)"; archive="$TEMPORARY/premake.tar.gz"
    if [[ "$ARCHITECTURE" == x86_64 ]]; then hash="$PREMAKE_X64_HASH"; url="$PREMAKE_X64_URL";
    else hash="$PREMAKE_ARM_HASH"; url="$PREMAKE_ARM_URL"; fi
    curl -fsSL "$url" -o "$archive"
    [[ "$(shasum -a 256 "$archive" | awk '{print $1}')" == "$hash" ]] || { printf 'Premake checksum mismatch.\n' >&2; exit 1; }
    tar -xf "$archive" -C "$TEMPORARY"
    cp "$(find_premake_binary "$TEMPORARY")" "$PREMAKE"; chmod +x "$PREMAKE"
    "$PREMAKE" --version | grep -q "$PREMAKE_VERSION"
    rm -rf "$TEMPORARY"; TEMPORARY=""
}

install_premake
if ! xcode-select -p >/dev/null 2>&1; then xcode-select --install; printf 'Complete the Xcode tools installation, then rerun bootstrap.\n' >&2; exit 1; fi
check_version Xcode "$(xcodebuild -version | extract_version)" 15

if ! have git; then brew_install git git; fi
check_version Git "$(git --version | extract_version)" 2.40
[[ "$GENERATOR" == ninja || "$GENERATOR" == compilecommands ]] && { brew_install ninja ninja; check_version Ninja "$(ninja --version)" 1.11; }
[[ "$GENERATOR" == compilecommands ]] && brew_install python3 python
[[ "$GENERATOR" == gmake ]] && { brew_install gmake make; check_version Make "$(gmake --version | extract_version)" 4.3; }
case "$TOOLSET" in default|clang) check_version Clang "$(clang++ --version | extract_version)" 16;; *) printf 'macOS supports default or clang toolsets.\n' >&2; exit 1;; esac
if [[ $UPDATE -eq 1 ]]; then ensure_brew; brew update; fi
if [[ $INSTALL_OPTIONAL -eq 1 ]]; then ensure_brew; brew install git ninja llvm make; fi
bash "$ROOT/Scripts/Mac/vendor.sh"
step "macOS prerequisites are ready for $ARCHITECTURE"
