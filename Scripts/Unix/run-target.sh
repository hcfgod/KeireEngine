#!/usr/bin/env bash
set -euo pipefail
PLATFORM="$1"; MODE="$2"; shift 2
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
GENERATOR=ninja; [[ "$PLATFORM" == Mac ]] && GENERATOR=xcode4
CONFIGURATION=Debug; ARCHITECTURE="$(native_architecture)"; TOOLSET=default; TARGET=KeireClient; CI=0; UPDATE=0; FORCE=0; INSTALL_OPTIONAL=0
parse_build_arguments "$@"; load_project_config "$ROOT"; TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
if [[ "$MODE" == test ]]; then
    TARGET="$TESTS_TARGET"
elif [[ "${KEIRE_EDITOR:-0}" == 1 || "${KEIRE_SMOKE_PROJECT:-0}" == 1 || -n "${KEIRE_PROJECT_PATH:-}" || "${KEIRE_SMOKE_WINDOW:-0}" == 1 || $CI -eq 1 ]]; then
    TARGET="$CLIENT_TARGET"
else
    TARGET="$HUB_TARGET"
fi
args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$TARGET")
[[ $CI -eq 1 ]] && args+=(--ci); [[ $UPDATE -eq 1 ]] && args+=(--update); [[ $FORCE -eq 1 ]] && args+=(--force)
bash "$ROOT/Scripts/$PLATFORM/build.sh" "${args[@]}"
system=linux; [[ "$PLATFORM" == Mac ]] && system=macosx
executable="$ROOT/Build/Bin/$CONFIGURATION-$system-$(architecture_output_name "$ARCHITECTURE")/$TARGET/$TARGET"
[[ -x "$executable" ]] || { printf 'Executable not found: %s\n' "$executable" >&2; exit 1; }
printf '==> Running %s %s for %s\n' "$TARGET" "$CONFIGURATION" "$ARCHITECTURE"
if [[ "$MODE" == run && "${KEIRE_SMOKE_UI:-0}" == 1 ]]; then
    (cd "$ROOT" && "$executable" --smoke-ui)
elif [[ "$MODE" == run && "${KEIRE_SMOKE_PROJECT:-0}" == 1 ]]; then
    project_path="${KEIRE_PROJECT_PATH:-$ROOT/Samples/KeireSandbox}"
    (cd "$ROOT" && "$executable" --project "$project_path" --smoke-project)
elif [[ "$MODE" == run && ($CI -eq 1 || "${KEIRE_SMOKE_WINDOW:-0}" == 1) ]]; then
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy "$executable" --smoke-window)
elif [[ "$MODE" == run && ("${KEIRE_EDITOR:-0}" == 1 || -n "${KEIRE_PROJECT_PATH:-}") ]]; then
    [[ -n "${KEIRE_PROJECT_PATH:-}" ]] || { printf '%s\n' '--project is required when launching the editor directly.' >&2; exit 1; }
    (cd "$ROOT" && "$executable" --project "$KEIRE_PROJECT_PATH")
else
    (cd "$ROOT" && "$executable")
fi
if [[ "$MODE" == test && "$CONFIGURATION" =~ ^Debug ]]; then
    probe_output="$(mktemp)"
    set +e
    (cd "$ROOT" && "$executable" --core-assert-probe) 2>"$probe_output"
    probe_status=$?
    set -e
    [[ $probe_status -ne 0 ]] || { printf 'Assertion probe unexpectedly succeeded.\n' >&2; rm -f "$probe_output"; exit 1; }
    grep -q 'Assertion failed: false' "$probe_output"
    grep -q 'assertion probe' "$probe_output"
    rm -f "$probe_output"
fi
if [[ "$MODE" == test ]]; then
    editor_tests_target="${PROJECT_NAMESPACE}EditorTests"
    editor_args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$editor_tests_target")
    [[ $CI -eq 1 ]] && editor_args+=(--ci)
    [[ $UPDATE -eq 1 ]] && editor_args+=(--update)
    [[ $FORCE -eq 1 ]] && editor_args+=(--force)
    bash "$ROOT/Scripts/$PLATFORM/build.sh" "${editor_args[@]}"
    editor_tests="$ROOT/Build/Bin/$CONFIGURATION-$system-$(architecture_output_name "$ARCHITECTURE")/$editor_tests_target/$editor_tests_target"
    [[ -x "$editor_tests" ]] || { printf 'Editor tests executable not found: %s\n' "$editor_tests" >&2; exit 1; }
    (cd "$ROOT" && "$editor_tests")

    printf '==> Building complete client compile gate\n'
    client_build_args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$CLIENT_TARGET")
    [[ $CI -eq 1 ]] && client_build_args+=(--ci)
    [[ $UPDATE -eq 1 ]] && client_build_args+=(--update)
    [[ $FORCE -eq 1 ]] && client_build_args+=(--force)
    bash "$ROOT/Scripts/$PLATFORM/build.sh" "${client_build_args[@]}"
fi
if [[ "$MODE" == test && "$CONFIGURATION" =~ ^(Debug|Release)$ ]]; then
    render_tests_target="${PROJECT_NAMESPACE}RenderTests"
    render_args=(--generator "$GENERATOR" --configuration "$CONFIGURATION" --architecture "$ARCHITECTURE" --toolset "$TOOLSET" --target "$render_tests_target")
    [[ $CI -eq 1 ]] && render_args+=(--ci)
    [[ $UPDATE -eq 1 ]] && render_args+=(--update)
    [[ $FORCE -eq 1 ]] && render_args+=(--force)
    bash "$ROOT/Scripts/$PLATFORM/build.sh" "${render_args[@]}"
    render_tests="$ROOT/Build/Bin/$CONFIGURATION-$system-$(architecture_output_name "$ARCHITECTURE")/$render_tests_target/$render_tests_target"
    [[ -x "$render_tests" ]] || { printf 'GPU render tests executable not found: %s\n' "$render_tests" >&2; exit 1; }
    backends=(vulkan)
    [[ "$PLATFORM" == Mac ]] && backends=(metal)
    for backend in "${backends[@]}"; do
        printf '==> Running GPU render tests with %s\n' "$backend"
        set +e
        (cd "$ROOT" && env -u SDL_VIDEODRIVER KEIRE_GPU_TEST_BACKEND="$backend" "$render_tests" --probe)
        gpu_probe_status=$?
        set -e
        if [[ $gpu_probe_status -eq 77 ]]; then
            printf '==> GPU render tests skipped: %s is unavailable\n' "$backend"
            required=",${KEIRE_REQUIRE_GPU_TESTS:-},"
            [[ "$required" != ",1," && "$required" != ",all," && "$required" != *",$backend,"* ]] || {
                printf 'Required GPU test backend is unavailable: %s\n' "$backend" >&2
                exit 1
            }
            continue
        elif [[ $gpu_probe_status -ne 0 ]]; then
            printf 'GPU render test probe failed for %s with exit code %s.\n' "$backend" "$gpu_probe_status" >&2
            exit "$gpu_probe_status"
        fi
        set +e
        (cd "$ROOT" && env -u SDL_VIDEODRIVER KEIRE_GPU_TEST_BACKEND="$backend" "$render_tests")
        gpu_status=$?
        set -e
        if [[ $gpu_status -ne 0 ]]; then
            printf 'GPU render tests failed for %s with exit code %s.\n' "$backend" "$gpu_status" >&2
            exit "$gpu_status"
        fi
    done
fi
if [[ "$MODE" == run ]]; then
    cli_root="$(mktemp -d)"
    for option in --help -h --version -v; do (cd "$cli_root" && "$executable" "$option" >/dev/null); done
    set +e
    (cd "$cli_root" && "$executable" --invalid >/dev/null 2>&1)
    invalid_status=$?
    set -e
    [[ $invalid_status -eq 2 ]] || { printf '%s invalid option returned %s, expected 2.\n' "$TARGET" "$invalid_status" >&2; rm -rf "$cli_root"; exit 1; }
    [[ ! -d "$cli_root/Logs" ]] || { printf 'Informational KeireClient commands created logs.\n' >&2; rm -rf "$cli_root"; exit 1; }
    rm -rf "$cli_root"
fi
