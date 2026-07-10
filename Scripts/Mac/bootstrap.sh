#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS_DIR="$ROOT/Tools/Mac"
PREMAKE_VERSION="5.0.0-beta8"
PREMAKE="$TOOLS_DIR/premake5"
INSTALL_OPTIONAL=0
FORCE=0
GENERATORS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generator)
            GENERATORS+=("$2")
            shift 2
            ;;
        --install-optional)
            INSTALL_OPTIONAL=1
            shift
            ;;
        --force)
            FORCE=1
            shift
            ;;
        *)
            GENERATORS+=("$1")
            shift
            ;;
    esac
done

step() {
    printf '==> %s\n' "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

ensure_brew() {
    if have brew; then
        step "Homebrew already available"
        return
    fi

    step "Installing Homebrew"
    NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    if [[ -x /opt/homebrew/bin/brew ]]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [[ -x /usr/local/bin/brew ]]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
}

brew_install_if_missing() {
    local command_name="$1"
    local package_name="$2"

    if have "$command_name"; then
        step "$command_name already available"
        return
    fi

    ensure_brew
    step "Installing $package_name"
    brew install "$package_name"
}

premake_asset_url() {
    local machine_arch
    machine_arch="$(uname -m)"

    case "$machine_arch" in
        x86_64|amd64)
            printf 'https://github.com/premake/premake-core/releases/download/v%s/premake-%s-macosx-x64.tar.gz\n' \
                "$PREMAKE_VERSION" "$PREMAKE_VERSION"
            ;;
        arm64|aarch64)
            printf 'https://github.com/premake/premake-core/releases/download/v%s/premake-%s-macosx.tar.gz\n' \
                "$PREMAKE_VERSION" "$PREMAKE_VERSION"
            ;;
        *)
            printf "Unsupported macOS architecture '%s'.\n" "$machine_arch" >&2
            return 1
            ;;
    esac
}

install_premake() {
    mkdir -p "$TOOLS_DIR"

    if [[ "$FORCE" -eq 0 && -x "$PREMAKE" ]] && "$PREMAKE" --version 2>/dev/null | grep -q "$PREMAKE_VERSION"; then
        step "Premake $PREMAKE_VERSION already installed at $PREMAKE"
        return
    fi

    step "Downloading Premake $PREMAKE_VERSION"
    local temp_dir
    temp_dir="$(mktemp -d)"

    local archive="$temp_dir/premake-download"
    curl -fsSL "$(premake_asset_url)" -o "$archive"
    tar -xf "$archive" -C "$temp_dir" 2>/dev/null || unzip -q "$archive" -d "$temp_dir"

    local downloaded
    downloaded="$(find "$temp_dir" -type f -name premake5 | head -n 1)"
    if [[ -z "$downloaded" ]]; then
        printf 'Downloaded Premake archive did not contain premake5.\n' >&2
        exit 1
    fi

    cp "$downloaded" "$PREMAKE"
    chmod +x "$PREMAKE"
    if ! "$PREMAKE" --version 2>/dev/null | grep -q "$PREMAKE_VERSION"; then
        rm -f "$PREMAKE"
        printf "Downloaded Premake cannot run on macOS architecture '%s'.\n" "$(uname -m)" >&2
        exit 1
    fi
    rm -rf "$temp_dir"
    step "Installed Premake at $PREMAKE"
}

ensure_xcode_tools() {
    if xcode-select -p >/dev/null 2>&1; then
        step "Xcode command line tools already available"
        return
    fi

    step "Installing Xcode command line tools"
    xcode-select --install
}

ensure_generator() {
    case "$1" in
        xcode4)
            ensure_xcode_tools
            ;;
        ninja)
            ensure_xcode_tools
            brew_install_if_missing ninja ninja
            ;;
        gmake)
            ensure_xcode_tools
            brew_install_if_missing make make
            ;;
        compilecommands)
            ensure_generator ninja
            ;;
        *)
            printf "Unsupported macOS generator '%s'.\n" "$1" >&2
            exit 1
            ;;
    esac
}

install_premake
brew_install_if_missing git git

if [[ "$INSTALL_OPTIONAL" -eq 1 ]]; then
    ensure_brew
    brew install git ninja llvm make
fi

for generator in "${GENERATORS[@]}"; do
    ensure_generator "$generator"
done

bash "$(dirname "${BASH_SOURCE[0]}")/vendor.sh"

step "macOS prerequisites are ready"
