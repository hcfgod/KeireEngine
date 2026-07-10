#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT/Vendor"

step() {
    printf '==> %s\n' "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

is_git_repo() {
    local path="$1"
    [[ -e "$path" ]] && git -C "$path" rev-parse --is-inside-work-tree >/dev/null 2>&1
}

is_git_index_path() {
    local path="$1"
    git -C "$ROOT" ls-files --error-unmatch "$path" >/dev/null 2>&1
}

if ! have git; then
    printf 'Git is required to install vendor submodules.\n' >&2
    exit 1
fi

if ! is_git_repo "$ROOT"; then
    step "Initializing Git repository"
    git -C "$ROOT" init
fi

mkdir -p "$VENDOR_DIR"

install_vendor_dependency() {
    local name="$1"
    local path="$2"
    local url="$3"
    local tag="$4"
    local dependency_dir="$ROOT/$path"

    if [[ -e "$dependency_dir" ]]; then
        if ! is_git_repo "$dependency_dir"; then
            printf '%s already exists but is not a Git repository or submodule. Move it aside before bootstrapping vendor libraries.\n' "$path" >&2
            exit 1
        fi

        step "Updating $name submodule"
        git -C "$ROOT" submodule update --init --recursive -- "$path"
    elif is_git_index_path "$path"; then
        step "Restoring $name submodule"
        git -C "$ROOT" submodule update --init --recursive -- "$path"
    else
        step "Adding $name submodule"
        git -C "$ROOT" submodule add "$url" "$path"
    fi

    step "Checking out $name $tag"
    git -C "$dependency_dir" fetch --tags --force --quiet
    git -C "$dependency_dir" checkout --quiet "$tag"

    step "Recording $name submodule pointer"
    git -C "$ROOT" add .gitmodules "$path"
}

install_vendor_dependency "spdlog" "Vendor/spdlog" "https://github.com/gabime/spdlog.git" "v1.17.0"
install_vendor_dependency "doctest" "Vendor/doctest" "https://github.com/doctest/doctest.git" "v2.5.3"

step "Vendor libraries are ready"
