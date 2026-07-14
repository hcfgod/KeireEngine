#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
LOCK="$ROOT/Config/Dependencies.lock"
command -v git >/dev/null 2>&1 || { printf 'Git is required.\n' >&2; exit 1; }
git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 || git -C "$ROOT" init

install_dependency() {
    local name="$1" path="$2" url="$3" commit="$4" directory="$ROOT/$2" entry index_commit actual
    entry="$(git -C "$ROOT" ls-files --stage -- "$path" 2>/dev/null || true)"
    if [[ "$entry" == 160000\ * ]]; then
        index_commit="$(printf '%s\n' "$entry" | awk '{print $2}')"
        [[ "$index_commit" == "$commit" ]] || { printf '%s lock is %s; committed submodule pointer is %s.\n' "$name" "$commit" "$index_commit" >&2; return 1; }
        git -C "$ROOT" submodule update --init --recursive -- "$path"
    elif [[ ! -e "$directory" ]]; then git clone --quiet "$url" "$directory"; git -C "$directory" checkout --quiet "$commit"
    elif ! git -C "$directory" rev-parse --is-inside-work-tree >/dev/null 2>&1; then printf '%s is not a Git repository.\n' "$path" >&2; return 1; fi
    actual="$(git -C "$directory" rev-parse HEAD)"
    if [[ "$actual" != "$commit" && "$entry" == 160000\ * ]]; then
        printf '==> Restoring %s working tree to committed hash %s\n' "$name" "$commit"
        git -C "$directory" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$directory" fetch --no-tags origin "$commit"
        git -C "$directory" checkout --quiet --detach "$commit"
        actual="$(git -C "$directory" rev-parse HEAD)"
    fi
    [[ "$actual" == "$commit" ]] || { printf '%s is at %s; expected %s.\n' "$name" "$actual" "$commit" >&2; return 1; }
    printf '==> %s verified at %s\n' "$name" "$actual"
}

install_dependency spdlog Vendor/spdlog "$(config_value "$LOCK" SPDLOG_URL)" "$(config_value "$LOCK" SPDLOG_COMMIT)"
install_dependency doctest Vendor/doctest "$(config_value "$LOCK" DOCTEST_URL)" "$(config_value "$LOCK" DOCTEST_COMMIT)"
install_dependency SDL Vendor/SDL "$(config_value "$LOCK" SDL_URL)" "$(config_value "$LOCK" SDL_COMMIT)"
install_dependency json Vendor/json "$(config_value "$LOCK" JSON_URL)" "$(config_value "$LOCK" JSON_COMMIT)"
printf '==> Vendor libraries are ready; Git staging was not modified\n'
