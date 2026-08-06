#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
load_project_config "$ROOT"
scope="${1:-all}"
[[ "$scope" == full ]] && scope=all
[[ "$scope" == all || "$scope" == build || "$scope" == generated ]] || { printf 'Clean scope must be full, build, or generated.\n' >&2; exit 1; }

safe_remove() {
    local path="$1" resolved
    [[ -e "$path" ]] || return 0
    resolved="$(cd "$(dirname "$path")" && pwd)/$(basename "$path")"
    [[ "$resolved" == "$ROOT"/* ]] || { printf 'Refusing to remove path outside repository: %s\n' "$resolved" >&2; return 1; }
    rm -rf "$resolved"
    printf 'Removed %s\n' "$resolved"
}

if [[ "$scope" == all ]]; then
    safe_remove "$ROOT/Build"
    safe_remove "$ROOT/Artifacts"
elif [[ "$scope" == build ]]; then
    if [[ -d "$ROOT/Build" ]]; then
        while IFS= read -r -d '' path; do
            safe_remove "$path"
        done < <(find "$ROOT/Build" -mindepth 1 -maxdepth 1 \
            ! -name Dependencies ! -name Generated ! -name Projects -print0)
    fi
    safe_remove "$ROOT/Artifacts"
fi
if [[ "$scope" == all || "$scope" == generated ]]; then
    if [[ "$scope" == generated ]]; then
        safe_remove "$ROOT/Build/Generated"
        safe_remove "$ROOT/Build/Projects"
    fi
    find "$ROOT" -maxdepth 1 \( -name '*.sln' -o -name '*.slnx' -o -name Makefile -o -name build.ninja -o -name compile_commands.json -o -name '*.xcodeproj' -o -name '*.xcworkspace' \) -exec rm -rf {} +
    project_directories=("$ROOT/$CORE_DIRECTORY" "$ROOT/$CLIENT_DIRECTORY" "$ROOT/$HUB_DIRECTORY" "$ROOT/$TESTS_DIRECTORY")
    [[ ! -d "$ROOT/AssetTool" ]] || project_directories+=("$ROOT/AssetTool")
    [[ ! -d "$ROOT/KeireAssetWorker" ]] || project_directories+=("$ROOT/KeireAssetWorker")
    [[ ! -d "$ROOT/KeireRuntime" ]] || project_directories+=("$ROOT/KeireRuntime")
    find "${project_directories[@]}" -maxdepth 1 \( -name '*.vcxproj' -o -name '*.vcxproj.filters' -o -name '*.vcxproj.user' -o -name Makefile -o -name '*.make' -o -name '*.ninja' -o -name '*.xcodeproj' -o -name '*.xcworkspace' \) -exec rm -rf {} +
fi
printf '==> Clean complete\n'
