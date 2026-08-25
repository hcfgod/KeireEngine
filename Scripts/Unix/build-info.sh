#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"
load_project_config "$ROOT"

for value in "$PROJECT_IDENTIFIER" "$PROJECT_DISPLAY_NAME" "$PROJECT_VERSION" "$PROJECT_NAMESPACE" \
  "$PROJECT_MACRO_PREFIX" "$CORE_TARGET" "$CORE_DIRECTORY" "$CLIENT_TARGET" "$CLIENT_DIRECTORY" \
  "$HUB_TARGET" "$HUB_DIRECTORY" \
  "$TESTS_TARGET" "$TESTS_DIRECTORY" "$ARTIFACT_PREFIX" "$REPOSITORY_SLUG"; do
  [[ "$value" != *$'\n'* && "$value" != *$'\r'* ]] || { printf 'Project configuration values must not contain line breaks.\n' >&2; exit 1; }
done
is_semantic_version "$PROJECT_VERSION" || { printf 'PROJECT_VERSION must be a valid Semantic Version 2.0.0 value.\n' >&2; exit 1; }

c_string_escape() {
  local value="$1"
  local LC_ALL=C
  local output="" character code escaped
  while [[ -n "$value" ]]; do
    character="${value:0:1}"
    value="${value:1}"
    case "$character" in
      \\) output="${output}\\\\" ;;
      \") output+='\"' ;;
      $'\t') output+='\t' ;;
      *)
        code="$(printf '%s' "$character" | od -An -tu1)"
        if ((code < 32 || code == 127)); then
          printf -v escaped '\\%03o' "$code"
          output+="$escaped"
        else
          output+="$character"
        fi
        ;;
    esac
  done
  printf '%s' "$output"
}

commit=unknown
dirty=false
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  commit="$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf unknown)"
  [[ -z "$(git_worktree_status "$ROOT")" ]] || dirty=true
fi

directory="$ROOT/Build/Generated/$PROJECT_NAMESPACE"
output="$directory/BuildInfo.generated.h"
mkdir -p "$directory"
temporary="$(mktemp "$output.XXXXXX.tmp")"
trap 'rm -f "$temporary"' EXIT
printf '#pragma once\n\n#define KEIRE_BUILD_PROJECT_VERSION "%s"\n#define KEIRE_BUILD_PROJECT_NAME "%s"\n#define KEIRE_BUILD_REPOSITORY_SLUG "%s"\n#define KEIRE_BUILD_GIT_COMMIT "%s"\n#define KEIRE_BUILD_GIT_DIRTY %s\n' \
  "$(c_string_escape "$PROJECT_VERSION")" "$(c_string_escape "$PROJECT_DISPLAY_NAME")" \
  "$(c_string_escape "$REPOSITORY_SLUG")" "$(c_string_escape "$commit")" "$dirty" > "$temporary"
if [[ ! -f "$output" ]] || ! cmp -s "$temporary" "$output"; then
  mv "$temporary" "$output"
else
  rm -f "$temporary"
fi
trap - EXIT
