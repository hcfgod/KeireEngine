#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=Client; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
validate_unix_combination Mac "$GENERATOR" "$TOOLSET"
PREMAKE_VERSION=5.0.0-beta8
PREMAKE="$ROOT/Tools/Mac/premake5"
HOMEBREW_COMMIT=c7952e40b7957268f61643152f4db725379b292e
HOMEBREW_HASH=99287f194a8b3c9e6b0203a11a5fa54518be57209343e6bb954dec4635796d9d

step() { printf '==> %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
load_brew() {
    if [[ -x /opt/homebrew/bin/brew ]]; then eval "$(/opt/homebrew/bin/brew shellenv)";
    elif [[ -x /usr/local/bin/brew ]]; then eval "$(/usr/local/bin/brew shellenv)"; fi
}
ensure_brew() {
    load_brew; have brew && return
    local temporary script
    temporary="$(mktemp -d)"; script="$temporary/install.sh"
    trap 'rm -rf "$temporary"' RETURN
    curl -fsSL "https://raw.githubusercontent.com/Homebrew/install/$HOMEBREW_COMMIT/install.sh" -o "$script"
    [[ "$(shasum -a 256 "$script" | awk '{print $1}')" == "$HOMEBREW_HASH" ]] || { printf 'Homebrew installer checksum mismatch.\n' >&2; exit 1; }
    NONINTERACTIVE=1 /bin/bash "$script"
    rm -rf "$temporary"; trap - RETURN; load_brew
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
    local temporary archive hash suffix
    temporary="$(mktemp -d)"; archive="$temporary/premake.tar.gz"; trap 'rm -rf "$temporary"' RETURN
    if [[ "$ARCHITECTURE" == x86_64 ]]; then suffix=macosx-x64; hash=84b5fa5a432dcebdc3dd12e8677d10e38e5b32a3fe06d83ae68967e4f5e2db8a;
    else suffix=macosx; hash=fa73a46f093fa6f17494a3d063421aa6cae3ea825a61c62dd59fc2f07a256d03; fi
    curl -fsSL "https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-$suffix.tar.gz" -o "$archive"
    [[ "$(shasum -a 256 "$archive" | awk '{print $1}')" == "$hash" ]] || { printf 'Premake checksum mismatch.\n' >&2; exit 1; }
    tar -xf "$archive" -C "$temporary"
    cp "$(find "$temporary" -type f -name premake5 | head -n 1)" "$PREMAKE"; chmod +x "$PREMAKE"
    "$PREMAKE" --version | grep -q "$PREMAKE_VERSION"
    rm -rf "$temporary"; trap - RETURN
}

install_premake
if ! xcode-select -p >/dev/null 2>&1; then xcode-select --install; printf 'Complete the Xcode tools installation, then rerun bootstrap.\n' >&2; exit 1; fi
check_version Xcode "$(xcodebuild -version | head -n 1 | grep -Eo '[0-9]+(\.[0-9]+)?')" 15

if ! have git; then brew_install git git; fi
check_version Git "$(git --version | grep -Eo '[0-9]+(\.[0-9]+)+' | head -n 1)" 2.40
[[ "$GENERATOR" == ninja || "$GENERATOR" == compilecommands ]] && { brew_install ninja ninja; check_version Ninja "$(ninja --version)" 1.11; }
[[ "$GENERATOR" == compilecommands ]] && brew_install python3 python
[[ "$GENERATOR" == gmake ]] && { brew_install gmake make; check_version Make "$(gmake --version | head -n 1 | grep -Eo '[0-9]+(\.[0-9]+)+')" 4.3; }
case "$TOOLSET" in default|clang) check_version Clang "$(clang++ --version | head -n 1 | grep -Eo '[0-9]+(\.[0-9]+)+' | head -n 1)" 16;; *) printf 'macOS supports default or clang toolsets.\n' >&2; exit 1;; esac
if [[ $UPDATE -eq 1 ]]; then ensure_brew; brew update; fi
if [[ $INSTALL_OPTIONAL -eq 1 ]]; then ensure_brew; brew install git ninja llvm make; fi
bash "$ROOT/Scripts/Mac/vendor.sh"
step "macOS prerequisites are ready for $ARCHITECTURE"
