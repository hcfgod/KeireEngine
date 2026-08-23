#!/usr/bin/env bash

generated_content_hash_file() {
  local path="${1:?path is required}"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
  else
    shasum -a 256 "$path" | awk '{print $1}'
  fi
}

generated_content_fingerprint() {
  local schema="${1:?schema is required}"
  shift
  local input
  {
    printf 'schema=%s\n' "$schema"
    for input in "$@"; do
      [[ -f "$input" ]] || {
        printf 'Generated-content input is missing: %s\n' "$input" >&2
        return 1
      }
      generated_content_hash_file "$input"
    done
  } | if command -v sha256sum >/dev/null 2>&1; then
    sha256sum | awk '{print $1}'
  else
    shasum -a 256 | awk '{print $1}'
  fi
}

generated_content_is_current() {
  local output="${1:?output is required}"
  local stamp="${2:?stamp is required}"
  local fingerprint="${3:?fingerprint is required}"
  [[ -f "$output" && -f "$stamp" && "$(tr -d '\r\n' < "$stamp")" == "keire-generated-v1|$fingerprint" ]]
}

generated_content_acquire_lock() {
  local lock="${1:?lock directory is required}"
  local timeout_seconds="${2:-600}"
  local wait_message="${3:-}"
  local deadline=$((SECONDS + timeout_seconds))
  local waiting=0
  mkdir -p "$(dirname "$lock")"
  while ! mkdir "$lock" 2>/dev/null; do
    if [[ $waiting -eq 0 && -n "$wait_message" ]]; then
      printf '%s\n' "$wait_message"
      waiting=1
    fi
    if [[ -f "$lock/pid" ]]; then
      local owner
      owner="$(tr -dc '0-9' < "$lock/pid")"
      if [[ -n "$owner" ]] && ! kill -0 "$owner" 2>/dev/null; then
        rm -f "$lock/pid"
        rmdir "$lock" 2>/dev/null || true
        continue
      fi
    fi
    if ((SECONDS >= deadline)); then
      printf 'Timed out waiting for generated content from another build: %s\n' "$lock" >&2
      return 1
    fi
    sleep 0.1
  done
  printf '%s\n' "$$" > "$lock/pid"
}

generated_content_release_lock() {
  local lock="${1:?lock directory is required}"
  rm -f "$lock/pid"
  rmdir "$lock" 2>/dev/null || true
}

generated_content_write_stamp() {
  local stamp="${1:?stamp is required}"
  local fingerprint="${2:?fingerprint is required}"
  local temporary="$stamp.tmp.$$.$RANDOM"
  mkdir -p "$(dirname "$stamp")"
  printf 'keire-generated-v1|%s\n' "$fingerprint" > "$temporary"
  mv "$temporary" "$stamp"
}

generated_content_copy_file_if_changed() {
  local source="${1:?source file is required}"
  local destination="${2:?destination file is required}"
  [[ -f "$source" ]] || {
    printf 'Copy source must be a file: %s\n' "$source" >&2
    return 1
  }
  if [[ -f "$destination" ]] && cmp -s "$source" "$destination"; then
    return 0
  fi

  mkdir -p "$(dirname "$destination")"
  local temporary="$destination.tmp.$$.$RANDOM"
  if ! cp -p "$source" "$temporary"; then
    rm -f "$temporary"
    return 1
  fi
  if ! mv -f "$temporary" "$destination"; then
    rm -f "$temporary"
    return 1
  fi
}
