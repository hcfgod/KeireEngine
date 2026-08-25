#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPENDENCY="${1:-}"; TAG="${2:-}"; LOCK="$ROOT/Config/Dependencies.lock"
case "$DEPENDENCY" in spdlog|doctest|Tracy|SDL|json|imgui|zstd|entt|glm|SDL_shadercross|assimp|stb) ;; *) printf 'Unsupported dependency.\n' >&2; exit 1;; esac
[[ -n "$TAG" ]] || { printf 'A tag is required.\n' >&2; exit 1; }
DIRECTORY="$ROOT/Vendor/$DEPENDENCY"
[[ "$DEPENDENCY" == Tracy ]] && DIRECTORY="$ROOT/Build/Dependencies/tracy"
[[ -d "$DIRECTORY" ]] || { printf 'Initialize the selected dependency first.\n' >&2; exit 1; }
git -C "$DIRECTORY" fetch --tags --force
git -C "$DIRECTORY" checkout --detach "$TAG"
COMMIT="$(git -C "$DIRECTORY" rev-parse HEAD)"; PREFIX="$(printf '%s' "$DEPENDENCY" | tr '[:lower:]' '[:upper:]')"
awk -v prefix="$PREFIX" -v tag="$TAG" -v commit="$COMMIT" '$0 ~ "^" prefix "_TAG=" {print prefix "_TAG=" tag; next} $0 ~ "^" prefix "_COMMIT=" {print prefix "_COMMIT=" commit; next} {print}' "$LOCK" > "$LOCK.tmp"
mv "$LOCK.tmp" "$LOCK"
printf '==> %s now points to %s (%s)\nReview it, then run:\n' "$DEPENDENCY" "$TAG" "$COMMIT"
if [[ "$DEPENDENCY" == Tracy ]]; then
  printf '  git add Config/Dependencies.lock\n'
else
  printf '  git add Vendor/%s Config/Dependencies.lock\n' "$DEPENDENCY"
fi
