#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
command -v git >/dev/null 2>&1 || { printf 'Git is required.\n' >&2; exit 1; }
git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 || git -C "$ROOT" init

install_dependency() {
    local name="$1" path="$2" url="$3" commit="$4"
    local directory="$ROOT/$path" entry
    entry="$(git -C "$ROOT" ls-files --stage -- "$path" 2>/dev/null || true)"
    if [[ "$entry" == 160000\ * ]]; then
        printf '==> Restoring %s from the committed submodule pointer\n' "$name"
        git -C "$ROOT" submodule update --init --recursive -- "$path"
    elif [[ ! -e "$directory" ]]; then
        printf '==> Cloning %s at its pinned commit\n' "$name"
        git clone --quiet "$url" "$directory"
        git -C "$directory" checkout --quiet "$commit"
    elif ! git -C "$directory" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf '%s exists but is not a Git repository.\n' "$path" >&2
        exit 1
    fi
    local actual
    actual="$(git -C "$directory" rev-parse HEAD)"
    [[ "$actual" == "$commit" ]] || { printf '%s is at %s; expected %s.\n' "$name" "$actual" "$commit" >&2; exit 1; }
    printf '==> %s verified at %s\n' "$name" "$actual"
}

install_dependency spdlog Vendor/spdlog https://github.com/gabime/spdlog.git 79524ddd08a4ec981b7fea76afd08ee05f83755d
install_dependency doctest Vendor/doctest https://github.com/doctest/doctest.git 2d0a9359a60c51affe2a9bebb1be1dca47868151
printf '==> Vendor libraries are ready; Git staging was not modified\n'
