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
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
load_project_config "$ROOT"

COMMAND="${1:-menu}"
[[ "$COMMAND" == "menu" ]] || shift || true
CONFIGURATION="Debug"
CONFIGURATION_SET=0
ARCHITECTURE="$(native_architecture)"
TOOLSET="default"
TARGET="$CLIENT_TARGET"
TARGET_SET=0
DEPENDENCY="spdlog"
TAG=""
NAME=""
DISPLAY_NAME=""
REPOSITORY=""
CLEAN_SCOPE=full
INSTALL_OPTIONAL=0
UPDATE=0
FORCE=0
ALLOW_DIRTY=0
CI=0
SMOKE_UI=0
SMOKE_PROJECT=0
EDITOR=0
PROJECT_PATH=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --generator) GENERATOR="$2"; shift 2 ;;
        --configuration) CONFIGURATION="$(normalize_configuration "$2")"; CONFIGURATION_SET=1; shift 2 ;;
        --architecture) ARCHITECTURE="$(normalize_architecture "$2")"; shift 2 ;;
        --toolset) TOOLSET="$2"; shift 2 ;;
        --target) TARGET="$2"; TARGET_SET=1; shift 2 ;;
        --dependency) DEPENDENCY="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --name) NAME="$2"; shift 2 ;;
        --display-name) DISPLAY_NAME="$2"; shift 2 ;;
        --repository) REPOSITORY="$2"; shift 2 ;;
        --clean-scope) CLEAN_SCOPE="$2"; shift 2 ;;
        --install-optional) INSTALL_OPTIONAL=1; shift ;;
        --update) UPDATE=1; shift ;;
        --force) FORCE=1; shift ;;
        --allow-dirty) ALLOW_DIRTY=1; shift ;;
        --ci) CI=1; shift ;;
        --smoke-ui) SMOKE_UI=1; shift ;;
        --smoke-project) SMOKE_PROJECT=1; shift ;;
        --editor) EDITOR=1; shift ;;
        --project) PROJECT_PATH="$2"; shift 2 ;;
        *) printf "Unknown argument '%s'.\n" "$1" >&2; exit 1 ;;
    esac
done
[[ "$COMMAND" == package && $CONFIGURATION_SET -eq 0 ]] && CONFIGURATION=Release

run_command() {
    local resolved_toolset
    resolved_toolset="$(resolve_unix_toolset "$PLATFORM_NAME" "$TOOLSET")"
    validate_unix_combination "$PLATFORM_NAME" "$GENERATOR" "$resolved_toolset"
    local common=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$resolved_toolset")
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
        run) KEIRE_SMOKE_UI="$SMOKE_UI" KEIRE_SMOKE_PROJECT="$SMOKE_PROJECT" KEIRE_EDITOR="$EDITOR" KEIRE_PROJECT_PATH="$PROJECT_PATH" bash "$PLATFORM_DIR/run.sh" "${common[@]}" --configuration "$CONFIGURATION" ;;
        clean) bash "$PLATFORM_DIR/clean.sh" "$CLEAN_SCOPE" ;;
        coverage) bash "$SCRIPT_DIR/Unix/coverage.sh" "$PLATFORM_NAME" "${common[@]}" ;;
        package)
            [[ $ALLOW_DIRTY -eq 0 ]] || common+=(--allow-dirty)
            bash "$SCRIPT_DIR/Unix/package.sh" "$PLATFORM_NAME" "${common[@]}" --configuration "$CONFIGURATION"
            ;;
        doctor) bash "$SCRIPT_DIR/Unix/doctor.sh" "$PLATFORM_NAME" "${common[@]}" ;;
        rename)
            [[ -n "$NAME" ]] || { printf '%s\n' '--name is required for rename.' >&2; return 1; }
            bash "$SCRIPT_DIR/Unix/rename.sh" "$NAME" "${DISPLAY_NAME:-$NAME}" "$REPOSITORY"
            load_project_config "$ROOT"
            [[ $TARGET_SET -eq 1 ]] || TARGET="$CLIENT_TARGET"
            ;;
        vendor-update)
            [[ -n "$TAG" ]] || { printf '%s\n' '--tag is required for vendor-update.' >&2; return 1; }
            bash "$PLATFORM_DIR/vendor-update.sh" "$DEPENDENCY" "$TAG"
            ;;
        help) show_help ;;
        *) printf "Unknown command '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

show_help() {
    cat <<'EOF'
Usage: Scripts/project.sh <command> [options]

Commands: bootstrap, generate, build, test, run, clean, coverage, package,
          doctor, rename, vendor-update, help

Common options:
  --generator <ninja|gmake|xcode4|compilecommands>
  --configuration <Debug|Release|Dist|DebugASan|DebugUBSan|DebugTSan|Coverage>
  --architecture <x86_64|ARM64> --toolset <default|gcc|clang>
  --smoke-ui (run command only; requires a graphics-capable environment)
  --smoke-project (run the sample project editor and exit after several frames)
  --editor --project <path> (open the editor directly instead of the project hub)
EOF
}

read_setting() {
    local prompt="$1" current="$2" value
    printf '%s [%s]: ' "$prompt" "$current" >&2
    read -r value
    printf '%s' "${value:-$current}"
}

show_menu() {
    while true; do
        printf '\n%s\n1. Bootstrap prerequisites\n2. Generate project files\n3. Build\n4. Run tests\n5. Run %s\n6. Coverage report\n7. Package SDK\n8. Doctor\n9. Clean\n10. Vendor update\n11. Rename template\n12. Exit\n\nChoose an option: ' "$PROJECT_IDENTIFIER" "$HUB_TARGET"
        read -r choice
        case "$choice" in
            1|2|3|4|5)
                GENERATOR="$(read_setting 'Generator' "$GENERATOR")"
                ARCHITECTURE="$(normalize_architecture "$(read_setting 'Architecture (x86_64, ARM64)' "$ARCHITECTURE")")"
                TOOLSET="$(read_setting 'Toolset (default, gcc, clang)' "$TOOLSET")"
                update_choice="$(read_setting 'Update installed prerequisites (yes, no)' "$([[ $UPDATE -eq 1 ]] && printf yes || printf no)")"
                [[ "$update_choice" =~ ^([Yy]|[Yy][Ee][Ss])$ ]] && UPDATE=1 || UPDATE=0
                [[ "$choice" =~ ^[345]$ ]] && CONFIGURATION="$(normalize_configuration "$(read_setting 'Configuration' "$CONFIGURATION")")"
                [[ "$choice" == 1 ]] && { optional_choice="$(read_setting 'Install optional toolchains (yes, no)' no)"; [[ "$optional_choice" =~ ^([Yy]|[Yy][Ee][Ss])$ ]] && INSTALL_OPTIONAL=1 || INSTALL_OPTIONAL=0; }
                [[ "$choice" == 2 ]] && { force_choice="$(read_setting 'Force regeneration (yes, no)' no)"; [[ "$force_choice" =~ ^([Yy]|[Yy][Ee][Ss])$ ]] && FORCE=1 || FORCE=0; }
                case "$choice" in 1) run_command bootstrap;; 2) run_command generate;; 3) run_command build;; 4) run_command test;; 5) run_command run;; esac || printf '\nCommand failed.\n' >&2
                ;;
            6) run_command coverage || printf '\nCommand failed.\n' >&2 ;;
            7) CONFIGURATION="$(normalize_configuration "$(read_setting 'Package configuration (Release, Dist)' Release)")"; run_command package || printf '\nCommand failed.\n' >&2 ;;
            8) run_command doctor || printf '\nCommand failed.\n' >&2 ;;
            9) CLEAN_SCOPE="$(read_setting 'Clean scope (full, build, generated)' "$CLEAN_SCOPE")"; run_command clean || printf '\nCommand failed.\n' >&2 ;;
            10) DEPENDENCY="$(read_setting 'Dependency (spdlog, doctest, SDL, json, imgui, zstd, entt, glm)' "$DEPENDENCY")"; TAG="$(read_setting 'Tag' "$TAG")"; run_command vendor-update || printf '\nCommand failed.\n' >&2 ;;
            11) NAME="$(read_setting 'PascalCase identifier' "$NAME")"; DISPLAY_NAME="$(read_setting 'Display name' "$NAME")"; REPOSITORY="$(read_setting 'Repository (owner/name, optional)' "$REPOSITORY")"; run_command rename || printf '\nCommand failed.\n' >&2 ;;
            12) return ;;
            *) printf 'Invalid menu choice.\n' >&2 ;;
        esac
        printf '\nPress Enter to return to the menu.'; read -r _
    done
}

if [[ "$COMMAND" == "menu" ]]; then show_menu; else run_command "$COMMAND"; fi
