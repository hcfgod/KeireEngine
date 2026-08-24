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

generated_content_prepare_destination_directory() {
  local containment_root="${1:?containment root is required}"
  local destination_directory="${2:?destination directory is required}"
  local resolved_root resolved_destination relative component current
  local components=(sentinel)
  [[ -d "$containment_root" && ! -L "$containment_root" ]] || {
    printf 'Generated-content containment root must be a regular directory: %s\n' "$containment_root" >&2
    return 1
  }
  resolved_root="$(cd -P "$containment_root" && pwd -P)" || return 1
  case "$destination_directory" in
    "$containment_root") return 0 ;;
    "$containment_root"/*) relative="${destination_directory#"$containment_root"/}" ;;
    *)
      printf 'Generated-content destination escapes its containment root: %s\n' "$destination_directory" >&2
      return 1
      ;;
  esac
  IFS='/' read -r -a components <<< "sentinel/$relative"
  current="$containment_root"
  for component in "${components[@]:1}"; do
    [[ -n "$component" && "$component" != . && "$component" != .. ]] || {
      printf 'Generated-content destination contains an unsafe path component: %s\n' "$destination_directory" >&2
      return 1
    }
    current="$current/$component"
    [[ ! -L "$current" ]] || {
      printf 'Generated-content destination ancestor may not be a symbolic link: %s\n' "$current" >&2
      return 1
    }
    if [[ -e "$current" ]]; then
      [[ -d "$current" ]] || {
        printf 'Generated-content destination ancestor must be a directory: %s\n' "$current" >&2
        return 1
      }
    else
      mkdir "$current" 2>/dev/null || {
        [[ -d "$current" && ! -L "$current" ]] || return 1
      }
    fi
  done
  resolved_destination="$(cd -P "$destination_directory" && pwd -P)" || return 1
  case "$resolved_destination" in
    "$resolved_root"|"$resolved_root"/*) ;;
    *)
      printf 'Generated-content destination resolved outside its containment root: %s\n' \
        "$destination_directory" >&2
      return 1
      ;;
  esac
}

generated_content_read_pid() {
  local path="${1:?PID file is required}"
  # Keep the path as a file operand so a concurrent release is a suppressed command failure, not a shell error.
  # shellcheck disable=SC2002
  cat "$path" 2>/dev/null | tr -dc '0-9' || true
}

generated_content_recover_stale_gate() {
  local recovery="${1:?recovery gate is required}"
  local owner quarantine moved_owner
  [[ -d "$recovery" && ! -L "$recovery" ]] || return 1
  owner="$(generated_content_read_pid "$recovery/pid")"
  [[ -n "$owner" ]] && ! kill -0 "$owner" 2>/dev/null || return 1

  quarantine="$recovery.stale.$$.$RANDOM"
  mv "$recovery" "$quarantine" 2>/dev/null || return 1
  moved_owner="$(generated_content_read_pid "$quarantine/pid")"
  if [[ "$moved_owner" != "$owner" ]]; then
    if [[ ! -e "$recovery" && ! -L "$recovery" ]]; then
      mv "$quarantine" "$recovery" 2>/dev/null || true
    fi
    return 1
  fi
  rm -f "$quarantine/pid"
  rmdir "$quarantine" 2>/dev/null || {
    printf 'Generated-content recovery gate contained unexpected state: %s\n' "$recovery" >&2
    return 1
  }
}

generated_content_acquire_lock() {
  local lock="${1:?lock directory is required}"
  local timeout_seconds="${2:-600}"
  local wait_message="${3:-}"
  local deadline=$((SECONDS + timeout_seconds))
  local waiting=0
  local recovery="$lock.recovery"
  mkdir -p "$(dirname "$lock")"
  while true; do
    if [[ -d "$recovery" ]]; then
      if generated_content_recover_stale_gate "$recovery"; then
        continue
      fi
      if ((SECONDS >= deadline)); then
        printf 'Timed out waiting for generated-content lock recovery: %s\n' "$lock" >&2
        return 1
      fi
      sleep 0.1
      continue
    fi
    if mkdir "$lock" 2>/dev/null; then
      if ! printf '%s\n' "$$" > "$lock/pid"; then
        rmdir "$lock" 2>/dev/null || true
        return 1
      fi
      return 0
    fi
    if [[ $waiting -eq 0 && -n "$wait_message" ]]; then
      printf '%s\n' "$wait_message"
      waiting=1
    fi
    if [[ -f "$lock/pid" ]]; then
      local owner
      owner="$(generated_content_read_pid "$lock/pid")"
      if [[ -n "$owner" ]] && ! kill -0 "$owner" 2>/dev/null; then
        if mkdir "$recovery" 2>/dev/null; then
          local quarantine moved_owner current_owner recovery_status=0
          printf '%s\n' "$$" > "$recovery/pid"
          current_owner="$(generated_content_read_pid "$lock/pid")"
          if [[ "$current_owner" == "$owner" ]] && ! kill -0 "$current_owner" 2>/dev/null; then
            quarantine="$lock.stale.$$.$RANDOM"
            if mv "$lock" "$quarantine" 2>/dev/null; then
              moved_owner="$(generated_content_read_pid "$quarantine/pid")"
              if [[ "$moved_owner" != "$owner" ]]; then
                if [[ ! -e "$lock" && ! -L "$lock" ]]; then
                  mv "$quarantine" "$lock" 2>/dev/null || recovery_status=1
                else
                  recovery_status=1
                fi
              else
                rm -f "$quarantine/pid"
                rmdir "$quarantine" || recovery_status=1
              fi
            fi
          fi
          rm -f "$recovery/pid"
          rmdir "$recovery" 2>/dev/null || recovery_status=1
          if [[ $recovery_status -ne 0 ]]; then
            printf 'Generated-content lock recovery could not preserve ownership safely: %s\n' "$lock" >&2
            return 1
          fi
          continue
        fi
      fi
    fi
    if ((SECONDS >= deadline)); then
      printf 'Timed out waiting for generated content from another build: %s\n' "$lock" >&2
      return 1
    fi
    sleep 0.1
  done
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
  local containment_root="${3:?containment root is required}"
  local destination_directory temporary
  [[ -f "$source" ]] || {
    printf 'Copy source must be a file: %s\n' "$source" >&2
    return 1
  }
  destination_directory="$(dirname "$destination")"
  generated_content_prepare_destination_directory "$containment_root" "$destination_directory" || return 1
  [[ ! -L "$destination" ]] || {
    printf 'Copy destination may not be a symbolic link: %s\n' "$destination" >&2
    return 1
  }
  [[ ! -e "$destination" || -f "$destination" ]] || {
    printf 'Copy destination must be a regular file path: %s\n' "$destination" >&2
    return 1
  }
  if [[ -f "$destination" && ! -L "$destination" ]] && cmp -s "$source" "$destination"; then
    return 0
  fi

  temporary="$(mktemp "$destination_directory/.keire-copy.XXXXXX")" || return 1
  if ! cp -pL "$source" "$temporary"; then
    rm -f "$temporary"
    return 1
  fi
  [[ ! -L "$destination" ]] || {
    printf 'Copy destination became a symbolic link: %s\n' "$destination" >&2
    rm -f "$temporary"
    return 1
  }
  generated_content_prepare_destination_directory "$containment_root" "$destination_directory" || {
    rm -f "$temporary"
    return 1
  }
  if ! mv -f "$temporary" "$destination"; then
    rm -f "$temporary"
    return 1
  fi
}
