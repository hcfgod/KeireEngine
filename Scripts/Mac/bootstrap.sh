#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
export PATH="$ROOT/Tools/Mac:$PATH"
GENERATOR=xcode4; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
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
DOTNET_SDK_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_SDK_VERSION)"
DOTNET_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_X86_64_URL)"
DOTNET_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_X86_64_SHA512)"
DOTNET_ARM64_URL="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_ARM64_URL)"
DOTNET_ARM64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_MACOS_ARM64_SHA512)"
PYYAML_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" PYYAML_VERSION)"
PYYAML_URL="$(config_value "$ROOT/Config/Dependencies.lock" PYYAML_SOURCE_URL)"
PYYAML_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PYYAML_SOURCE_SHA256)"
TEMPORARY=""
PUBLICATION_INSTALL=""
PUBLICATION_BACKUP=""
PUBLICATION_STAGED=""
PUBLICATION_LOCK=""
PUBLICATION_TOKEN=""

cleanup_temporary() {
    if [[ -n "$PUBLICATION_BACKUP" && -d "$PUBLICATION_BACKUP" && ! -L "$PUBLICATION_BACKUP" ]]; then
        if [[ -n "$PUBLICATION_INSTALL" && ! -e "$PUBLICATION_INSTALL" && ! -L "$PUBLICATION_INSTALL" ]]; then
            mv "$PUBLICATION_BACKUP" "$PUBLICATION_INSTALL" 2>/dev/null || true
        elif [[ -n "$PUBLICATION_INSTALL" && -e "$PUBLICATION_INSTALL" ]]; then
            rm -rf -- "$PUBLICATION_BACKUP"
        fi
    fi
    [[ -z "$PUBLICATION_STAGED" || ! -d "$PUBLICATION_STAGED" || -L "$PUBLICATION_STAGED" ]] || \
        rm -rf -- "$PUBLICATION_STAGED"
    if [[ -n "$PUBLICATION_LOCK" && -n "$PUBLICATION_TOKEN" ]]; then
        atomic_symlink_lock_release "$PUBLICATION_LOCK" "$PUBLICATION_TOKEN" 2>/dev/null || true
    fi
    [[ -z "$TEMPORARY" ]] || rm -rf "$TEMPORARY"
}
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
    run_homebrew_installer "$CI" "$script"
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

publish_cached_directory() {
    local candidate="${1:?publication candidate is required}"
    local install_root="${2:?publication destination is required}"
    local parent name publication_lock publication_token
    parent="$(dirname "$install_root")"
    name="$(basename "$install_root")"
    [[ -d "$candidate" && ! -L "$candidate" && -d "$parent" && ! -L "$parent" && ! -L "$install_root" ]] || {
        printf 'Refusing an unsafe cached-directory publication: %s\n' "$install_root" >&2
        return 1
    }
    publication_lock="$parent/.$name.publish.lock"
    publication_token="$(atomic_symlink_lock_acquire "$publication_lock")" || {
        printf 'Another cached-directory publication is already running: %s\n' "$install_root" >&2
        return 1
    }
    PUBLICATION_LOCK="$publication_lock"
    PUBLICATION_TOKEN="$publication_token"
    PUBLICATION_INSTALL="$install_root"
    PUBLICATION_STAGED="$parent/.$name.candidate.$$.$RANDOM"
    PUBLICATION_BACKUP="$parent/.$name.backup.$$.$RANDOM"
    [[ ! -e "$PUBLICATION_STAGED" && ! -L "$PUBLICATION_STAGED" &&
       ! -e "$PUBLICATION_BACKUP" && ! -L "$PUBLICATION_BACKUP" ]] || return 1
    mv "$candidate" "$PUBLICATION_STAGED" || return 1
    if [[ -e "$install_root" ]]; then
        [[ -d "$install_root" && ! -L "$install_root" ]] || return 1
        mv "$install_root" "$PUBLICATION_BACKUP" || return 1
    else
        PUBLICATION_BACKUP=""
    fi
    if ! mv "$PUBLICATION_STAGED" "$install_root"; then
        if [[ -n "$PUBLICATION_BACKUP" && ! -e "$install_root" && ! -L "$install_root" ]]; then
            mv "$PUBLICATION_BACKUP" "$install_root" 2>/dev/null || true
        fi
        return 1
    fi
    PUBLICATION_STAGED=""
    if [[ -n "$PUBLICATION_BACKUP" ]]; then
        rm -rf -- "$PUBLICATION_BACKUP"
        PUBLICATION_BACKUP=""
    fi
    PUBLICATION_INSTALL=""
    atomic_symlink_lock_release "$PUBLICATION_LOCK" "$PUBLICATION_TOKEN"
    PUBLICATION_LOCK=""
    PUBLICATION_TOKEN=""
}

install_bison() {
    ensure_brew
    if ! brew list --versions bison >/dev/null 2>&1; then
        brew install bison
    elif [[ $UPDATE -eq 1 ]]; then
        brew upgrade bison || true
    fi
    local bison_executable
    bison_executable="$(brew --prefix bison)/bin/bison"
    check_version Bison "$("$bison_executable" --version | extract_version)" 3.0
}

install_premake() {
    local version_output=""
    mkdir -p "$(dirname "$PREMAKE")"
    if [[ $FORCE -eq 0 && -x "$PREMAKE" ]] &&
       version_output="$(tool_version_from_temporary_directory "$PREMAKE" --version 2>/dev/null)" &&
       [[ "$version_output" == *"$PREMAKE_VERSION"* ]]; then
        step "Premake $PREMAKE_VERSION already installed"
        return
    fi
    local archive hash url
    TEMPORARY="$(mktemp -d)"; archive="$TEMPORARY/premake.tar.gz"
    if [[ "$ARCHITECTURE" == x86_64 ]]; then hash="$PREMAKE_X64_HASH"; url="$PREMAKE_X64_URL";
    else hash="$PREMAKE_ARM_HASH"; url="$PREMAKE_ARM_URL"; fi
    curl -fsSL "$url" -o "$archive"
    [[ "$(shasum -a 256 "$archive" | awk '{print $1}')" == "$hash" ]] || { printf 'Premake checksum mismatch.\n' >&2; exit 1; }
    tar -xf "$archive" -C "$TEMPORARY"
    cp "$(find_premake_binary "$TEMPORARY")" "$PREMAKE"; chmod +x "$PREMAKE"
    version_output="$(tool_version_from_temporary_directory "$PREMAKE" --version)" || {
        printf 'Downloaded Premake could not report its version.\n' >&2
        exit 1
    }
    [[ "$version_output" == *"$PREMAKE_VERSION"* ]] || {
        printf 'Downloaded Premake did not report the pinned version %s.\n' "$PREMAKE_VERSION" >&2
        exit 1
    }
    rm -rf "$TEMPORARY"; TEMPORARY=""
}

install_dotnet_sdk() {
    local cache_root resolved_cache install_root dotnet_link url hash archive candidate listing
    [[ "$DOTNET_SDK_VERSION" =~ ^10\.[0-9]+\.[0-9]+$ ]] || {
        printf 'The pinned .NET SDK version is invalid: %s.\n' "$DOTNET_SDK_VERSION" >&2
        exit 1
    }
    [[ "$DOTNET_X64_HASH" =~ ^[0-9a-f]{128}$ && "$DOTNET_ARM64_HASH" =~ ^[0-9a-f]{128}$ ]] || {
        printf 'A pinned macOS .NET SDK SHA-512 digest is invalid.\n' >&2
        exit 1
    }

    cache_root="${XDG_CACHE_HOME:-$HOME/Library/Caches}/keire/toolchains"
    mkdir -p "$cache_root" "$ROOT/Tools/Mac"
    [[ ! -L "$cache_root" ]] || {
        printf 'Refusing to use a symbolic .NET toolchain cache: %s.\n' "$cache_root" >&2
        exit 1
    }
    resolved_cache="$(cd -P "$cache_root" && pwd -P)"
    install_root="$resolved_cache/dotnet-sdk-$DOTNET_SDK_VERSION-macos-$(architecture_output_name "$ARCHITECTURE")"
    dotnet_link="$ROOT/Tools/Mac/dotnet"
    case "$install_root" in "$resolved_cache"/dotnet-sdk-*) ;; *) exit 1 ;; esac
    [[ ! -L "$install_root" ]] || {
        printf 'Refusing to use a symbolic .NET SDK installation: %s.\n' "$install_root" >&2
        exit 1
    }
    if [[ $FORCE -eq 0 && -x "$install_root/dotnet" ]]; then
        listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 "$install_root/dotnet" --list-sdks)" || listing=""
        if dotnet_sdk_listing_matches_installation "$listing" "$DOTNET_SDK_VERSION" "$install_root"; then
            [[ ! -e "$dotnet_link" || -L "$dotnet_link" ]] || {
                printf 'Refusing to replace a non-symbolic .NET launcher: %s.\n' "$dotnet_link" >&2
                exit 1
            }
            ln -sfn "$install_root/dotnet" "$dotnet_link"
            step ".NET SDK $DOTNET_SDK_VERSION already installed"
            return
        fi
    fi

    if [[ "$ARCHITECTURE" == ARM64 ]]; then
        url="$DOTNET_ARM64_URL"; hash="$DOTNET_ARM64_HASH"
    else
        url="$DOTNET_X64_URL"; hash="$DOTNET_X64_HASH"
    fi
    step "Installing pinned .NET SDK $DOTNET_SDK_VERSION"
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/dotnet-sdk.tar.gz"
    candidate="$TEMPORARY/dotnet-sdk"
    mkdir -p "$candidate"
    curl -fsSL "$url" -o "$archive"
    [[ "$(shasum -a 512 "$archive" | awk '{print $1}')" == "$hash" ]] || {
        printf '.NET SDK checksum mismatch.\n' >&2
        exit 1
    }
    tar -xzf "$archive" -C "$candidate"
    listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 "$candidate/dotnet" --list-sdks)" || {
        printf 'Downloaded .NET SDK could not start on this macOS host.\n' >&2
        exit 1
    }
    dotnet_sdk_listing_matches_installation "$listing" "$DOTNET_SDK_VERSION" "$candidate" || {
        printf 'Downloaded .NET SDK did not report the pinned version %s from its installation path.\n' \
            "$DOTNET_SDK_VERSION" >&2
        exit 1
    }
    publish_cached_directory "$candidate" "$install_root"
    [[ ! -e "$dotnet_link" || -L "$dotnet_link" ]] || {
        printf 'Refusing to replace a non-symbolic .NET launcher: %s.\n' "$dotnet_link" >&2
        exit 1
    }
    ln -sfn "$install_root/dotnet" "$dotnet_link"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

install_pyyaml() {
    local cache_root resolved_cache install_root python_packages_link archive source candidate
    [[ "$PYYAML_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && "$PYYAML_HASH" =~ ^[0-9a-f]{64}$ ]] || {
        printf 'The pinned PyYAML dependency is invalid.\n' >&2
        exit 1
    }
    cache_root="${XDG_CACHE_HOME:-$HOME/Library/Caches}/keire/python"
    mkdir -p "$cache_root" "$ROOT/Tools/Mac"
    [[ ! -L "$cache_root" ]] || {
        printf 'Refusing to use a symbolic Python dependency cache: %s.\n' "$cache_root" >&2
        exit 1
    }
    resolved_cache="$(cd -P "$cache_root" && pwd -P)"
    install_root="$resolved_cache/pyyaml-$PYYAML_VERSION"
    python_packages_link="$ROOT/Tools/Mac/python-packages"
    [[ ! -L "$install_root" ]] || {
        printf 'Refusing to use a symbolic PyYAML installation: %s.\n' "$install_root" >&2
        exit 1
    }
    if [[ $FORCE -eq 0 && -f "$install_root/yaml/__init__.py" ]] &&
       PYTHONPATH="$install_root${PYTHONPATH:+:$PYTHONPATH}" python3 -c \
         'import sys, yaml; sys.exit(0 if yaml.__version__ == sys.argv[1] else 1)' "$PYYAML_VERSION"; then
        [[ ! -e "$python_packages_link" || -L "$python_packages_link" ]] || {
            printf 'Refusing to replace a non-symbolic Python dependency path: %s.\n' "$python_packages_link" >&2
            exit 1
        }
        ln -sfn "$install_root" "$python_packages_link"
        step "PyYAML $PYYAML_VERSION already installed"
        return
    fi

    step "Installing checksum-pinned PyYAML $PYYAML_VERSION"
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/pyyaml.tar.gz"
    candidate="$TEMPORARY/python-packages"
    curl -fsSL "$PYYAML_URL" -o "$archive"
    [[ "$(shasum -a 256 "$archive" | awk '{print $1}')" == "$PYYAML_HASH" ]] || {
        printf 'PyYAML source checksum mismatch.\n' >&2
        exit 1
    }
    tar -xzf "$archive" -C "$TEMPORARY"
    source="$(find "$TEMPORARY" -mindepth 1 -maxdepth 1 -type d -iname "pyyaml-$PYYAML_VERSION" -print -quit)"
    [[ -n "$source" && -d "$source/lib/yaml" ]] || {
        printf 'PyYAML source package did not contain its Python module.\n' >&2
        exit 1
    }
    mkdir -p "$candidate"
    cp -R "$source/lib/yaml" "$candidate/yaml"
    PYTHONPATH="$candidate${PYTHONPATH:+:$PYTHONPATH}" python3 -c \
      'import sys, yaml; sys.exit(0 if yaml.__version__ == sys.argv[1] else 1)' "$PYYAML_VERSION" || {
        printf 'Installed PyYAML could not be imported with the expected version.\n' >&2
        exit 1
    }
    publish_cached_directory "$candidate" "$install_root"
    [[ ! -e "$python_packages_link" || -L "$python_packages_link" ]] || {
        printf 'Refusing to replace a non-symbolic Python dependency path: %s.\n' "$python_packages_link" >&2
        exit 1
    }
    ln -sfn "$install_root" "$python_packages_link"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

if ! developer_path="$(xcode-select -p 2>/dev/null)"; then
    xcode-select --install
    printf 'Command Line Tools are missing. Complete their installation, then rerun bootstrap.\n' >&2
    exit 1
fi
if mac_requires_full_xcode "$GENERATOR"; then
    command -v xcodebuild >/dev/null 2>&1 || { printf 'The xcode4 generator requires a full Xcode installation.\n' >&2; exit 1; }
    xcode_version="$(xcodebuild -version 2>/dev/null | extract_version)" || { printf 'The xcode4 generator requires full Xcode to be selected with xcode-select.\n' >&2; exit 1; }
    check_version Xcode "$xcode_version" 15
else
    clt_version="$(pkgutil --pkg-info=com.apple.pkg.CLTools_Executables 2>/dev/null | awk '$1 == "version:" { print $2; exit }')"
    if [[ -n "$clt_version" ]]; then
        check_version 'Xcode Command Line Tools' "$clt_version" 15
    elif [[ "$developer_path" == *Xcode.app* ]] && xcode_version="$(xcodebuild -version 2>/dev/null | extract_version)"; then
        check_version Xcode "$xcode_version" 15
    else
        printf 'A valid Xcode Command Line Tools receipt or full Xcode installation was not found.\n' >&2
        exit 1
    fi
fi
case "$TOOLSET" in
    default|clang)
        check_version Clang "$(clang++ --version | extract_version)" 16
        probe_cxx20_thread_library clang++
        ;;
    *)
        printf 'macOS supports default or clang toolsets.\n' >&2
        exit 1
        ;;
esac

install_premake
if ! have git; then brew_install git git; fi
check_version Git "$(git --version | extract_version)" 2.34
brew_install cmake cmake
check_version CMake "$(cmake --version | extract_version)" 3.24
brew_install ninja ninja
check_version Ninja "$(ninja --version)" 1.11
brew_install nasm nasm
check_version NASM "$(nasm -v | extract_version)" 2.14
brew_install pkg-config pkgconf
check_version pkg-config "$(pkg-config --version)" 0.29.2
brew_install rg ripgrep
check_version ripgrep "$(rg --version | extract_version)" 13.0
brew_install python3 python
check_version Python "$(python3 --version | extract_version)" 3.9
install_bison
install_dotnet_sdk
install_pyyaml
[[ "$GENERATOR" == gmake ]] && { brew_install gmake make; check_version Make "$(gmake --version | extract_version)" 4.3; }
if [[ $UPDATE -eq 1 ]]; then ensure_brew; brew update; fi
if [[ $INSTALL_OPTIONAL -eq 1 ]]; then ensure_brew; brew install git ninja llvm make; fi
bash "$ROOT/Scripts/Mac/vendor.sh"
step "macOS prerequisites are ready for $ARCHITECTURE"
