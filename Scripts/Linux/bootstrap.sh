#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
export PATH="$ROOT/Tools/Linux:$PATH"
GENERATOR=ninja; CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"
PREMAKE_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_VERSION)"
PREMAKE_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_X86_64_SHA256)"
PREMAKE_SOURCE_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_SOURCE_SHA256)"
PREMAKE_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_X86_64_URL)"
PREMAKE_SOURCE_URL="$(config_value "$ROOT/Config/Dependencies.lock" PREMAKE_LINUX_SOURCE_URL)"
CMAKE_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_VERSION)"
CMAKE_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_LINUX_X86_64_URL)"
CMAKE_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_LINUX_X86_64_SHA256)"
CMAKE_ARM64_URL="$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_LINUX_ARM64_URL)"
CMAKE_ARM64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" CMAKE_LINUX_ARM64_SHA256)"
NINJA_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" NINJA_VERSION)"
NINJA_SOURCE_URL="$(config_value "$ROOT/Config/Dependencies.lock" NINJA_SOURCE_URL)"
NINJA_SOURCE_HASH="$(config_value "$ROOT/Config/Dependencies.lock" NINJA_SOURCE_SHA256)"
NASM_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" NASM_VERSION)"
NASM_SOURCE_URL="$(config_value "$ROOT/Config/Dependencies.lock" NASM_SOURCE_URL)"
NASM_SOURCE_HASH="$(config_value "$ROOT/Config/Dependencies.lock" NASM_SOURCE_SHA256)"
PATCHELF_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" PATCHELF_VERSION)"
PATCHELF_SOURCE_URL="$(config_value "$ROOT/Config/Dependencies.lock" PATCHELF_SOURCE_URL)"
PATCHELF_SOURCE_HASH="$(config_value "$ROOT/Config/Dependencies.lock" PATCHELF_SOURCE_SHA256)"
DOTNET_SDK_VERSION="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_SDK_VERSION)"
DOTNET_X64_URL="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_LINUX_X86_64_URL)"
DOTNET_X64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_LINUX_X86_64_SHA512)"
DOTNET_ARM64_URL="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_LINUX_ARM64_URL)"
DOTNET_ARM64_HASH="$(config_value "$ROOT/Config/Dependencies.lock" DOTNET_LINUX_ARM64_SHA512)"
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
        apt-get)
            [[ $PACKAGE_INDEX_UPDATED -eq 1 ]] || {
                sudo_cmd env DEBIAN_FRONTEND=noninteractive apt-get update
                PACKAGE_INDEX_UPDATED=1
            }
            sudo_cmd env DEBIAN_FRONTEND=noninteractive apt-get install "${flags[@]}" "$@"
            ;;
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
    local version_output=""
    mkdir -p "$(dirname "$PREMAKE")"
    if [[ $FORCE -eq 0 && -x "$PREMAKE" ]] &&
       version_output="$(tool_version_from_temporary_directory "$PREMAKE" --version 2>/dev/null)" &&
       [[ "$version_output" == *"$PREMAKE_VERSION"* ]]; then
        step "Premake $PREMAKE_VERSION already installed"
        return
    fi
    ensure_command curl curl
    ensure_command tar tar
    local archive downloaded="" source source_archive
    TEMPORARY="$(mktemp -d)"
    if [[ "$ARCHITECTURE" == x86_64 ]]; then
        archive="$TEMPORARY/premake-prebuilt.tar.gz"
        mkdir -p "$TEMPORARY/prebuilt"
        curl -fsSL "$PREMAKE_X64_URL" -o "$archive"
        [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$PREMAKE_X64_HASH" ]] || {
            printf 'Premake checksum mismatch.\n' >&2
            exit 1
        }
        tar -xf "$archive" -C "$TEMPORARY/prebuilt"
        downloaded="$(find_premake_binary "$TEMPORARY/prebuilt")"
        if [[ -z "$downloaded" ]] ||
           ! version_output="$(tool_version_from_temporary_directory "$downloaded" --version 2>/dev/null)" ||
           [[ "$version_output" != *"$PREMAKE_VERSION"* ]]; then
            step "The verified Premake binary is not compatible with this Linux userspace; building the pinned source"
            downloaded=""
        fi
    fi
    if [[ -z "$downloaded" ]]; then
        install_logical_packages build uuid
        source_archive="$TEMPORARY/premake-source.tar.gz"
        mkdir -p "$TEMPORARY/source"
        curl -fsSL "$PREMAKE_SOURCE_URL" -o "$source_archive"
        [[ "$(sha256sum "$source_archive" | awk '{print $1}')" == "$PREMAKE_SOURCE_HASH" ]] || {
            printf 'Premake source checksum mismatch.\n' >&2
            exit 1
        }
        tar -xf "$source_archive" -C "$TEMPORARY/source"
        source="$(find "$TEMPORARY/source" -maxdepth 1 -type d -name 'premake-core-*' -print -quit)"
        [[ -n "$source" ]] || { printf 'Premake source directory was not found.\n' >&2; exit 1; }
        if [[ "$ARCHITECTURE" == ARM64 ]]; then
            make -C "$source" -f Bootstrap.mak linux PLATFORM=ARM64
        else
            make -C "$source" -f Bootstrap.mak linux
        fi
        downloaded="$(find_premake_binary "$source")"
        [[ -n "$downloaded" ]] || { printf 'Premake source build did not produce an executable.\n' >&2; exit 1; }
        version_output="$(tool_version_from_temporary_directory "$downloaded" --version)" || {
            printf 'Built Premake could not report its version.\n' >&2
            exit 1
        }
        [[ "$version_output" == *"$PREMAKE_VERSION"* ]] || {
            printf 'Built Premake did not report the pinned version %s.\n' "$PREMAKE_VERSION" >&2
            exit 1
        }
    fi
    cp "$downloaded" "$PREMAKE"
    chmod +x "$PREMAKE"
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

install_cmake() {
    local actual="" cache_root install_root url hash archive candidate tool
    if have cmake && actual="$(cmake --version 2>/dev/null | extract_version)"; then
        if [[ $UPDATE -eq 0 ]] && version_at_least "$actual" 3.24; then
            step "CMake $actual already available"
            return
        fi
    fi

    step "Installing or updating CMake"
    install_logical_packages cmake
    if have cmake && actual="$(cmake --version 2>/dev/null | extract_version)" &&
       version_at_least "$actual" 3.24; then
        step "CMake $actual is ready"
        return
    fi

    step "The distro repositories did not provide CMake 3.24 or newer; installing the pinned binary"
    ensure_command curl curl
    ensure_command sha256sum coreutils
    ensure_command tar tar
    cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/toolchains"
    install_root="$cache_root/cmake-$CMAKE_VERSION-linux-$(architecture_output_name "$ARCHITECTURE")"
    if [[ "$ARCHITECTURE" == ARM64 ]]; then
        url="$CMAKE_ARM64_URL"; hash="$CMAKE_ARM64_HASH"
    else
        url="$CMAKE_X64_URL"; hash="$CMAKE_X64_HASH"
    fi
    mkdir -p "$cache_root" "$ROOT/Tools/Linux"
    if [[ $FORCE -eq 0 && -x "$install_root/bin/cmake" ]]; then
        actual="$("$install_root/bin/cmake" --version | extract_version)" || actual=""
        if [[ "$actual" == "$CMAKE_VERSION" ]]; then
            for tool in cmake ctest cpack; do
                ln -sfn "$install_root/bin/$tool" "$ROOT/Tools/Linux/$tool"
            done
            step "Pinned CMake $CMAKE_VERSION already installed"
            return
        fi
    fi

    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/cmake.tar.gz"
    candidate="$TEMPORARY/cmake"
    mkdir -p "$candidate"
    curl -fsSL "$url" -o "$archive"
    [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$hash" ]] || {
        printf 'CMake checksum mismatch.\n' >&2
        exit 1
    }
    tar -xzf "$archive" -C "$candidate" --strip-components=1
    actual="$("$candidate/bin/cmake" --version | extract_version)" || {
        printf 'Downloaded CMake could not start on this Linux userspace.\n' >&2
        exit 1
    }
    [[ "$actual" == "$CMAKE_VERSION" ]] || {
        printf 'Downloaded CMake reported %s; expected %s.\n' "$actual" "$CMAKE_VERSION" >&2
        exit 1
    }
    if [[ -e "$install_root" ]]; then
        case "$install_root" in "$cache_root"/*) rm -rf "$install_root" ;; *) exit 1 ;; esac
    fi
    mv "$candidate" "$install_root"
    for tool in cmake ctest cpack; do
        ln -sfn "$install_root/bin/$tool" "$ROOT/Tools/Linux/$tool"
    done
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

install_ninja() {
    local actual="" archive source built
    if have ninja; then
        actual="$(ninja --version)"
        if [[ $UPDATE -eq 0 ]] && version_at_least "$actual" 1.11; then
            step "Ninja $actual already available"
            return
        fi
    fi

    step "Installing or updating Ninja"
    if install_logical_packages ninja && have ninja; then
        actual="$(ninja --version)"
        if version_at_least "$actual" 1.11; then
            step "Ninja $actual is ready"
            return
        fi
    fi

    step "The distro repositories did not provide Ninja 1.11 or newer; building the pinned source"
    install_logical_packages build
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/ninja-source.tar.gz"
    mkdir -p "$TEMPORARY/source"
    curl -fsSL "$NINJA_SOURCE_URL" -o "$archive"
    [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$NINJA_SOURCE_HASH" ]] || {
        printf 'Ninja source checksum mismatch.\n' >&2
        exit 1
    }
    tar -xf "$archive" -C "$TEMPORARY/source"
    source="$(find "$TEMPORARY/source" -maxdepth 1 -type d -name 'ninja-*' -print -quit)"
    [[ -n "$source" ]] || { printf 'Ninja source directory was not found.\n' >&2; exit 1; }
    cmake -S "$source" -B "$TEMPORARY/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build "$TEMPORARY/build" --parallel "$(build_parallel_jobs)"
    built="$TEMPORARY/build/ninja"
    [[ -x "$built" ]] || { printf 'Ninja source build did not produce an executable.\n' >&2; exit 1; }
    actual="$("$built" --version)"
    [[ "$actual" == "$NINJA_VERSION" ]] || {
        printf 'Built Ninja reported %s; expected %s.\n' "$actual" "$NINJA_VERSION" >&2
        exit 1
    }
    mkdir -p "$ROOT/Tools/Linux"
    cp "$built" "$ROOT/Tools/Linux/ninja"
    chmod +x "$ROOT/Tools/Linux/ninja"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

install_dotnet_sdk() {
    local cache_root install_root url hash archive candidate listing
    install_logical_packages dotnet-runtime-deps
    ensure_command sha512sum coreutils
    cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/keire/toolchains"
    install_root="$cache_root/dotnet-sdk-$DOTNET_SDK_VERSION-linux-$(architecture_output_name "$ARCHITECTURE")"
    mkdir -p "$cache_root" "$ROOT/Tools/Linux"
    if [[ $FORCE -eq 0 && -x "$install_root/dotnet" ]]; then
        listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 "$install_root/dotnet" --list-sdks)" || listing=""
        if grep -Fq "$DOTNET_SDK_VERSION [$install_root/sdk]" <<< "$listing"; then
            ln -sfn "$install_root/dotnet" "$ROOT/Tools/Linux/dotnet"
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
    [[ "$(sha512sum "$archive" | awk '{print $1}')" == "$hash" ]] || {
        printf '.NET SDK checksum mismatch.\n' >&2
        exit 1
    }
    tar -xzf "$archive" -C "$candidate"
    listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 "$candidate/dotnet" --list-sdks)" || {
        printf 'Downloaded .NET SDK could not start on this Linux userspace.\n' >&2
        exit 1
    }
    grep -Fq "$DOTNET_SDK_VERSION [$candidate/sdk]" <<< "$listing" || {
        printf 'Downloaded .NET SDK did not report the pinned version %s.\n' "$DOTNET_SDK_VERSION" >&2
        exit 1
    }
    if [[ -e "$install_root" ]]; then
        case "$install_root" in "$cache_root"/*) rm -rf "$install_root" ;; *) exit 1 ;; esac
    fi
    mv "$candidate" "$install_root"
    ln -sfn "$install_root/dotnet" "$ROOT/Tools/Linux/dotnet"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

install_nasm() {
    local actual="" archive source built
    if have nasm; then
        actual="$(nasm -v | extract_version)"
        if version_at_least "$actual" 2.14; then
            step "NASM $actual already available"
            return
        fi
    fi

    step "Installing or updating NASM"
    if install_logical_packages nasm >/dev/null 2>&1 && have nasm; then
        actual="$(nasm -v | extract_version)"
        if version_at_least "$actual" 2.14; then
            step "NASM $actual is ready"
            return
        fi
    fi

    step "The distro repositories did not provide NASM 2.14 or newer; building the pinned source"
    install_logical_packages build
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/nasm-source.tar.gz"
    mkdir -p "$TEMPORARY/source" "$TEMPORARY/install"
    curl -fsSL "$NASM_SOURCE_URL" -o "$archive"
    [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$NASM_SOURCE_HASH" ]] || {
        printf 'NASM source checksum mismatch.\n' >&2
        exit 1
    }
    tar -xf "$archive" -C "$TEMPORARY/source"
    source="$(find "$TEMPORARY/source" -maxdepth 1 -type d -name 'nasm-*' -print -quit)"
    [[ -n "$source" ]] || { printf 'NASM source directory was not found.\n' >&2; exit 1; }
    (cd "$source" && ./configure --prefix="$TEMPORARY/install")
    make -C "$source" -j"$(build_parallel_jobs)"
    make -C "$source" install
    built="$TEMPORARY/install/bin/nasm"
    [[ -x "$built" ]] || { printf 'NASM source build did not produce an executable.\n' >&2; exit 1; }
    actual="$("$built" -v | extract_version)"
    [[ "$actual" == "$NASM_VERSION" ]] || {
        printf 'Built NASM reported %s; expected %s.\n' "$actual" "$NASM_VERSION" >&2
        exit 1
    }
    mkdir -p "$ROOT/Tools/Linux"
    cp "$built" "$ROOT/Tools/Linux/nasm"
    chmod +x "$ROOT/Tools/Linux/nasm"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

install_patchelf() {
    local actual="" archive source built
    if have patchelf; then
        actual="$(patchelf --version | extract_version)"
        if version_at_least "$actual" 0.14; then
            step "patchelf $actual already available"
            return
        fi
    fi

    step "Installing or updating patchelf"
    if install_logical_packages patchelf >/dev/null 2>&1 && have patchelf; then
        actual="$(patchelf --version | extract_version)"
        if version_at_least "$actual" 0.14; then
            step "patchelf $actual is ready"
            return
        fi
    fi

    step "The distro repositories did not provide patchelf 0.14 or newer; building the pinned source"
    install_logical_packages build
    TEMPORARY="$(mktemp -d)"
    archive="$TEMPORARY/patchelf-source.tar.gz"
    mkdir -p "$TEMPORARY/source" "$TEMPORARY/install"
    curl -fsSL "$PATCHELF_SOURCE_URL" -o "$archive"
    [[ "$(sha256sum "$archive" | awk '{print $1}')" == "$PATCHELF_SOURCE_HASH" ]] || {
        printf 'patchelf source checksum mismatch.\n' >&2
        exit 1
    }
    tar -xf "$archive" -C "$TEMPORARY/source"
    source="$(find "$TEMPORARY/source" -maxdepth 1 -type d -name 'patchelf-*' -print -quit)"
    [[ -n "$source" ]] || { printf 'patchelf source directory was not found.\n' >&2; exit 1; }
    (cd "$source" && ./configure --prefix="$TEMPORARY/install")
    make -C "$source" -j"$(build_parallel_jobs)"
    make -C "$source" install
    built="$TEMPORARY/install/bin/patchelf"
    [[ -x "$built" ]] || { printf 'patchelf source build did not produce an executable.\n' >&2; exit 1; }
    actual="$("$built" --version | extract_version)"
    [[ "$actual" == "$PATCHELF_VERSION" ]] || {
        printf 'Built patchelf reported %s; expected %s.\n' "$actual" "$PATCHELF_VERSION" >&2
        exit 1
    }
    mkdir -p "$ROOT/Tools/Linux"
    cp "$built" "$ROOT/Tools/Linux/patchelf"
    chmod +x "$ROOT/Tools/Linux/patchelf"
    rm -rf "$TEMPORARY"
    TEMPORARY=""
}

write_compiler_shim() {
    local name="$1" target="$2" environment="${3:-}" temporary
    temporary="$ROOT/Tools/Linux/.$name.tmp.$$"
    if [[ -n "$environment" ]]; then
        printf '#!/usr/bin/env bash\nsource "%s"\nexec "%s" "$@"\n' "$environment" "$target" > "$temporary"
    else
        printf '#!/usr/bin/env bash\nexec "%s" "$@"\n' "$target" > "$temporary"
    fi
    chmod +x "$temporary"
    mv "$temporary" "$ROOT/Tools/Linux/$name"
}

install_gcc_toolchain() {
    local compiler="" cxx="" actual="" environment="" environment_temporary=""
    if have g++ && actual="$(g++ -dumpfullversion -dumpversion 2>/dev/null)"; then
        if version_at_least "$actual" 12; then
            step "GCC $actual already available"
            return
        fi
    fi

    install_logical_packages cxx
    if have g++ && actual="$(g++ -dumpfullversion -dumpversion 2>/dev/null)"; then
        if version_at_least "$actual" 12; then
            step "GCC $actual is ready"
            return
        fi
    fi

    detect_package_manager
    case "$PACKAGE_MANAGER" in
        apt-get)
            install_logical_packages modern-cxx
            compiler="$(command -v gcc-12 2>/dev/null || true)"
            cxx="$(command -v g++-12 2>/dev/null || true)"
            ;;
        dnf)
            install_logical_packages modern-cxx
            compiler=/opt/rh/gcc-toolset-12/root/usr/bin/gcc
            cxx=/opt/rh/gcc-toolset-12/root/usr/bin/g++
            environment="$ROOT/Tools/Linux/gcc-environment.sh"
            ;;
    esac
    [[ -x "$compiler" && -x "$cxx" ]] || {
        printf 'This distribution does not provide GCC 12 or newer through its configured repositories. Use --toolset clang or install a supported GCC toolchain.\n' >&2
        exit 1
    }
    actual="$("$cxx" -dumpfullversion -dumpversion)"
    version_at_least "$actual" 12 || {
        printf 'Installed GCC %s is older than required 12.\n' "$actual" >&2
        exit 1
    }
    mkdir -p "$ROOT/Tools/Linux"
    if [[ -n "$environment" ]]; then
        environment_temporary="$ROOT/Tools/Linux/.gcc-environment.sh.tmp.$$"
        printf '%s\n' \
            'export LD_LIBRARY_PATH="/opt/rh/gcc-toolset-12/root/usr/lib64:/opt/rh/gcc-toolset-12/root/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"' \
            > "$environment_temporary"
        mv "$environment_temporary" "$environment"
    fi
    write_compiler_shim gcc "$compiler" "$environment"
    write_compiler_shim g++ "$cxx" "$environment"
    step "Project-private GCC $actual shims are ready"
}

ensure_command awk awk
ensure_command find findutils
install_premake
ensure_command git git
check_version Git "$(git --version | extract_version)" 2.34
install_cmake

install_ninja
ensure_command pkg-config pkg-config
ensure_command python3 python
ensure_command bison bison
ensure_command flex flex
[[ "$ARCHITECTURE" == x86_64 ]] && install_nasm
install_patchelf
ensure_command perl perl
install_logical_packages perl-json
install_logical_packages perl-open
perl -Mopen=:std -e 1 || { printf 'Perl open module is unavailable.\n' >&2; exit 1; }
install_dotnet_sdk
install_logical_packages curl-dev
install_logical_packages sdl-video
if ! pkg-config --exists x11 && ! pkg-config --exists wayland-client; then
    printf 'SDL video requires at least one available X11 or Wayland development backend.\n' >&2
    exit 1
fi
if [[ "$GENERATOR" == gmake ]]; then
    install_logical_packages build
    check_version Make "$(make --version | extract_version)" 4.3
fi
case "$TOOLSET" in
    default|gcc)
        install_gcc_toolchain
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
