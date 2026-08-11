#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=Unix/common.sh
source "$ROOT/Scripts/Unix/common.sh"

GENERATOR=ninja
TOOLSET=gcc
ARCHITECTURE="$(native_architecture)"
RUN_TEST=0
CONFIGURATION=Debug
CONFIGURATION_SET=0
UPDATE=0
FORCE=0
INSTALL_OPTIONAL=0

usage() {
    cat <<'EOF'
Usage: bash Scripts/setup-linux.sh [options]

Prepare a Linux checkout through the authoritative project bootstrap, report the
resolved toolchain, and optionally run the complete selected test configuration.

Options:
  --generator <ninja|gmake>       Build-system generator (default: ninja)
  --toolset <clang|gcc>           Native compiler toolset (default: gcc)
  --architecture <x86_64|ARM64>   Target architecture (default: native)
  --test                          Run the complete test gate after setup
  --configuration <name>          Test configuration (default: Debug; requires --test)
  --jobs <count>                  Override bounded compiler concurrency
  --update                        Update installed prerequisites where supported
  --force                         Reinstall project-private pinned tools
  --install-optional              Install optional bootstrap toolchains
  --help                          Show this help

The script supports apt-get, dnf, pacman, and zypper hosts. It deliberately does
not upgrade the operating system, reboot, install VM guest additions, or change
DNS, firewall, and network configuration.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generator) GENERATOR="${2:?--generator requires a value}"; shift 2 ;;
        --toolset) TOOLSET="${2:?--toolset requires a value}"; shift 2 ;;
        --architecture) ARCHITECTURE="$(normalize_architecture "${2:?--architecture requires a value}")"; shift 2 ;;
        --test) RUN_TEST=1; shift ;;
        --configuration) CONFIGURATION="$(normalize_configuration "${2:?--configuration requires a value}")"; CONFIGURATION_SET=1; shift 2 ;;
        --jobs) export KEIRE_BUILD_JOBS="${2:?--jobs requires a value}"; shift 2 ;;
        --update) UPDATE=1; shift ;;
        --force) FORCE=1; shift ;;
        --install-optional) INSTALL_OPTIONAL=1; shift ;;
        --help) usage; exit 0 ;;
        *) printf 'Unknown Linux setup option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$(uname -s)" == Linux ]] || {
    printf 'Scripts/setup-linux.sh must run on Linux or WSL2.\n' >&2
    exit 1
}
[[ "$GENERATOR" == ninja || "$GENERATOR" == gmake ]] || {
    printf 'Linux workstation setup supports the ninja and gmake generators.\n' >&2
    exit 2
}
[[ "$TOOLSET" == clang || "$TOOLSET" == gcc ]] || {
    printf 'Linux workstation setup supports the clang and gcc toolsets.\n' >&2
    exit 2
}
[[ -z "${KEIRE_BUILD_JOBS:-}" || "${KEIRE_BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]] || {
    printf '%s\n' '--jobs must be a positive integer.' >&2
    exit 2
}
[[ $CONFIGURATION_SET -eq 0 || $RUN_TEST -eq 1 ]] || {
    printf '%s\n' '--configuration requires --test.' >&2
    exit 2
}
[[ -f "$ROOT/Config/Project.conf" && -f "$ROOT/Scripts/project.sh" ]] || {
    printf 'Linux setup must run from a complete Kéire repository checkout.\n' >&2
    exit 1
}

package_manager=""
for candidate in apt-get dnf pacman zypper; do
    if command -v "$candidate" >/dev/null 2>&1; then
        package_manager="$candidate"
        break
    fi
done
[[ -n "$package_manager" ]] || {
    printf 'No supported Linux package manager was found (apt-get, dnf, pacman, or zypper).\n' >&2
    exit 1
}

distro_id=unknown
distro_version=unknown
distro_name='Unknown Linux distribution'
if [[ -r /etc/os-release ]]; then
    # The operating system owns this root-controlled identification file.
    # shellcheck disable=SC1091
    source /etc/os-release
    distro_id="${ID:-unknown}"
    distro_version="${VERSION_ID:-rolling}"
    distro_name="${PRETTY_NAME:-$distro_id $distro_version}"
fi

validation_status='compatible package-manager family; outside the current observed distro matrix'
case "$distro_id:$distro_version" in
    ubuntu:22.04|ubuntu:24.04|debian:12|rocky:9*)
        validation_status='validated Linux baseline'
        ;;
    ubuntu:26.04)
        validation_status='Ubuntu 26.04 setup and matrix target; complete native validation is pending'
        ;;
    fedora:44|arch:*|opensuse-tumbleweed:*)
        validation_status='rolling/current validated Linux family'
        ;;
esac

printf '==> Linux workstation setup\n'
printf '    Host:         %s\n' "$distro_name"
printf '    Package tool: %s\n' "$package_manager"
printf '    Status:       %s\n' "$validation_status"
printf '    Generator:    %s\n' "$GENERATOR"
printf '    Toolset:      %s\n' "$TOOLSET"
printf '    Architecture: %s\n' "$ARCHITECTURE"

common=(--generator "$GENERATOR" --toolset "$TOOLSET" --architecture "$ARCHITECTURE")
bootstrap_arguments=("${common[@]}")
[[ $UPDATE -eq 1 ]] && bootstrap_arguments+=(--update)
[[ $FORCE -eq 1 ]] && bootstrap_arguments+=(--force)
[[ $INSTALL_OPTIONAL -eq 1 ]] && bootstrap_arguments+=(--install-optional)

bash "$ROOT/Scripts/project.sh" bootstrap "${bootstrap_arguments[@]}"
bash "$ROOT/Scripts/project.sh" doctor "${common[@]}"

if [[ $RUN_TEST -eq 1 ]]; then
    bash "$ROOT/Scripts/project.sh" test "${common[@]}" --configuration "$CONFIGURATION"
fi

printf '==> Linux workstation setup complete for %s\n' "$distro_name"
if [[ $RUN_TEST -eq 0 ]]; then
    printf '    Run again with --test to execute the complete Debug validation gate.\n'
fi
printf '    Run the Hub with: bash Scripts/project.sh run --generator %s --toolset %s\n' \
    "$GENERATOR" "$TOOLSET"
