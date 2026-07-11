#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPENDENCY="${1:-}"
TAG="${2:-}"
case "$DEPENDENCY" in spdlog|doctest) ;; *) printf 'Dependency must be spdlog or doctest.\n' >&2; exit 1;; esac
[[ -n "$TAG" ]] || { printf 'A tag is required.\n' >&2; exit 1; }
DIRECTORY="$ROOT/Vendor/$DEPENDENCY"
[[ -d "$DIRECTORY" ]] || { printf 'Run bootstrap before updating vendors.\n' >&2; exit 1; }
git -C "$DIRECTORY" fetch --tags --force
git -C "$DIRECTORY" checkout --detach "$TAG"
COMMIT="$(git -C "$DIRECTORY" rev-parse HEAD)"
printf '==> %s now points to %s (%s)\n' "$DEPENDENCY" "$TAG" "$COMMIT"
printf 'Review it, update pinned commits in vendor scripts, then run:\n  git add Vendor/%s Scripts\n' "$DEPENDENCY"
