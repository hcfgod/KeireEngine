#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS_DIR="$ROOT/Tools/Linux"
PREMAKE_VERSION="5.0.0-beta8"
PREMAKE="$TOOLS_DIR/premake5"
INSTALL_OPTIONAL=0
FORCE=0
GENERATORS=()
PACKAGE_MANAGER=""
UPDATED=0

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

sudo_cmd() {
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

detect_package_manager() {
    if [[ -n "$PACKAGE_MANAGER" ]]; then
        return
    fi

    if have apt-get; then
        PACKAGE_MANAGER="apt"
    elif have dnf; then
        PACKAGE_MANAGER="dnf"
    elif have pacman; then
        PACKAGE_MANAGER="pacman"
    elif have zypper; then
        PACKAGE_MANAGER="zypper"
    else
        printf 'No supported package manager found. Expected apt, dnf, pacman, or zypper.\n' >&2
        exit 1
    fi
}

install_packages() {
    detect_package_manager

    case "$PACKAGE_MANAGER" in
        apt)
            if [[ "$UPDATED" -eq 0 ]]; then
                sudo_cmd apt-get update
                UPDATED=1
            fi
            sudo_cmd apt-get install -y "$@"
            ;;
        dnf)
            sudo_cmd dnf install -y "$@"
            ;;
        pacman)
            sudo_cmd pacman -Sy --needed --noconfirm "$@"
            ;;
        zypper)
            sudo_cmd zypper --non-interactive install "$@"
            ;;
    esac
}

install_command_if_missing() {
    local command_name="$1"
    shift

    if have "$command_name"; then
        step "$command_name already available"
        return
    fi

    step "Installing packages for $command_name"
    install_packages "$@"
}

premake_asset_url() {
    local fallback="https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-linux.tar.gz"

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
        if "linux" in name and name.endswith((".tar.gz", ".zip")):
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

    install_command_if_missing curl curl
    install_command_if_missing tar tar

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

ensure_generator() {
    detect_package_manager

    case "$1" in
        ninja)
            case "$PACKAGE_MANAGER" in
                apt) install_command_if_missing ninja ninja-build ;;
                *) install_command_if_missing ninja ninja ;;
            esac
            ;;
        gmake)
            case "$PACKAGE_MANAGER" in
                apt) install_packages build-essential ;;
                dnf) install_packages gcc-c++ make ;;
                pacman) install_packages base-devel ;;
                zypper) install_packages gcc-c++ make ;;
            esac
            ;;
        compilecommands)
            ensure_generator ninja
            ;;
        *)
            printf "Unsupported Linux generator '%s'.\n" "$1" >&2
            exit 1
            ;;
    esac
}

install_premake
install_command_if_missing git git

if [[ "$INSTALL_OPTIONAL" -eq 1 ]]; then
    detect_package_manager
    case "$PACKAGE_MANAGER" in
        apt) install_packages build-essential git curl unzip tar ninja-build clang ;;
        dnf) install_packages gcc-c++ make git curl unzip tar ninja-build clang ;;
        pacman) install_packages base-devel git curl unzip tar ninja clang ;;
        zypper) install_packages gcc-c++ make git curl unzip tar ninja clang ;;
    esac
fi

for generator in "${GENERATORS[@]}"; do
    ensure_generator "$generator"
done

bash "$(dirname "${BASH_SOURCE[0]}")/vendor.sh"

step "Linux prerequisites are ready"
