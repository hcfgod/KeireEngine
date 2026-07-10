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
    local fallback="https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-macosx.tar.gz"

    if have python3; then
        python3 - "$PREMAKE_VERSION" "$fallback" <<'PY'
import json
import sys
import urllib.request

version = sys.argv[1]
fallback = sys.argv[2]
url = f"https://api.github.com/repos/premake/premake-core/releases/tags/v{version}"
try:
    request = urllib.request.Request(url, headers={"User-Agent": "cross-platform-core-client-template-bootstrap"})
    with urllib.request.urlopen(request, timeout=20) as response:
        data = json.load(response)
    for asset in data.get("assets", []):
        name = asset.get("name", "").lower()
        if ("macos" in name or "macosx" in name or "osx" in name) and name.endswith((".tar.gz", ".zip")):
            print(asset["browser_download_url"])
            break
    else:
        print(fallback)
except Exception:
    print(fallback)
PY
    else
        printf '%s\n' "$fallback"
    fi
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
