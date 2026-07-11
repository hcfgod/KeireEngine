#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=Unix/common.sh
source "$SCRIPT_DIR/Unix/common.sh"

case "$(uname -s)" in
    Darwin) PLATFORM_DIR="$SCRIPT_DIR/Mac"; PLATFORM_NAME="Mac"; GENERATOR="xcode4" ;;
    Linux) PLATFORM_DIR="$SCRIPT_DIR/Linux"; PLATFORM_NAME="Linux"; GENERATOR="ninja" ;;
    *) printf 'Use Scripts/project.ps1 on Windows.\n' >&2; exit 1 ;;
esac

COMMAND="${1:-menu}"
[[ "$COMMAND" == "menu" ]] || shift || true
CONFIGURATION="Debug"
ARCHITECTURE="$(native_architecture)"
TOOLSET="default"
TARGET="Client"
DEPENDENCY="spdlog"
TAG=""
INSTALL_OPTIONAL=0
UPDATE=0
FORCE=0
CI=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generator) GENERATOR="$2"; shift 2 ;;
        --configuration) CONFIGURATION="$(normalize_configuration "$2")"; shift 2 ;;
        --architecture) ARCHITECTURE="$(normalize_architecture "$2")"; shift 2 ;;
        --toolset) TOOLSET="$2"; shift 2 ;;
        --target) TARGET="$2"; shift 2 ;;
        --dependency) DEPENDENCY="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --install-optional) INSTALL_OPTIONAL=1; shift ;;
        --update) UPDATE=1; shift ;;
        --force) FORCE=1; shift ;;
        --ci) CI=1; shift ;;
        *) printf "Unknown argument '%s'.\n" "$1" >&2; exit 1 ;;
    esac
done

run_command() {
    validate_unix_combination "$PLATFORM_NAME" "$GENERATOR" "$TOOLSET"
    local common=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
    [[ $CI -eq 1 ]] && common+=(--ci)
    [[ $UPDATE -eq 1 ]] && common+=(--update)
    [[ $FORCE -eq 1 ]] && common+=(--force)
    case "$1" in
        bootstrap)
            [[ $INSTALL_OPTIONAL -eq 1 ]] && common+=(--install-optional)
            bash "$PLATFORM_DIR/bootstrap.sh" "${common[@]}"
            ;;
        generate) bash "$PLATFORM_DIR/generate.sh" "${common[@]}" ;;
        build) bash "$PLATFORM_DIR/build.sh" "${common[@]}" --configuration "$CONFIGURATION" --target "$TARGET" ;;
        test) bash "$PLATFORM_DIR/test.sh" "${common[@]}" --configuration "$CONFIGURATION" ;;
        run) bash "$PLATFORM_DIR/run.sh" "${common[@]}" --configuration "$CONFIGURATION" ;;
        clean) bash "$PLATFORM_DIR/clean.sh" ;;
        vendor-update)
            [[ -n "$TAG" ]] || { printf '%s\n' '--tag is required for vendor-update.' >&2; return 1; }
            bash "$PLATFORM_DIR/vendor-update.sh" "$DEPENDENCY" "$TAG"
            ;;
        *) printf "Unknown command '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

read_setting() {
    local prompt="$1" current="$2" value
    printf '%s [%s]: ' "$prompt" "$current" >&2
    read -r value
    printf '%s' "${value:-$current}"
}

show_menu() {
    while true; do
        printf '\nCrossPlatformCoreClientTemplate\n1. Bootstrap prerequisites\n2. Generate project files\n3. Build\n4. Run tests\n5. Run Client\n6. Clean\n7. Exit\n\nChoose an option: '
        read -r choice
        case "$choice" in
            1|2|3|4|5)
                GENERATOR="$(read_setting 'Generator' "$GENERATOR")"
                ARCHITECTURE="$(normalize_architecture "$(read_setting 'Architecture (x86_64, ARM64)' "$ARCHITECTURE")")"
                TOOLSET="$(read_setting 'Toolset (default, gcc, clang)' "$TOOLSET")"
                update_choice="$(read_setting 'Update installed prerequisites (yes, no)' "$([[ $UPDATE -eq 1 ]] && printf yes || printf no)")"
                [[ "$update_choice" =~ ^([Yy]|[Yy][Ee][Ss])$ ]] && UPDATE=1 || UPDATE=0
                [[ "$choice" =~ ^[345]$ ]] && CONFIGURATION="$(normalize_configuration "$(read_setting 'Configuration' "$CONFIGURATION")")"
                case "$choice" in 1) run_command bootstrap;; 2) run_command generate;; 3) run_command build;; 4) run_command test;; 5) run_command run;; esac || printf '\nCommand failed.\n' >&2
                ;;
            6) run_command clean || printf '\nCommand failed.\n' >&2 ;;
            7) return ;;
            *) printf 'Invalid menu choice.\n' >&2 ;;
        esac
        printf '\nPress Enter to return to the menu.'; read -r _
    done
}

if [[ "$COMMAND" == "menu" ]]; then show_menu; else run_command "$COMMAND"; fi
