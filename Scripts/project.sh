#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNAME="$(uname -s)"
COMMAND="${1:-menu}"
GENERATOR=""
CONFIGURATION="Debug"
TARGET="Client"
INSTALL_OPTIONAL=0
FORCE=0

if [[ "$COMMAND" != "menu" ]]; then
    shift || true
fi

case "$UNAME" in
    Darwin)
        PLATFORM_DIR="$SCRIPT_DIR/Mac"
        DEFAULT_GENERATOR="xcode4"
        ;;
    Linux)
        PLATFORM_DIR="$SCRIPT_DIR/Linux"
        DEFAULT_GENERATOR="ninja"
        ;;
    *)
        printf "Unsupported platform '%s'. Use Scripts/project.ps1 on Windows.\n" "$UNAME" >&2
        exit 1
        ;;
esac

GENERATOR="$DEFAULT_GENERATOR"

normalize_configuration() {
    case "$1" in
        Debug|debug) printf 'Debug' ;;
        Release|release) printf 'Release' ;;
        Dist|dist) printf 'Dist' ;;
        DebugASan|debugasan) printf 'DebugASan' ;;
        DebugUBSan|debugubsan) printf 'DebugUBSan' ;;
        DebugTSan|debugtsan) printf 'DebugTSan' ;;
        *)
            printf "Unsupported configuration '%s'. Expected Debug, Release, Dist, DebugASan, DebugUBSan, or DebugTSan.\n" "$1" >&2
            return 1
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --configuration)
            CONFIGURATION="$(normalize_configuration "$2")"
            shift 2
            ;;
        --target)
            TARGET="$2"
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
            if [[ -z "${GENERATOR_SET:-}" ]]; then
                GENERATOR="$1"
                GENERATOR_SET=1
            else
                printf "Unknown argument '%s'.\n" "$1" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

run_command() {
    local selected_command="$1"

    case "$selected_command" in
        bootstrap)
            local args=("--generator" "$GENERATOR")
            [[ "$INSTALL_OPTIONAL" -eq 1 ]] && args+=("--install-optional")
            [[ "$FORCE" -eq 1 ]] && args+=("--force")
            bash "$PLATFORM_DIR/bootstrap.sh" "${args[@]}"
            ;;
        generate)
            bash "$PLATFORM_DIR/generate.sh" "$GENERATOR"
            ;;
        build)
            bash "$PLATFORM_DIR/build.sh" "$GENERATOR" "$CONFIGURATION" "$TARGET"
            ;;
        test)
            bash "$PLATFORM_DIR/test.sh" "$GENERATOR" "$CONFIGURATION"
            ;;
        clean)
            bash "$PLATFORM_DIR/clean.sh"
            ;;
        *)
            printf "Unknown command '%s'.\n" "$selected_command" >&2
            exit 1
            ;;
    esac
}

show_menu() {
    while true; do
        printf '\nCrossPlatformCoreClientTemplate\n'
        printf '1. Bootstrap prerequisites\n'
        printf '2. Generate project files\n'
        printf '3. Build\n'
        printf '4. Run tests\n'
        printf '5. Clean\n'
        printf '6. Exit\n\n'
        printf 'Choose an option: '
        read -r choice

        case "$choice" in
            1)
                if ! run_command bootstrap; then
                    printf '\nCommand failed.\n' >&2
                fi
                ;;
            2)
                printf 'Generator [%s]: ' "$GENERATOR"
                read -r selected_generator
                [[ -n "$selected_generator" ]] && GENERATOR="$selected_generator"
                if ! run_command generate; then
                    printf '\nCommand failed.\n' >&2
                fi
                ;;
            3)
                printf 'Generator [%s]: ' "$GENERATOR"
                read -r selected_generator
                [[ -n "$selected_generator" ]] && GENERATOR="$selected_generator"
                printf 'Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan) [%s]: ' "$CONFIGURATION"
                read -r selected_configuration
                if [[ -n "$selected_configuration" ]]; then
                    if ! CONFIGURATION="$(normalize_configuration "$selected_configuration")"; then
                        printf '\nCommand failed.\n' >&2
                        printf '\nPress Enter to return to the menu.'
                        read -r _
                        continue
                    fi
                fi
                if ! run_command build; then
                    printf '\nCommand failed.\n' >&2
                fi
                ;;
            4)
                printf 'Generator [%s]: ' "$GENERATOR"
                read -r selected_generator
                [[ -n "$selected_generator" ]] && GENERATOR="$selected_generator"
                printf 'Configuration (Debug, Release, Dist, DebugASan, DebugUBSan, DebugTSan) [%s]: ' "$CONFIGURATION"
                read -r selected_configuration
                if [[ -n "$selected_configuration" ]]; then
                    if ! CONFIGURATION="$(normalize_configuration "$selected_configuration")"; then
                        printf '\nCommand failed.\n' >&2
                        printf '\nPress Enter to return to the menu.'
                        read -r _
                        continue
                    fi
                fi
                if ! run_command test; then
                    printf '\nCommand failed.\n' >&2
                fi
                ;;
            5)
                if ! run_command clean; then
                    printf '\nCommand failed.\n' >&2
                fi
                ;;
            6) exit 0 ;;
            *)
                printf "Invalid menu choice '%s'.\n" "$choice" >&2
                ;;
        esac

        printf '\nPress Enter to return to the menu.'
        read -r _
    done
}

if [[ "$COMMAND" == "menu" ]]; then
    show_menu
else
    run_command "$COMMAND"
fi
