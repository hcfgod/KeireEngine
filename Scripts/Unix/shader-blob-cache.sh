#!/usr/bin/env bash

# Requires generated-content-cache.sh. Generators retain their existing outer lock.
SHADER_BLOB_HELPER="${BASH_SOURCE[0]}"
shader_blob_collect_source() {
  local source_file="$1" include_directory="$2" list="$3"
  local include_name resolved
  [[ -f "$source_file" ]] || { printf 'Missing shader dependency: %s\n' "$source_file" >&2; return 1; }
  source_file="$(cd "$(dirname "$source_file")" && pwd -P)/$(basename "$source_file")"
  grep -Fxq -- "$source_file" "$list" && return 0
  printf '%s\n' "$source_file" >> "$list"
  # Conservative closure includes inactive branches too. Unsupported include syntax fails closed.
  while IFS= read -r include_name; do
    [[ -n "$include_name" ]] || continue
    if [[ -f "$(dirname "$source_file")/$include_name" ]]; then
      resolved="$(dirname "$source_file")/$include_name"
    elif [[ -f "$include_directory/$include_name" ]]; then
      resolved="$include_directory/$include_name"
    else
      printf 'Unresolved shader include %s in %s\n' "$include_name" "$source_file" >&2
      return 1
    fi
    shader_blob_collect_source "$resolved" "$include_directory" "$list" || return 1
  done < <(sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]+"([^"]+)".*$/\1/p' "$source_file")
  if grep -E '^[[:space:]]*#[[:space:]]*include[[:space:]]+' "$source_file" |
      grep -Ev '^[[:space:]]*#[[:space:]]*include[[:space:]]+"[^"]+"'; then
    printf 'Unsupported shader include syntax in %s\n' "$source_file" >&2
    return 1
  fi
}

shader_blob_collect_libraries() {
  local compiler="$1" list="$2" library
  printf '%s\n' "$compiler" >> "$list"
  for library in "$(dirname "$compiler")"/*.dll "$(dirname "$compiler")"/*.so \
      "$(dirname "$compiler")"/*.so.* "$(dirname "$compiler")"/*.dylib; do
    [[ ! -f "$library" ]] || printf '%s\n' "$library" >> "$list"
  done
}

shader_blob_global_inputs() {
  local compiler="$1" include_directory="$2" list="$3" source_file
  shift 3
  : > "$list"
  printf '%s\n' "$SHADER_BLOB_HELPER" >> "$list"
  shader_blob_collect_libraries "$compiler" "$list"
  for source_file in "$@"; do
    shader_blob_collect_source "$source_file" "$include_directory" "$list" || return 1
  done
}

shader_blob_source_identity() {
  local compiler="$1" source_file="$2" include_directory="$3" work="$4" input
  shader_blob_global_inputs "$compiler" "$include_directory" "$work/dependencies" "$source_file" || return 1
  : > "$work/identity"
  while IFS= read -r input; do
    printf '%s\n' "$input" >> "$work/identity"
    generated_content_hash_file "$input" >> "$work/identity" || return 1
  done < "$work/dependencies"
  generated_content_hash_file "$work/identity"
}

shader_blob_compile() {
  local cache_directory="$1" compiler="$2" source_file="$3" output="$4"
  local stage="$5" entry="$6" destination="$7" include_directory="$8"
  local identity="${9:-}" work key blob stamp digest
  mkdir -p "$cache_directory"
  work="$(mktemp -d "$cache_directory/.compile.XXXXXX")" || return 1
  local current_identity
  current_identity="$(shader_blob_source_identity "$compiler" "$source_file" "$include_directory" "$work")" || {
    rm -rf "$work"
    return 1
  }
  if [[ -n "$identity" && "$current_identity" != "$identity" ]]; then
    printf 'Shader inputs changed after preparation; refusing stale cache reuse.\n' >&2
    rm -rf "$work"
    return 1
  fi
  identity="$current_identity"
  # Fixed compiler switches plus their values form an unambiguous invocation identity. Output is not semantic.
  printf '%s\n' shader-blob-v1 "$source_file" -s HLSL -d "$destination" -t "$stage" \
    -e "$entry" -I "$include_directory" "$identity" > "$work/key"
  key="$(generated_content_hash_file "$work/key")" || { rm -rf "$work"; return 1; }
  blob="$cache_directory/$key.blob"
  stamp="$cache_directory/$key.sha256"
  if [[ -s "$blob" && -f "$stamp" ]] &&
      [[ "$(generated_content_hash_file "$blob")" == "$(cat "$stamp")" ]]; then
    local copy_result=0
    cp "$blob" "$output" || copy_result=$?
    rm -rf "$work"
    return "$copy_result"
  fi
  if ! "$compiler" "$source_file" -s HLSL -d "$destination" -t "$stage" -e "$entry" \
      -I "$include_directory" -o "$work/output"; then
    rm -rf "$work"
    return 1
  fi
  if [[ ! -s "$work/output" ]]; then
    printf 'Shader compiler produced no data for %s/%s.\n' "$entry" "$destination" >&2
    rm -rf "$work"
    return 1
  fi
  current_identity="$(shader_blob_source_identity "$compiler" "$source_file" "$include_directory" "$work")" || {
    rm -rf "$work"
    return 1
  }
  if [[ "$current_identity" != "$identity" ]]; then
    printf 'Shader inputs changed during compilation; refusing stale cache publication.\n' >&2
    rm -rf "$work"
    return 1
  fi
  digest="$(generated_content_hash_file "$work/output")" || { rm -rf "$work"; return 1; }
  printf '%s\n' "$digest" > "$work/stamp"
  local result=0
  mv -f "$work/output" "$blob" && mv -f "$work/stamp" "$stamp" && cp "$blob" "$output" || result=$?
  rm -rf "$work"
  return "$result"
}
