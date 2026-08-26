#!/usr/bin/env bash

workspace_lock_setting() {
    local name="${1:?setting name is required}" default="${2:?default is required}" minimum="${3:?minimum is required}"
    local value="${!name:-$default}"
    [[ "$value" =~ ^[0-9]+$ && "$value" -ge "$minimum" ]] || {
        printf '%s must be an integer greater than or equal to %s.\n' "$name" "$minimum" >&2
        return 1
    }
    printf '%s\n' "$value"
}

workspace_lock_mtime() {
    local path="${1:?path is required}"
    stat -c %Y "$path" 2>/dev/null || stat -f %m "$path"
}

workspace_lock_owner_value() {
    local lock="${1:?lock is required}" key="${2:?key is required}" line owner_contents
    owner_contents="$(cat "$lock/owner" 2>/dev/null || true)"
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        [[ "$line" == "$key="* ]] && { printf '%s\n' "${line#*=}"; return 0; }
    done <<< "$owner_contents"
    return 0
}

workspace_lock_remove_stale() {
    local lock="${1:?lock is required}" quarantine="${2:?quarantine is required}"
    mv "$lock" "$quarantine" || return 1
    rm -f "$quarantine/owner" "$quarantine/heartbeat"
    if ! rmdir "$quarantine"; then
        printf "The stale workspace lock contains unexpected files. Inspect and remove '%s' manually.\n" "$quarantine" >&2
        return 2
    fi
}

workspace_lock_prepare_parent() {
    local root="${1:?lock root is required}" parent="${2:?lock parent is required}"
    local relative current component
    [[ -d "$root" ]] || {
        printf 'Workspace lock root is not an existing directory: %s\n' "$root" >&2
        return 1
    }
    case "$parent" in
        "$root") return 0 ;;
        "$root"/*) relative="${parent#"$root"/}" ;;
        *) printf '%s\n' 'Lock path must remain inside its declared root.' >&2; return 1 ;;
    esac
    current="$root"
    while [[ -n "$relative" ]]; do
        component="${relative%%/*}"
        if [[ "$relative" == */* ]]; then relative="${relative#*/}"; else relative=""; fi
        current="$current/$component"
        if [[ -L "$current" || (-e "$current" && ! -d "$current") ]]; then
            printf 'Workspace lock parent contains a non-directory or symbolic-link component: %s\n' \
                "$current" >&2
            return 1
        fi
        if [[ ! -e "$current" ]] && ! mkdir "$current" 2>/dev/null && [[ ! -d "$current" ]]; then
            printf 'Could not create workspace lock parent: %s\n' "$current" >&2
            return 1
        fi
        if [[ -L "$current" || ! -d "$current" ]]; then
            printf 'Workspace lock parent contains a non-directory or symbolic-link component: %s\n' \
                "$current" >&2
            return 1
        fi
    done
}

workspace_lock_acquire() {
    local root="${1:?repository root is required}" command_name="${2:?command name is required}"
    local lock_relative="${3:-Tools/.locks/project-command.lock}"
    local timeout_seconds stale_seconds heartbeat_seconds lock_parent lock inherited_token existing_token
    local token deadline reported_wait=0 owner_host owner_platform owner_pid owner_command owner_started
    local heartbeat lease first_write second_write now quarantine safe_command safe_host owner_temporary
    timeout_seconds="$(workspace_lock_setting KEIRE_WORKSPACE_LOCK_TIMEOUT_SECONDS 7200 1)" || return 1
    stale_seconds="$(workspace_lock_setting KEIRE_WORKSPACE_LOCK_STALE_SECONDS 300 10)" || return 1
    heartbeat_seconds="$(workspace_lock_setting KEIRE_WORKSPACE_LOCK_HEARTBEAT_SECONDS 5 1)" || return 1
    ((heartbeat_seconds * 3 < stale_seconds)) || {
        printf '%s\n' 'KEIRE_WORKSPACE_LOCK_STALE_SECONDS must be more than three heartbeat intervals.' >&2
        return 1
    }

    [[ "$lock_relative" =~ ^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$ &&
       "/$lock_relative/" != *'/../'* && "/$lock_relative/" != *'/./'* ]] || {
        printf '%s\n' 'Lock path must remain inside its declared root.' >&2
        return 1
    }
    lock="$root/$lock_relative"
    case "$lock/" in
        "$root"/*) ;;
        *) printf '%s\n' 'Lock path must remain inside its declared root.' >&2; return 1 ;;
    esac
    lock_parent="$(dirname "$lock")"
    workspace_lock_prepare_parent "$root" "$lock_parent" || return 1
    inherited_token="${KEIRE_WORKSPACE_LOCK_TOKEN:-}"
    [[ ! -L "$lock" ]] || {
        printf "Workspace lock path must not be a symbolic link: '%s'.\n" "$lock" >&2
        return 1
    }
    if [[ -d "$lock" ]]; then
        existing_token="$(workspace_lock_owner_value "$lock" token)"
        if [[ -n "$inherited_token" && "$existing_token" == "$inherited_token" ]]; then
            KEIRE_WORKSPACE_LOCK_OWNED=0
            KEIRE_WORKSPACE_LOCK_PATH="$lock"
            return 0
        fi
    fi

    token="unix-$$-$RANDOM-$RANDOM"
    deadline=$(($(date +%s) + timeout_seconds))
    while ! mkdir "$lock" 2>/dev/null; do
        [[ -d "$lock" && ! -L "$lock" ]] || {
            printf "Workspace lock path is not a directory: '%s'. Remove it manually after confirming no project command is running.\n" "$lock" >&2
            return 1
        }
        if [[ $reported_wait -eq 0 ]]; then
            owner_host="$(workspace_lock_owner_value "$lock" host)"
            owner_platform="$(workspace_lock_owner_value "$lock" platform)"
            owner_pid="$(workspace_lock_owner_value "$lock" pid)"
            owner_command="$(workspace_lock_owner_value "$lock" command)"
            owner_started="$(workspace_lock_owner_value "$lock" started)"
            printf '==> Waiting for another Kéire project command (host=%s, platform=%s, pid=%s, command=%s, started=%s).\n' \
                "$owner_host" "$owner_platform" "$owner_pid" "$owner_command" "$owner_started"
            printf '    Shared workspace lock: %s\n' "$lock"
            reported_wait=1
        fi
        heartbeat="$lock/heartbeat"
        [[ -f "$heartbeat" ]] && lease="$heartbeat" || lease="$lock"
        first_write="$(workspace_lock_mtime "$lease")" || return 1
        now="$(date +%s)"
        if ((now - first_write >= stale_seconds)); then
            sleep 0.25
            if [[ -e "$lease" ]]; then
                second_write="$(workspace_lock_mtime "$lease")" || return 1
                now="$(date +%s)"
                if [[ "$second_write" == "$first_write" && $((now - second_write)) -ge $stale_seconds ]]; then
                    quarantine="$lock.stale.$token"
                    if workspace_lock_remove_stale "$lock" "$quarantine"; then
                        printf '%s\n' '==> Recovered expired Kéire workspace lock.'
                        continue
                    elif [[ $? -eq 2 ]]; then
                        return 1
                    fi
                fi
            fi
        fi
        if (($(date +%s) >= deadline)); then
            printf "Timed out after %s seconds waiting for '%s'. Confirm the reported owner is no longer running before removing the lock.\n" \
                "$timeout_seconds" "$lock" >&2
            return 1
        fi
        sleep 0.25
    done

    safe_command="$(printf '%s' "$command_name" | tr '\r\n=' '___')"
    safe_host="$(hostname 2>/dev/null | tr '\r\n=' '___')"
    owner_temporary="$lock/owner.tmp.$token"
    if ! printf 'token=%s\nplatform=unix\npid=%s\nhost=%s\ncommand=%s\nstarted=%s\n' \
        "$token" "$$" "$safe_host" "$safe_command" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$owner_temporary" || \
       ! mv "$owner_temporary" "$lock/owner" || ! : > "$lock/heartbeat"; then
        rm -f "$owner_temporary" "$lock/owner" "$lock/heartbeat"
        rmdir "$lock" 2>/dev/null || true
        return 1
    fi
    KEIRE_WORKSPACE_LOCK_PREVIOUS_TOKEN="$inherited_token"
    KEIRE_WORKSPACE_LOCK_TOKEN="$token"
    export KEIRE_WORKSPACE_LOCK_TOKEN
    KEIRE_WORKSPACE_LOCK_PATH="$lock"
    KEIRE_WORKSPACE_LOCK_OWNED=1
    (
        while true; do
            sleep "$heartbeat_seconds"
            [[ -f "$lock/owner" ]] || break
            IFS= read -r existing_token < "$lock/owner" || break
            [[ "$existing_token" == "token=$token" ]] || break
            touch "$lock/heartbeat" || break
        done
    ) &
    KEIRE_WORKSPACE_LOCK_HEARTBEAT_PID=$!
}

workspace_lock_release() {
    [[ "${KEIRE_WORKSPACE_LOCK_OWNED:-0}" -eq 1 ]] || return 0
    kill "${KEIRE_WORKSPACE_LOCK_HEARTBEAT_PID:-0}" 2>/dev/null || true
    wait "${KEIRE_WORKSPACE_LOCK_HEARTBEAT_PID:-0}" 2>/dev/null || true
    local existing_token=""
    existing_token="$(workspace_lock_owner_value "$KEIRE_WORKSPACE_LOCK_PATH" token)"
    if [[ "$existing_token" == "$KEIRE_WORKSPACE_LOCK_TOKEN" ]]; then
        rm -f "$KEIRE_WORKSPACE_LOCK_PATH/heartbeat" "$KEIRE_WORKSPACE_LOCK_PATH/owner"
        rmdir "$KEIRE_WORKSPACE_LOCK_PATH" 2>/dev/null || true
    fi
    if [[ -n "${KEIRE_WORKSPACE_LOCK_PREVIOUS_TOKEN:-}" ]]; then
        KEIRE_WORKSPACE_LOCK_TOKEN="$KEIRE_WORKSPACE_LOCK_PREVIOUS_TOKEN"
        export KEIRE_WORKSPACE_LOCK_TOKEN
    else
        unset KEIRE_WORKSPACE_LOCK_TOKEN
    fi
    KEIRE_WORKSPACE_LOCK_OWNED=0
}

locked_git_source_validate() {
    local path="${1:?source path is required}" expected_commit="${2:?expected commit is required}"
    local name="${3:?dependency name is required}" actual changes
    if [[ ! -d "$path" || -L "$path" || ! -d "$path/.git" || -L "$path/.git" ]]; then
        printf 'Locked %s source cache is not an ordinary Git checkout: %s\n' "$name" "$path" >&2
        return 1
    fi
    actual="$(git -C "$path" rev-parse HEAD 2>/dev/null || true)"
    [[ "$actual" == "$expected_commit" ]] || {
        printf 'Locked %s source cache is not the expected commit: %s\n' "$name" "$path" >&2
        return 1
    }
    if ! changes="$(git -C "$path" status --porcelain=v1 --untracked-files=all 2>/dev/null)"; then
        printf 'Could not validate the locked %s source cache: %s\n' "$name" "$path" >&2
        return 1
    fi
    [[ -z "$changes" ]] || {
        printf 'Locked %s source cache contains modified or untracked files: %s\n' "$name" "$path" >&2
        return 1
    }
}

workspace_identity() {
    local root="${1:?workspace root is required}" canonical descriptor digest
    [[ -d "$root" ]] || { printf 'Workspace root is not an existing directory: %s\n' "$root" >&2; return 1; }
    canonical="$(cd -P "$root" && pwd -P)" || return 1
    [[ -n "$canonical" && "$canonical" != / ]] || {
        printf '%s\n' 'Workspace root must resolve to a non-root directory.' >&2
        return 1
    }
    descriptor="$canonical"
    if command -v sha256sum >/dev/null 2>&1; then
        digest="$(printf '%s' "$descriptor" | sha256sum | awk '{print $1}')" || return 1
    elif command -v shasum >/dev/null 2>&1; then
        digest="$(printf '%s' "$descriptor" | shasum -a 256 | awk '{print $1}')" || return 1
    else
        printf '%s\n' 'Workspace identity requires sha256sum or shasum.' >&2
        return 1
    fi
    [[ "$digest" =~ ^[0-9a-fA-F]{64}$ ]] || return 1
    printf '%s\n' "${digest:0:16}" | LC_ALL=C tr '[:upper:]' '[:lower:]'
}

coral_build_variant_key() {
    local platform="${1:?platform is required}" architecture="${2:?target architecture is required}"
    local toolset="${3:?toolset is required}" compiler_identity="${4:?compiler identity is required}"
    local dotnet_sdk_version="${5:?.NET SDK version is required}" dotnet_root="${6:?.NET SDK root is required}"
    local deployment_target="${7:-none}" nethost_identity="${8:?nethost identity is required}"
    local workspace_key="${9:?workspace identity is required}"
    local system normalized_architecture output_architecture descriptor digest
    case "$platform" in
        Linux) system=linux ;;
        Mac) system=macosx ;;
        *) printf "Unsupported Coral platform '%s'. Expected Linux or Mac.\n" "$platform" >&2; return 1 ;;
    esac
    normalized_architecture="$(normalize_architecture "$architecture")" || return 1
    output_architecture="$(architecture_output_name "$normalized_architecture")" || return 1
    [[ "$toolset" == gcc || "$toolset" == clang ]] || {
        printf "Unsupported Coral toolset '%s'. Expected gcc or clang.\n" "$toolset" >&2
        return 1
    }
    [[ -n "$compiler_identity" && "$compiler_identity" != *$'\n'* && "$compiler_identity" != *$'\r'* ]] || {
        printf '%s\n' 'Coral compiler identity must be one non-empty line.' >&2
        return 1
    }
    [[ "$dotnet_sdk_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
        printf '%s\n' 'Coral .NET SDK identity must be an exact numeric SDK version.' >&2
        return 1
    }
    [[ "$dotnet_root" == /* && "$dotnet_root" != *$'\n'* && "$dotnet_root" != *$'\r'* ]] || {
        printf '%s\n' 'Coral .NET SDK root must be one non-empty absolute path.' >&2
        return 1
    }
    [[ -n "$nethost_identity" && "$nethost_identity" != *$'\n'* && "$nethost_identity" != *$'\r'* ]] || {
        printf '%s\n' 'Coral nethost identity must be one non-empty line.' >&2
        return 1
    }
    [[ "$workspace_key" =~ ^[0-9a-f]{16}$ ]] || {
        printf '%s\n' 'Coral workspace identity must be a 16-character lowercase SHA-256 prefix.' >&2
        return 1
    }
    if [[ "$platform" == Mac ]]; then
        [[ "$deployment_target" =~ ^[0-9]+\.[0-9]+$ ]] || {
            printf '%s\n' 'Coral macOS deployment target must be a pinned major.minor version.' >&2
            return 1
        }
    else
        deployment_target=none
    fi
    descriptor="coral-native-unix-v3
$system
$normalized_architecture
$toolset
$compiler_identity
$dotnet_sdk_version
$dotnet_root
$deployment_target
$nethost_identity
$workspace_key"
    if command -v sha256sum >/dev/null 2>&1; then
        digest="$(printf '%s' "$descriptor" | sha256sum | awk '{print $1}')" || return 1
    elif command -v shasum >/dev/null 2>&1; then
        digest="$(printf '%s' "$descriptor" | shasum -a 256 | awk '{print $1}')" || return 1
    else
        printf '%s\n' 'Coral cache identity requires sha256sum or shasum.' >&2
        return 1
    fi
    [[ "$digest" =~ ^[0-9a-fA-F]{64}$ ]] || return 1
    printf '%s-%s-%s\n' "$system" "$output_architecture" "${digest:0:24}"
}

config_value() {
    local file="$1" key="$2" line prefix
    prefix="$key="
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        if [[ "$line" == "$prefix"* ]]; then
            printf '%s\n' "${line#"$prefix"}"
            return 0
        fi
    done < "$file"
}

tool_version_from_temporary_directory() {
    local executable="$1" temporary_directory output="" status
    shift
    temporary_directory="$(mktemp -d)"
    if output="$(cd "$temporary_directory" && "$executable" "$@")"; then
        status=0
    else
        status=$?
    fi
    rm -rf "$temporary_directory"
    printf '%s\n' "$output"
    return "$status"
}

probe_cxx20_thread_library() {
    local compiler="${1:?C++ compiler is required}"
    local temporary_directory source executable compiler_output runtime_output status
    temporary_directory="$(mktemp -d)" || return 1
    source="$temporary_directory/keire-cxx20-thread-probe.cpp"
    executable="$temporary_directory/keire-cxx20-thread-probe"
    compiler_output="$temporary_directory/compiler-output.txt"
    runtime_output="$temporary_directory/runtime-output.txt"
    printf '%s\n' \
      '#include <atomic>' \
      '#include <stop_token>' \
      '#include <thread>' \
      'int main()' \
      '{' \
      '    std::atomic<bool> observed = false;' \
      '    std::jthread worker([&observed](std::stop_token token) { observed.store(token.stop_possible()); });' \
      '    worker.request_stop();' \
      '    worker.join();' \
      '    return observed.load() ? 0 : 1;' \
      '}' > "$source"

    if "$compiler" -std=c++20 -pthread "$source" -o "$executable" >"$compiler_output" 2>&1; then
        :
    else
        status=$?
        printf '%s\n' \
          'The selected macOS C++ toolchain cannot compile the C++20 library features required by Kéire: std::stop_token and std::jthread.' \
          'Install a newer full Xcode or Command Line Tools release whose production libc++ provides both features, select it with xcode-select, then rerun bootstrap.' \
          'After installing full Xcode, select it with: sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer' \
          'Do not enable _LIBCPP_ENABLE_EXPERIMENTAL: Kéire requires the supported production libc++ ABI.' >&2
        [[ ! -s "$compiler_output" ]] || { printf '%s\n' 'Compiler diagnostic:' >&2; cat "$compiler_output" >&2; }
        rm -rf "$temporary_directory"
        return "$status"
    fi
    if "$executable" >"$runtime_output" 2>&1; then
        :
    else
        status=$?
        printf '%s\n' \
          'The selected macOS C++ toolchain compiled but could not run the Kéire C++20 std::stop_token/std::jthread probe.' \
          'Install and select a native full Xcode or Command Line Tools release, then rerun bootstrap.' >&2
        [[ ! -s "$runtime_output" ]] || { printf '%s\n' 'Probe diagnostic:' >&2; cat "$runtime_output" >&2; }
        rm -rf "$temporary_directory"
        return "$status"
    fi
    rm -rf "$temporary_directory"
}

run_homebrew_installer() {
    local ci="${1:?CI mode is required}"
    local installer="${2:?Homebrew installer path is required}"
    [[ "$ci" == 0 || "$ci" == 1 ]] || {
        printf 'Homebrew installer CI mode must be 0 or 1.\n' >&2
        return 2
    }
    [[ -f "$installer" && ! -L "$installer" ]] || {
        printf 'Homebrew installer is not a regular local file: %s.\n' "$installer" >&2
        return 1
    }
    if [[ "$ci" == 1 ]]; then
        NONINTERACTIVE=1 /bin/bash "$installer"
        return
    fi
    [[ -t 0 ]] || {
        printf '%s\n' \
            'Homebrew is missing and local bootstrap cannot request authorization without an interactive terminal.' \
            'Rerun from a terminal, or pass --ci only for an intentionally unattended environment.' >&2
        return 1
    }
    (
        unset NONINTERACTIVE
        /bin/bash "$installer"
    )
}

dotnet_sdk_listing_matches_installation() {
    local listing="${1-}"
    local version="${2:?SDK version is required}"
    local install_root="${3:?SDK installation root is required}"
    local expected_sdk line reported_version reported_sdk resolved_reported_sdk
    [[ -d "$install_root" && ! -L "$install_root" && -d "$install_root/sdk" && ! -L "$install_root/sdk" ]] || return 1
    expected_sdk="$(cd -P "$install_root/sdk" && pwd -P)" || return 1
    while IFS= read -r line; do
        [[ "$line" =~ ^([^[:space:]]+)[[:space:]]+\[([^][]+)\][[:space:]]*$ ]] || continue
        reported_version="${BASH_REMATCH[1]}"
        reported_sdk="${BASH_REMATCH[2]}"
        [[ "$reported_version" == "$version" && -d "$reported_sdk" ]] || continue
        resolved_reported_sdk="$(cd -P "$reported_sdk" && pwd -P)" || continue
        [[ "$resolved_reported_sdk" == "$expected_sdk" ]] && return 0
    done <<< "$listing"
    return 1
}

pinned_dotnet_sdk_root() {
    local executable="${1:?dotnet executable is required}"
    local version="${2:?SDK version is required}"
    local listing line reported_version reported_sdk install_root="" direct_executable direct_listing selected_version
    [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 "$executable" --list-sdks)" || return 1
    while IFS= read -r line; do
        [[ "$line" =~ ^([^[:space:]]+)[[:space:]]+\[([^][]+)\][[:space:]]*$ ]] || continue
        reported_version="${BASH_REMATCH[1]}"
        reported_sdk="${BASH_REMATCH[2]}"
        [[ "$reported_version" == "$version" && -d "$reported_sdk" ]] || continue
        install_root="$(cd -P "$reported_sdk/.." && pwd -P)" || continue
        dotnet_sdk_listing_matches_installation "$listing" "$version" "$install_root" || {
            install_root=""
            continue
        }
        break
    done <<< "$listing"
    [[ -n "$install_root" ]] || return 1

    direct_executable="$install_root/dotnet"
    [[ -x "$direct_executable" && ! -L "$direct_executable" ]] || return 1
    direct_listing="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_ROOT="$install_root" \
        "$direct_executable" --list-sdks)" || return 1
    dotnet_sdk_listing_matches_installation "$direct_listing" "$version" "$install_root" || return 1
    selected_version="$(DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_ROOT="$install_root" \
        "$direct_executable" --version)" || return 1
    selected_version="${selected_version//$'\r'/}"
    [[ "$selected_version" == "$version" ]] || return 1
    printf '%s\n' "$install_root"
}

dotnet_apphost_pack_metadata() {
    local bundled_versions="${1:?bundled versions file is required}"
    local target_framework="${2:?target framework is required}"
    [[ -f "$bundled_versions" && ! -L "$bundled_versions" ]] || {
        printf 'The pinned .NET SDK host-pack manifest is not an ordinary file: %s\n' \
            "$bundled_versions" >&2
        return 1
    }
    [[ "$target_framework" =~ ^net[0-9]+\.0$ ]] || {
        printf 'Invalid .NET target framework for host-pack discovery: %s\n' "$target_framework" >&2
        return 1
    }
    command -v python3 >/dev/null 2>&1 || {
        printf '%s\n' 'Pinned .NET host-pack discovery requires Python 3.' >&2
        return 1
    }

    python3 - "$bundled_versions" "$target_framework" <<'PY'
import sys
import xml.etree.ElementTree as ET


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


manifest, target_framework = sys.argv[1:]
try:
    root = ET.parse(manifest).getroot()
except (OSError, ET.ParseError) as error:
    print(f"Could not parse the pinned .NET SDK host-pack manifest: {error}", file=sys.stderr)
    raise SystemExit(1)

matches = [
    element
    for element in root.iter()
    if local_name(element.tag) == "KnownAppHostPack"
    and element.get("Include") == "Microsoft.NETCore.App"
    and element.get("TargetFramework") == target_framework
]
if len(matches) != 1:
    print(
        f"The pinned .NET SDK must describe exactly one {target_framework} "
        "Microsoft.NETCore.App host pack.",
        file=sys.stderr,
    )
    raise SystemExit(1)

pack = matches[0]
version = pack.get("AppHostPackVersion", "")
runtime_identifiers = pack.get("AppHostRuntimeIdentifiers", "")
if any(character in version or character in runtime_identifiers for character in "\r\n"):
    print("The pinned .NET SDK host-pack metadata contains an unexpected line break.", file=sys.stderr)
    raise SystemExit(1)
print(version)
print(runtime_identifiers)
PY
}

dotnet_sdk_root() {
    local executable="$1" major="$2" listing line sdk_directory=""
    listing="$("$executable" --list-sdks)" || {
        printf 'Unable to query installed .NET SDKs with %s.\n' "$executable" >&2
        return 1
    }
    while IFS= read -r line; do
        [[ "$line" == "$major."* ]] || continue
        if [[ "$line" =~ \[([^][]+)\][[:space:]]*$ ]]; then
            sdk_directory="${BASH_REMATCH[1]}"
        fi
    done <<< "$listing"
    [[ -n "$sdk_directory" && -d "$sdk_directory" ]] || {
        printf 'A valid .NET %s SDK installation was not found.\n' "$major" >&2
        return 1
    }
    dirname "$sdk_directory"
}

load_project_config() {
    local root="$1" file="$1/Config/Project.conf"
    local line key required seen_keys=$'\n'
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        [[ "$line" != *$'\r'* ]] || { printf 'Configuration contains an embedded carriage return: %s\n' "$file" >&2; return 1; }
        [[ -z "$line" ]] && continue
        [[ "$line" =~ ^([A-Z0-9_]+)=(.*)$ ]] || { printf 'Malformed configuration line in %s: %s\n' "$file" "$line" >&2; return 1; }
        key="${BASH_REMATCH[1]}"
        [[ "$seen_keys" != *$'\n'"$key"$'\n'* ]] || { printf "Duplicate key '%s' in %s.\n" "$key" "$file" >&2; return 1; }
        seen_keys+="$key"$'\n'
    done < "$file"
    for required in PROJECT_IDENTIFIER PROJECT_DISPLAY_NAME PROJECT_VERSION PROJECT_NAMESPACE PROJECT_MACRO_PREFIX CORE_TARGET CORE_DIRECTORY CLIENT_TARGET CLIENT_DIRECTORY HUB_TARGET HUB_DIRECTORY TESTS_TARGET TESTS_DIRECTORY ARTIFACT_PREFIX REPOSITORY_SLUG; do
        [[ "$seen_keys" == *$'\n'"$required"$'\n'* ]] || { printf "Project configuration is missing '%s'.\n" "$required" >&2; return 1; }
    done
    PROJECT_IDENTIFIER="$(config_value "$file" PROJECT_IDENTIFIER)"
    PROJECT_DISPLAY_NAME="$(config_value "$file" PROJECT_DISPLAY_NAME)"
    PROJECT_VERSION="$(config_value "$file" PROJECT_VERSION)"
    PROJECT_NAMESPACE="$(config_value "$file" PROJECT_NAMESPACE)"
    PROJECT_MACRO_PREFIX="$(config_value "$file" PROJECT_MACRO_PREFIX)"
    CORE_TARGET="$(config_value "$file" CORE_TARGET)"
    CORE_DIRECTORY="$(config_value "$file" CORE_DIRECTORY)"
    CLIENT_TARGET="$(config_value "$file" CLIENT_TARGET)"
    CLIENT_DIRECTORY="$(config_value "$file" CLIENT_DIRECTORY)"
    HUB_TARGET="$(config_value "$file" HUB_TARGET)"
    HUB_DIRECTORY="$(config_value "$file" HUB_DIRECTORY)"
    TESTS_TARGET="$(config_value "$file" TESTS_TARGET)"
    TESTS_DIRECTORY="$(config_value "$file" TESTS_DIRECTORY)"
    ARTIFACT_PREFIX="$(config_value "$file" ARTIFACT_PREFIX)"
    REPOSITORY_SLUG="$(config_value "$file" REPOSITORY_SLUG)"
    is_semantic_version "$PROJECT_VERSION" || { printf 'PROJECT_VERSION must be a valid Semantic Version 2.0.0 value.\n' >&2; return 1; }
}

project_generation_premake_inputs() {
    local root="$1"
    find "$root" \
      \( -type d \( \
        -path "$root/Vendor" -o -path "$root/Tools" \
        -o -name '.git' -o -name '.vs' -o -name '.ruff_cache' -o -name '.codex-remote-attachments' \
        -o -name 'Artifacts' -o -name 'Build' -o -name 'Library' -o -name 'Logs' \
        -o -name 'bin' -o -name 'obj' -o -name 'node_modules' -o -name '__pycache__' \
      \) -prune \) -o -type f -name 'premake5.lua' -print
}

project_generation_fingerprint() {
    local root="$1" source_root
    {
        for source_root in "$CORE_DIRECTORY" "$CLIENT_DIRECTORY" "$HUB_DIRECTORY" "$TESTS_DIRECTORY" \
          AssetTool KeireAssetWorker KeireEditorTests KeireHubRuntime KeireHubTests KeireHubWorker KeireInstallWorker \
          KeireRenderTests KeireRuntime KeireManaged KeireManaged.Tests SourceModules Scripts/Premake; do
            [[ -d "$root/$source_root" ]] || continue
            find "$root/$source_root" -type f \( \
              -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
              -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.lua' \
              -o -name '*.cs' -o -name '*.csproj' \
            \) -not -path '*/bin/*' -not -path '*/obj/*' -print
        done | LC_ALL=C sort
        project_generation_premake_inputs "$root" | LC_ALL=C sort | while IFS= read -r source_root; do
            cksum "$source_root"
        done
        find "$root/Scripts/Premake" -type f -name '*.lua' -print | LC_ALL=C sort | while IFS= read -r source_root; do
            cksum "$source_root"
        done
        for source_root in \
          Scripts/Unix/common.sh Scripts/Windows/common.ps1 \
          Scripts/patch-ninja-depfiles.py Scripts/patch-ninja-compiler-cache.py \
          Scripts/Linux/bootstrap.sh Scripts/Linux/generate.sh Scripts/Mac/bootstrap.sh Scripts/Mac/generate.sh \
          Scripts/Windows/bootstrap.ps1 Scripts/Windows/generate.ps1 \
          Scripts/Unix/dependencies.sh Scripts/Windows/dependencies.ps1 \
          Scripts/Unix/shader-compiler.sh Scripts/Windows/shader-compiler.ps1 \
          Scripts/Unix/coral.sh Scripts/Windows/coral.ps1 Scripts/Unix/ffmpeg.sh Scripts/Windows/ffmpeg.ps1 \
          Scripts/Unix/vendor.sh Scripts/Linux/vendor.sh Scripts/Mac/vendor.sh Scripts/Windows/vendor.ps1; do
            [[ -f "$root/$source_root" ]] && cksum "$root/$source_root"
        done
        find "$root/Scripts/Dependencies" -type f -print | LC_ALL=C sort | while IFS= read -r source_root; do
            cksum "$source_root"
        done
        cksum "$root/Config/Project.conf" "$root/Config/Dependencies.lock"
    } | cksum | awk '{ print $1 "-" $2 }'
}

is_semantic_version() {
    [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-((0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(\+([0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*))?$ ]]
}

git_worktree_status() {
    local root="$1" kernel windows_root windows_status attempt
    kernel="$(uname -r 2>/dev/null || true)"
    if [[ "$kernel" == *[Mm]icrosoft* && "$root" == /mnt/[A-Za-z]/* ]] &&
        command -v git.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
        windows_root="$(wslpath -m "$root")" || return 1
        for attempt in 1 2 3 4 5; do
            if windows_status="$(git.exe -C "$windows_root" status --porcelain --untracked-files=normal 2>/dev/null)"; then
                [[ -z "$windows_status" ]] || printf '%s\n' "${windows_status//$'\r'/}"
                return
            fi
            sleep 0.2
        done
        return 1
    fi
    git -C "$root" status --porcelain --untracked-files=normal
}

package_worktree_policy() {
    local root="$1" allow_dirty="${2:-0}" ci="${3:-0}" status dirty=false
    [[ "$ci" == 0 || "$allow_dirty" == 0 ]] || { printf '%s\n' '--allow-dirty cannot be used in CI.' >&2; return 1; }
    git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1 || { printf '%s\n' 'Release packaging requires a Git working tree.' >&2; return 1; }
    status="$(git_worktree_status "$root")" || { printf 'Unable to inspect the package worktree at %s.\n' "$root" >&2; return 1; }
    [[ -z "$status" ]] || dirty=true
    if [[ "$dirty" != false && "$allow_dirty" != 1 ]]; then
        printf '%s\n' 'Release packaging requires a clean worktree. Use --allow-dirty only for a local development artifact.' >&2
        printf 'Reported worktree status: %q\n' "$status" >&2
        return 1
    fi
    printf '%s %s\n' "$dirty" "$([[ "$dirty" == true && "$allow_dirty" == 1 ]] && printf true || printf false)"
}

json_escape() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    value="${value//$'\n'/\\n}"
    value="${value//$'\r'/\\r}"
    value="${value//$'\t'/\\t}"
    printf '%s' "$value"
}

identifier_to_macro_prefix() {
    printf '%s' "$1" | sed -E 's/([A-Z]+)([A-Z][a-z])/\1_\2/g; s/([a-z0-9])([A-Z])/\1_\2/g' | tr '[:lower:]' '[:upper:]'
}

copy_tracked_tree() {
    local repository_root="$1" relative_source="$2" destination="$3" tracked_file relative_path
    git -C "$repository_root" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
        printf 'Tracked package copies require a Git working tree: %s\n' "$repository_root" >&2
        return 1
    }

    mkdir -p "$destination"
    local copied=0
    while IFS= read -r -d '' tracked_file; do
        [[ "$tracked_file" == "$relative_source/"* ]] || {
            printf "Tracked package path escaped '%s': %s\n" "$relative_source" "$tracked_file" >&2
            return 1
        }
        [[ -f "$repository_root/$tracked_file" ]] || continue
        relative_path="${tracked_file#"$relative_source/"}"
        mkdir -p "$destination/$(dirname "$relative_path")"
        cp "$repository_root/$tracked_file" "$destination/$relative_path"
        copied=1
    done < <(git -c core.quotepath=false -C "$repository_root" ls-files -z -- "$relative_source")
    [[ "$copied" == 1 ]] || { printf "No tracked files were found for package source '%s'.\n" "$relative_source" >&2; return 1; }
}

is_generated_package_path() {
    local path="/${1//\\//}/" name
    path="$(printf '%s' "$path" | sed -E 's#^/include/[^/]+/Build/#/include/PublicApi/#')"
    path="$(printf '%s' "$path" | sed -E 's#^/bin/Managed/Dotnet/#/bundled-dotnet/#; s#/bundled-dotnet/(.*)/Build/#/bundled-dotnet/\1/DotnetBuild/#')"
    [[ "$path" =~ /(Library|Logs|Build|Temp|SceneRecovery|Recovery)/ ]] && return 0
    name="${path%/}"; name="${name##*/}"
    [[ "$name" =~ (^|[._-])[Rr][Ee][Cc][Oo][Vv][Ee][Rr][Yy]([._-]|$) || "$name" =~ \.[Tt][Mm][Pp]$ ]]
}

assert_package_generated_data_free() {
    local stage="$1" entry relative
    while IFS= read -r -d '' entry; do
        relative="${entry#"$stage/"}"
        if is_generated_package_path "$relative"; then
            printf 'Package contains generated workspace data: %s\n' "$relative" >&2
            return 1
        fi
    done < <(find "$stage" -mindepth 1 -print0)
}

assert_package_archive_generated_data_free() {
    local archive="$1" entry
    while IFS= read -r entry; do
        entry="${entry#./}"
        if is_generated_package_path "$entry"; then
            printf 'Package archive contains generated workspace data: %s\n' "$entry" >&2
            return 1
        fi
    done < <(tar -tzf "$archive")
}

validate_package_stage() {
    local stage="$1" client="$2" hub="$3" core="$4" namespace="$5" path
    local required=("bin/$client" "bin/$hub" "bin/Managed/Coral.Managed.dll" "bin/Managed/Keire.Managed.dll" "lib/lib$core.a" "lib/lib${namespace}ImGui.a" "Config/Client.json" "Config/SourceModules.premake.lua" "include/$namespace/Core.h" "include/$namespace/Log.h" "include/$namespace/Api.h" "include/$namespace/Application.h" "include/$namespace/Assert.h" "include/$namespace/BuildInfo.h" "include/$namespace/EntryPoint.h" "include/$namespace/Event.h" "include/$namespace/Layer.h" "include/$namespace/Ref.h" "include/$namespace/Time.h" "include/$namespace/Undo.h" "include/$namespace/Project/Project.h" "include/$namespace/Scenes/Scene.h" "include/$namespace/Scenes/SceneAsset.h" "include/$namespace/Scenes/SceneSystem.h" "include/$namespace/Ui.h" "include/$namespace/UiWorkspace.h" "include/$namespace/Window.h" "include/$namespace/WindowConfig.h" "samples/KeireSandbox/ProjectSettings/Project.keireproject" "samples/KeireSandbox/ProjectSettings/Rendering.keiresettings" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput" "samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene" "examples/consumer/Source/Main.cpp" "examples/consumer/Client.json" "examples/consumer/CMakeLists.txt" "examples/consumer/README.md" "examples/managed-consumer/Source/ClientApplication.cpp" "examples/managed-consumer/CMakeLists.txt" "examples/managed-consumer/ManagedApiConsumer.csproj" "examples/managed-consumer/ManagedPresentationAssets.cs" "examples/managed-consumer/README.md" "examples/source-module/Source/ClientApplication.cpp" "examples/source-module/Source/GameplayModule.cpp" "examples/source-module/Include/GameplayModule.h" "examples/source-module/CMakeLists.txt" "examples/source-module/README.md" "Docs/PlayerBuilds.md" "Docs/Diagnostics/KEIRE-AUDIO-0001.md" "Docs/Diagnostics/KEIRE-REPLAY-0001.md" "Docs/Diagnostics/KEIRE-REPLAY-0002.md" "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst" "third-party/licenses/doctest-LICENSE.txt" "third-party/licenses/nlohmann-json-LICENSE.MIT.txt" "third-party/licenses/dear-imgui-LICENSE.txt" "third-party/licenses/Coral-LICENSE.txt" "third-party/licenses/dotnet-LICENSE.txt" "third-party/licenses/dotnet-ThirdPartyNotices.txt" "third-party/SDL3/include/SDL3/SDL.h" "third-party/SDL3/lib/libSDL3.a" "third-party/SDL3/cmake/SDL3Config.cmake" "third-party/SDL3/licenses/SDL3/LICENSE.txt" README.md LICENSE.txt THIRD_PARTY_NOTICES.md build-manifest.json)
    required+=("bin/KeireShaderCompiler")
    for path in "${required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required content: %s\n' "$path" >&2; return 1; }
    done
    local asset_required=("bin/${namespace}AssetTool" "bin/${namespace}AssetWorker" "bin/${namespace}Runtime" "lib/lib${namespace}Zstd.a" "include/$namespace/Math/Math.h" "include/$namespace/ECS/Component.h" "include/$namespace/ECS/Entity.h" "include/$namespace/ECS/Components/TransformComponent.h" "include/$namespace/ECS/Components/DirectionalLightComponent.h" "include/$namespace/ECS/Components/AudioComponents.h" "include/$namespace/ECS/Components/RuntimeUiComponents.h" "include/$namespace/Assets/Asset.h" "include/$namespace/Assets/AssetSystem.h" "include/$namespace/Assets/AssetPipeline.h" "include/$namespace/Assets/InputActionAsset.h" "include/$namespace/Input/Input.h" "samples/KeireSandbox/Assets/Input/DefaultInput.keireinput.keiremeta" "third-party/licenses/zstandard-LICENSE.txt" "third-party/licenses/entt-LICENSE.txt" "third-party/licenses/glm-COPYING.txt")
    asset_required+=("include/$namespace/ECS/Components/CameraComponent.h" "include/$namespace/ECS/Components/MeshRendererComponent.h" "include/$namespace/Rendering/RenderSystem.h" "include/$namespace/Assets/RenderingAssets.h" "samples/KeireSandbox/Assets/Shaders/DefaultUnlit.keireshader" "samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl" "samples/KeireSandbox/Assets/Materials/DefaultUnlit.keirematerial" "third-party/licenses/SDL-shadercross-LICENSE.txt" "third-party/licenses/DirectXShaderCompiler-LICENSE.txt" "third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt" "third-party/licenses/SPIRV-Cross-LICENSE.txt" "third-party/licenses/SPIRV-Headers-LICENSE.txt" "third-party/licenses/SPIRV-Tools-LICENSE.txt" "third-party/licenses/assimp-LICENSE.txt" "third-party/licenses/assimp-zlib-LICENSE.txt" "third-party/licenses/stb-LICENSE.txt" "third-party/licenses/Jolt-LICENSE.txt" "third-party/licenses/Recast-LICENSE.txt" "third-party/licenses/miniaudio-LICENSE.txt" "lib/libassimp.a" "lib/libzlibstatic.a" "lib/libJolt.a" "lib/libRecast.a" "lib/libDetour.a" "lib/libDetourCrowd.a" "lib/libDetourTileCache.a" "lib/libminiaudio.a" "lib/libCoral.Native.a" "lib/libnethost.a")
    for path in "${asset_required[@]}"; do
        [[ -f "$stage/$path" ]] || { printf 'Package is missing required asset content: %s\n' "$path" >&2; return 1; }
    done
    [[ ! -e "$stage/include/KeireInternal" ]] || { printf 'Package contains private KeireInternal headers.\n' >&2; return 1; }
    [[ ! -e "$stage/third-party/spdlog" ]] || { printf 'Package contains private spdlog headers.\n' >&2; return 1; }
    [[ ! -e "$stage/third-party/assimp" && ! -e "$stage/third-party/stb" && ! -e "$stage/third-party/SDL3/include/Jolt" && ! -e "$stage/third-party/SDL3/include/recastnavigation" && ! -e "$stage/third-party/SDL3/include/miniaudio" ]] || { printf 'Package contains private implementation headers.\n' >&2; return 1; }
    find "$stage/lib/cmake" -type f -name '*Config.cmake' -print -quit 2>/dev/null | grep -q . || { printf 'Package is missing its CMake package configuration.\n' >&2; return 1; }
    assert_package_generated_data_free "$stage"
}

hub_content_required_paths() {
    local required=(
      "content/Content/en-US.json" "content/Licenses/catalog.json"
      "content/Fonts/Inter-OFL.txt" "content/Fonts/Inter-Variable.ttf"
      "content/Fonts/Material-Symbols-Apache-2.0.txt" "content/Fonts/MaterialSymbolsRounded-Subset.ttf"
      "content/Fonts/SOURCES.md"
      "content/Templates/catalog.json" "content/Templates/Payloads/Empty/README.md"
      "content/Templates/Payloads/Starter3D/README.md"
      "content/Templates/Payloads/Starter3D/Assets/Shaders/StarterUnlit.hlsl"
      "content/Templates/Payloads/Starter3D/ProjectSettings/Rendering.keiresettings"
      "content/Templates/Payloads/Sandbox/README.md"
      "content/Templates/Payloads/Sandbox/Assets/Scripts/Gameplay.keireasm"
      "content/Templates/Payloads/Sandbox/Assets/Scripts/Runtime/FirstPersonCamera.cs"
      "content/Templates/Payloads/Sandbox/ProjectSettings/Scripting.keiresettings"
      "content/Templates/Thumbnails/empty.png" "content/Templates/Thumbnails/starter-3d.png"
      "content/Templates/Thumbnails/sandbox.png"
    )
    printf '%s\n' "${required[@]}"
}

editor_package_required_paths() {
    local client="$1" namespace="$4"
    local required=(
      "bin/$client" "bin/${namespace}AssetTool" "bin/${namespace}AssetWorker"
      "bin/${namespace}HubWorker" "bin/${namespace}Runtime" "bin/KeireShaderCompiler"
      "bin/Managed/Coral.Managed.dll"
      "bin/Managed/Coral.Managed.deps.json" "bin/Managed/Coral.Managed.runtimeconfig.json"
      "bin/Managed/Keire.Managed.dll" "bin/Managed/Dotnet/dotnet" "Config/Client.json"
      "Config/Branding/Keire.png" "Config/Marketplace/trusted-marketplace-key.json" \
      "Config/Marketplace/trusted-marketplace-keys.json"
      "third-party/licenses/libsodium-LICENSE.txt"
      "samples/KeireSandbox/ProjectSettings/Project.keireproject"
      "samples/KeireSandbox/Assets/Scenes/SampleScene.keirescene" "Docs/PlayerBuilds.md" "README.md"
      "CHANGELOG.md" "LICENSE.txt" "THIRD_PARTY_NOTICES.md" "build-manifest.json"
      "Config/SourceModules.premake.lua" "editor-package.json" "launch-editor.sh"
      "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst"
      "third-party/licenses/doctest-LICENSE.txt" "third-party/licenses/nlohmann-json-LICENSE.MIT.txt"
      "third-party/licenses/dear-imgui-LICENSE.txt" "third-party/licenses/zstandard-LICENSE.txt"
      "third-party/licenses/entt-LICENSE.txt" "third-party/licenses/glm-COPYING.txt"
      "third-party/licenses/SDL-shadercross-LICENSE.txt"
      "third-party/licenses/DirectXShaderCompiler-LICENSE.txt"
      "third-party/licenses/DirectXShaderCompiler-ThirdPartyNotices.txt"
      "third-party/licenses/SPIRV-Cross-LICENSE.txt" "third-party/licenses/SPIRV-Headers-LICENSE.txt"
      "third-party/licenses/SPIRV-Tools-LICENSE.txt" "third-party/licenses/assimp-LICENSE.txt"
      "third-party/licenses/assimp-zlib-LICENSE.txt" "third-party/licenses/stb-LICENSE.txt"
      "third-party/licenses/Jolt-LICENSE.txt" "third-party/licenses/Recast-LICENSE.txt"
      "third-party/licenses/miniaudio-LICENSE.txt" "third-party/licenses/Coral-LICENSE.txt"
      "third-party/licenses/dotnet-LICENSE.txt" "third-party/licenses/dotnet-ThirdPartyNotices.txt"
    )
    printf '%s\n' "${required[@]}"
    hub_content_required_paths
}

validate_editor_package_stage() {
    local stage="$1" client="$2" hub="$3" core="$4" namespace="$5" platform="$6" path pattern
    while IFS= read -r path; do
        [[ -f "$stage/$path" ]] || {
            printf 'Editor package is missing required content: %s\n' "$path" >&2
            return 1
        }
    done < <(editor_package_required_paths "$client" "$hub" "$core" "$namespace")
    for path in "bin/$client" "bin/${namespace}AssetTool" "bin/${namespace}AssetWorker" \
      "bin/${namespace}HubWorker" "bin/${namespace}Runtime" "bin/KeireShaderCompiler" \
      "bin/Managed/Dotnet/dotnet" "launch-editor.sh"; do
        [[ -x "$stage/$path" ]] || {
            printf 'Editor package entry is not executable: %s\n' "$path" >&2
            return 1
        }
    done
    find "$stage/bin/Managed/Dotnet/sdk" -mindepth 1 -maxdepth 1 -type d -name '10.*' -print -quit 2>/dev/null |
      grep -q . || { printf 'Editor package does not contain the .NET 10 SDK.\n' >&2; return 1; }
    for pattern in 'libavcodec.*' 'libavformat.*' 'libavutil.*' 'libswresample.*'; do
        find "$stage/bin" -maxdepth 1 -type f -name "$pattern" -print -quit | grep -q . || {
            printf "Editor package is missing an FFmpeg runtime matching '%s'.\n" "$pattern" >&2
            return 1
        }
    done
    local sodium_runtime=bin/libsodium.so
    [[ "$platform" == Mac ]] && sodium_runtime=bin/libsodium.dylib
    [[ -f "$stage/$sodium_runtime" ]] || {
        printf 'Editor package is missing its pinned marketplace signature verifier.\n' >&2
        return 1
    }
    for path in include lib examples; do
        [[ ! -e "$stage/$path" ]] || {
            printf 'Editor package contains SDK-only content: %s\n' "$path" >&2
            return 1
        }
    done
    for path in "bin/$hub" "${hub}.app"; do
        [[ ! -e "$stage/$path" ]] || {
            printf 'Editor package contains standalone Hub-owned content: %s\n' "$path" >&2
            return 1
        }
    done
    if [[ "$platform" == Mac ]]; then
        [[ -x "$stage/${client}.app/Contents/MacOS/${client}Launcher" &&
           -f "$stage/${client}.app/Contents/Info.plist" ]] || {
            printf 'macOS editor package is missing its application bundle launcher.\n' >&2
            return 1
        }
    fi
    assert_package_generated_data_free "$stage"
}

resolve_existing_package_stage() {
    local platform="$1"
    local requested="${KEIRE_EXISTING_PACKAGE_STAGE:-}"

    [[ -n "$requested" ]] || {
      printf 'KEIRE_EXISTING_PACKAGE_STAGE is empty.\n' >&2
      return 1
    }
    [[ "$platform" == Linux ]] || {
      printf 'KEIRE_EXISTING_PACKAGE_STAGE is supported only for Linux native installer assembly.\n' >&2
      return 1
    }
    [[ "$requested" == /* ]] || {
      printf 'KEIRE_EXISTING_PACKAGE_STAGE must be an absolute path: %s\n' "$requested" >&2
      return 1
    }
    [[ -d "$requested" && ! -L "$requested" ]] || {
      printf 'KEIRE_EXISTING_PACKAGE_STAGE must name a concrete package-stage directory: %s\n' "$requested" >&2
      return 1
    }

    (cd -P -- "$requested" && pwd)
}

hub_package_required_paths() {
    local hub="$1" namespace="$2"
    local required=(
      "bin/$hub" "bin/${namespace}HubWorker" "Config/Branding/Keire.png"
      "Config/Marketplace/trusted-marketplace-key.json" "Config/Marketplace/trusted-marketplace-keys.json"
      "Config/SourceModules.premake.lua" "Config/Distribution.json" "Config/Supabase.json" "Docs/ProjectHub.md"
      "Samples/KeireSandbox/ProjectSettings/Project.keireproject" "README.md" "CHANGELOG.md" "LICENSE.txt"
      "THIRD_PARTY_NOTICES.md" "hub-package.json" "launch-hub.sh"
      "third-party/licenses/spdlog-LICENSE.txt" "third-party/licenses/fmt-LICENSE.rst"
      "third-party/licenses/nlohmann-json-LICENSE.MIT.txt" "third-party/licenses/dear-imgui-LICENSE.txt"
      "third-party/licenses/zstandard-LICENSE.txt" "third-party/licenses/entt-LICENSE.txt"
      "third-party/licenses/glm-COPYING.txt" "third-party/licenses/SDL3-LICENSE.txt"
    )
    printf '%s\n' "${required[@]}"
    hub_content_required_paths
}

validate_hub_package_stage() {
    local stage="$1" hub="$2" client="$3" namespace="$4" platform="$5" path
    while IFS= read -r path; do
        [[ -f "$stage/$path" ]] || {
            printf 'Hub package is missing required content: %s\n' "$path" >&2
            return 1
        }
    done < <(hub_package_required_paths "$hub" "$namespace")
    for path in "bin/$hub" "bin/${namespace}HubWorker" launch-hub.sh; do
        [[ -x "$stage/$path" ]] || {
            printf 'Hub package entry is not executable: %s\n' "$path" >&2
            return 1
        }
    done
    local sodium_runtime=bin/libsodium.so
    [[ "$platform" == Mac ]] && sodium_runtime=bin/libsodium.dylib
    [[ -f "$stage/$sodium_runtime" &&
       -f "$stage/third-party/licenses/libsodium-LICENSE.txt" ]] || {
        printf 'Hub package is missing its pinned libsodium runtime or license.\n' >&2
        return 1
    }
    for path in "bin/$client" "bin/${namespace}AssetTool" "bin/${namespace}AssetWorker" \
      "bin/${namespace}Runtime" bin/KeireShaderCompiler bin/Managed/Dotnet/sdk; do
        [[ ! -e "$stage/$path" ]] || {
            printf 'Hub package contains editor-only content: %s\n' "$path" >&2
            return 1
        }
    done
    for path in include lib examples; do
        [[ ! -e "$stage/$path" ]] || {
            printf 'Hub package contains SDK-only content: %s\n' "$path" >&2
            return 1
        }
    done
    if [[ "$platform" == Mac ]]; then
        [[ -x "$stage/${hub}.app/Contents/MacOS/${hub}Launcher" &&
           -f "$stage/${hub}.app/Contents/Info.plist" ]] || {
            printf 'macOS Hub package is missing its application bundle launcher.\n' >&2
            return 1
        }
    fi
    assert_package_generated_data_free "$stage"
}

is_macos_macho_file() {
    local candidate="$1"
    file -b "$candidate" 2>/dev/null | grep -q 'Mach-O'
}

macos_macho_minimum_versions() {
    local candidate="$1" output versions
    if command -v vtool >/dev/null 2>&1; then
        output="$(vtool -show-build "$candidate" 2>/dev/null || true)"
        versions="$(printf '%s\n' "$output" | awk '$1 == "minos" { print $2 }')"
        if [[ -n "$versions" ]]; then
            printf '%s\n' "$versions"
            return 0
        fi
    fi

    if command -v otool >/dev/null 2>&1; then
        output="$(otool -l "$candidate" 2>/dev/null || true)"
        versions="$(printf '%s\n' "$output" | awk '
          $1 == "cmd" { legacy = ($2 == "LC_VERSION_MIN_MACOSX"); next }
          $1 == "minos" { print $2; next }
          legacy && $1 == "version" { print $2; legacy = 0 }
        ')"
        if [[ -n "$versions" ]]; then
            printf '%s\n' "$versions"
            return 0
        fi
    fi

    printf 'Could not read a macOS minimum version from Mach-O file: %s\n' "$candidate" >&2
    return 1
}

validate_macos_macho_minimum() {
    local root="$1" expected="$2" excluded_root="${3:-}" candidate version versions
    local found=0
    [[ -d "$root" ]] || { printf 'Mach-O validation root does not exist: %s\n' "$root" >&2; return 1; }
    [[ "$expected" =~ ^[0-9]+\.[0-9]+$ ]] || {
        printf 'Expected macOS deployment target must be major.minor: %s\n' "$expected" >&2
        return 1
    }
    command -v file >/dev/null 2>&1 || { printf 'file is required for Mach-O validation.\n' >&2; return 1; }
    if ! command -v vtool >/dev/null 2>&1 && ! command -v otool >/dev/null 2>&1; then
        printf 'vtool or otool is required for Mach-O deployment-target validation.\n' >&2
        return 1
    fi

    while IFS= read -r -d '' candidate; do
        if [[ -n "$excluded_root" ]]; then
            case "$candidate" in "$excluded_root"|"$excluded_root"/*) continue ;; esac
        fi
        is_macos_macho_file "$candidate" || continue
        found=1
        versions="$(macos_macho_minimum_versions "$candidate")" || return 1
        while IFS= read -r version; do
            [[ "$version" =~ ^([0-9]+)\.([0-9]+)(\.[0-9]+)?$ ]] || {
                printf 'Invalid Mach-O minimum version %s in %s.\n' "$version" "$candidate" >&2
                return 1
            }
            if [[ "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}" != "$expected" ]]; then
                printf 'Mach-O deployment target mismatch in %s: expected %s, found %s.\n' \
                  "$candidate" "$expected" "$version" >&2
                return 1
            fi
        done <<< "$versions"
    done < <(find "$root" -type f -print0)

    [[ "$found" == 1 ]] || {
        printf 'Mach-O deployment-target validation found no binaries beneath %s.\n' "$root" >&2
        return 1
    }
}

write_sha256_tree_manifest() {
    local root="$1" output="$2"
    command -v shasum >/dev/null 2>&1 || {
        printf 'shasum is required to preserve bundled runtime signatures.\n' >&2
        return 1
    }
    (
        cd "$root" || exit 1
        find . -type f -exec shasum -a 256 {} + | LC_ALL=C sort > "$output"
    )
}

verify_macos_signed_macho_tree() {
    local root="$1" description="$2" candidate
    local found=0
    while IFS= read -r -d '' candidate; do
        is_macos_macho_file "$candidate" || continue
        found=1
        codesign --verify --strict --verbose=2 "$candidate" || {
            printf '%s contains an invalid or missing native signature: %s\n' "$description" "$candidate" >&2
            return 1
        }
    done < <(find "$root" -type f -print0)
    [[ "$found" == 1 ]] || {
        printf '%s contains no Mach-O files to verify: %s\n' "$description" "$root" >&2
        return 1
    }
}

validate_macos_managed_host_entitlements() {
    local managed_host="$1" output="$2" key value
    codesign -d --entitlements :- "$managed_host" > "$output" 2>/dev/null || {
        printf 'Could not read managed-host entitlements from %s.\n' "$managed_host" >&2
        return 1
    }
    plutil -lint "$output" >/dev/null
    for key in com.apple.security.cs.allow-jit \
      com.apple.security.cs.allow-unsigned-executable-memory \
      com.apple.security.cs.allow-dyld-environment-variables \
      com.apple.security.cs.disable-library-validation; do
        value="$(/usr/libexec/PlistBuddy -c "Print :$key" "$output" 2>/dev/null || true)"
        [[ "$value" == true ]] || {
            printf 'Managed host is missing required entitlement %s.\n' "$key" >&2
            return 1
        }
    done
    if /usr/libexec/PlistBuddy -c 'Print :com.apple.security.get-task-allow' "$output" >/dev/null 2>&1; then
        printf 'Managed host must not contain the debug get-task-allow entitlement.\n' >&2
        return 1
    fi
}

sign_macos_app_inside_out() {
    local app="$1" payload="$2" identity="$3" managed_host="$4" entitlements="$5" work_root="$6"
    local dotnet_root="$payload/bin/Managed/Dotnet" candidate bundle before after actual_entitlements
    local managed_host_signed=0
    [[ -d "$app" && -d "$payload" ]] || {
        printf 'macOS signing requires an application bundle and payload directory.\n' >&2
        return 1
    }
    [[ -n "$identity" ]] || { printf 'macOS signing identity is empty.\n' >&2; return 1; }
    command -v codesign >/dev/null 2>&1 || { printf 'codesign is required for macOS signing.\n' >&2; return 1; }
    command -v plutil >/dev/null 2>&1 || { printf 'plutil is required for entitlement validation.\n' >&2; return 1; }

    if [[ -n "$managed_host" ]]; then
        [[ -f "$managed_host" && -f "$entitlements" ]] || {
            printf 'Managed-host signing inputs are missing.\n' >&2
            return 1
        }
        plutil -lint "$entitlements" >/dev/null
    fi

    if [[ -d "$dotnet_root" ]]; then
        before="$work_root/dotnet-before.sha256"
        after="$work_root/dotnet-after.sha256"
        write_sha256_tree_manifest "$dotnet_root" "$before"
        verify_macos_signed_macho_tree "$dotnet_root" 'Bundled Microsoft .NET runtime before signing'
    fi

    while IFS= read -r -d '' candidate; do
        if [[ -d "$dotnet_root" ]]; then
            case "$candidate" in "$dotnet_root"|"$dotnet_root"/*) continue ;; esac
        fi
        is_macos_macho_file "$candidate" || continue
        if [[ -n "$managed_host" && "$candidate" == "$managed_host" ]]; then
            codesign --force --options runtime --timestamp --entitlements "$entitlements" \
              --sign "$identity" "$candidate"
            managed_host_signed=1
        else
            codesign --force --options runtime --timestamp --sign "$identity" "$candidate"
        fi
    done < <(find "$payload" -type f -print0)

    if [[ -n "$managed_host" && "$managed_host_signed" != 1 ]]; then
        printf 'The declared managed editor host is not a Mach-O file: %s\n' "$managed_host" >&2
        return 1
    fi

    while IFS= read -r -d '' bundle; do
        if [[ -d "$dotnet_root" ]]; then
            case "$bundle" in "$dotnet_root"|"$dotnet_root"/*) continue ;; esac
        fi
        codesign --force --options runtime --timestamp --sign "$identity" "$bundle"
    done < <(find "$payload" -depth -type d \( -name '*.framework' -o -name '*.xpc' -o \
      -name '*.appex' -o -name '*.app' \) -print0)

    codesign --force --options runtime --timestamp --sign "$identity" "$app"

    while IFS= read -r -d '' candidate; do
        if [[ -d "$dotnet_root" ]]; then
            case "$candidate" in "$dotnet_root"|"$dotnet_root"/*) continue ;; esac
        fi
        is_macos_macho_file "$candidate" || continue
        codesign --verify --strict --verbose=2 "$candidate"
    done < <(find "$payload" -type f -print0)
    while IFS= read -r -d '' bundle; do
        if [[ -d "$dotnet_root" ]]; then
            case "$bundle" in "$dotnet_root"|"$dotnet_root"/*) continue ;; esac
        fi
        codesign --verify --strict --verbose=2 "$bundle"
    done < <(find "$payload" -depth -type d \( -name '*.framework' -o -name '*.xpc' -o \
      -name '*.appex' -o -name '*.app' \) -print0)
    codesign --verify --strict --verbose=2 "$app"

    if [[ -d "$dotnet_root" ]]; then
        write_sha256_tree_manifest "$dotnet_root" "$after"
        cmp "$before" "$after" >/dev/null || {
            printf 'macOS signing modified the bundled Microsoft .NET runtime.\n' >&2
            return 1
        }
        verify_macos_signed_macho_tree "$dotnet_root" 'Bundled Microsoft .NET runtime after signing'
    fi

    if [[ -n "$managed_host" ]]; then
        actual_entitlements="$work_root/managed-host.entitlements.plist"
        validate_macos_managed_host_entitlements "$managed_host" "$actual_entitlements"
    fi
}

resolve_unix_toolset() {
    local platform="$1" requested="$2"
    if [[ "$requested" != default ]]; then printf '%s' "$requested"
    elif [[ "$platform" == Mac ]]; then printf 'clang'
    else printf 'gcc'; fi
}

native_architecture() {
    case "$(uname -m)" in
        x86_64|amd64) printf 'x86_64' ;;
        arm64|aarch64) printf 'ARM64' ;;
        *) printf "Unsupported host architecture '%s'.\n" "$(uname -m)" >&2; return 1 ;;
    esac
}

normalize_architecture() {
    local value
    value="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$value" in
        x64|amd64|x86_64) printf 'x86_64' ;;
        arm64|aarch64) printf 'ARM64' ;;
        *) printf "Unsupported architecture '%s'. Expected x86_64 or ARM64.\n" "$1" >&2; return 1 ;;
    esac
}

normalize_configuration() {
    local value
    value="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
    case "$value" in
        debug) printf 'Debug' ;;
        release) printf 'Release' ;;
        profile) printf 'Profile' ;;
        dist) printf 'Dist' ;;
        debugasan) printf 'DebugASan' ;;
        debugubsan) printf 'DebugUBSan' ;;
        debugtsan) printf 'DebugTSan' ;;
        coverage) printf 'Coverage' ;;
        *) printf "Unsupported configuration '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

premake_architecture() {
    [[ "$(normalize_architecture "$1")" == ARM64 ]] && printf 'aarch64' || printf 'x86_64'
}

architecture_output_name() {
    [[ "$(normalize_architecture "$1")" == ARM64 ]] && printf 'AARCH64' || printf 'x86_64'
}

remove_generated_binary_directory() {
    local root="${1:?repository root is required}"
    local path="${2:?generated binary directory is required}"
    local resolved_root resolved_base resolved_path component current relative
    resolved_root="$(cd -P "$root" && pwd -P)" || return 1
    [[ -d "$root/Build/Bin" && ! -L "$root/Build" && ! -L "$root/Build/Bin" ]] || {
        printf 'Refusing to remove generated binaries through an invalid or symbolic Build/Bin path: %s.\n' \
            "$root/Build/Bin" >&2
        return 1
    }
    resolved_base="$(cd -P "$root/Build/Bin" && pwd -P)" || return 1
    resolved_path="$(cd -P "$path" && pwd -P)" || return 1
    case "$resolved_base" in
        "$resolved_root"/*) ;;
        *) printf 'Refusing to use a generated binary root outside the repository: %s.\n' "$resolved_base" >&2; return 1 ;;
    esac
    case "$resolved_path" in
        "$resolved_base"/*) ;;
        *) printf 'Refusing to remove generated binaries outside %s: %s.\n' "$resolved_base" "$resolved_path" >&2; return 1 ;;
    esac

    relative="${path#"$root"/}"
    current="$root"
    IFS='/' read -r -a components <<< "$relative"
    for component in "${components[@]}"; do
        current="$current/$component"
        [[ ! -L "$current" ]] || {
            printf 'Refusing to remove a symbolic generated binary path: %s.\n' "$current" >&2
            return 1
        }
    done
    rm -rf -- "$resolved_path"
}

invalidate_incompatible_binary_outputs() {
    local root="${1:?repository root is required}"
    local system="${2:?target system is required}"
    local architecture="${3:?target architecture is required}"
    local toolset="${4:?toolset is required}"
    local identity_stamp="${5:?generation identity stamp is required}"
    local current_architecture prior_line="" prior_generator="" prior_architecture="" prior_toolset=""
    local prior_cache="" prior_ci="" prior_fingerprint="" prior_extra="" normalized_prior="" configuration target
    case "$system" in
        linux|macosx) ;;
        *) printf 'Unsupported generated binary target system: %s.\n' "$system" >&2; return 1 ;;
    esac
    case "$toolset" in
        gcc|clang) ;;
        *) printf 'Unsupported generated binary toolset: %s.\n' "$toolset" >&2; return 1 ;;
    esac
    current_architecture="$(normalize_architecture "$architecture")" || return 1

    if [[ -f "$identity_stamp" ]]; then
        prior_line="$(tr -d '\r\n' < "$identity_stamp")"
        IFS='|' read -r prior_generator prior_architecture prior_toolset prior_cache prior_ci prior_fingerprint \
            prior_extra <<< "$prior_line"
        normalized_prior="$(normalize_architecture "$prior_architecture" 2>/dev/null || true)"
        if [[ -n "$prior_generator" && -n "$prior_fingerprint" && -z "$prior_extra" &&
              "$normalized_prior" == "$current_architecture" && "$prior_toolset" == "$toolset" ]]; then
            return 0
        fi
    fi

    local output_architecture
    output_architecture="$(architecture_output_name "$current_architecture")" || return 1
    local targets=()
    for configuration in Debug Release Profile Dist DebugASan DebugUBSan DebugTSan Coverage; do
        target="$root/Build/Bin/$configuration-$system-$output_architecture"
        [[ -e "$target" || -L "$target" ]] && targets+=("$target")
    done
    [[ ${#targets[@]} -gt 0 ]] || return 0
    printf '==> Removing binaries with unknown or incompatible toolset provenance for %s-%s\n' \
        "$system" "$output_architecture"
    for target in "${targets[@]}"; do
        remove_generated_binary_directory "$root" "$target" || return 1
    done
}

find_premake_binary() {
    find "$1" -type f -name premake5 -print -quit
}

validate_unix_combination() {
    local platform="$1" generator="$2" toolset="$3"
    if [[ "$toolset" == "msc" ]]; then
        printf 'The MSVC toolset is available only on Windows.\n' >&2
        return 1
    fi
    if [[ "$platform" == "Mac" && "$generator" == "xcode4" && "$toolset" != "default" && "$toolset" != "clang" ]]; then
        printf 'Xcode supports only the default or Clang toolset.\n' >&2
        return 1
    fi
}

mac_requires_full_xcode() {
    [[ "$1" == xcode4 ]]
}

version_at_least() {
    local actual="$1" minimum="$2" index actual_part minimum_part
    local IFS=.
    read -r -a actual_parts <<< "$actual"
    read -r -a minimum_parts <<< "$minimum"
    for ((index = 0; index < ${#actual_parts[@]} || index < ${#minimum_parts[@]}; ++index)); do
        actual_part="${actual_parts[index]:-0}"; actual_part="${actual_part%%[^0-9]*}"
        minimum_part="${minimum_parts[index]:-0}"; minimum_part="${minimum_part%%[^0-9]*}"
        ((10#${actual_part:-0} > 10#${minimum_part:-0})) && return 0
        ((10#${actual_part:-0} < 10#${minimum_part:-0})) && return 1
    done
    return 0
}

atomic_symlink_lock_token_pid() {
    local token="${1-}"
    local pid
    case "$token" in
        keire-owner:*) pid="${token#keire-owner:}"; pid="${pid%%:*}" ;;
        *) return 1 ;;
    esac
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    printf '%s\n' "$pid"
}

atomic_symlink_lock_read_legacy_pid() {
    local lock="${1:?lock path is required}"
    # Keep the path as a file operand so a concurrent release remains a contained command failure.
    # shellcheck disable=SC2002
    cat "$lock/pid" 2>/dev/null | tr -dc '0-9' || true
}

atomic_symlink_lock_recover_stale() {
    local lock="${1:?lock path is required}"
    local observed_kind="" observed_owner="" observed_token="" owner="" quarantine=""
    local moved_kind="" moved_owner="" moved_token=""
    if [[ -L "$lock" ]]; then
        observed_kind=symlink
        observed_token="$(readlink "$lock" 2>/dev/null || true)"
        owner="$(atomic_symlink_lock_token_pid "$observed_token" 2>/dev/null || true)"
    elif [[ -d "$lock" ]]; then
        observed_kind=legacy
        observed_owner="$(atomic_symlink_lock_read_legacy_pid "$lock")"
        owner="$observed_owner"
    else
        return 1
    fi
    [[ -z "$owner" ]] || ! kill -0 "$owner" 2>/dev/null || return 1

    quarantine="$lock.stale.$$.$RANDOM"
    mv "$lock" "$quarantine" 2>/dev/null || return 1
    if [[ -L "$quarantine" ]]; then
        moved_kind=symlink
        moved_token="$(readlink "$quarantine" 2>/dev/null || true)"
    elif [[ -d "$quarantine" ]]; then
        moved_kind=legacy
        moved_owner="$(atomic_symlink_lock_read_legacy_pid "$quarantine")"
    else
        moved_kind=unsafe
    fi
    if [[ "$moved_kind" != "$observed_kind" ||
          ( "$moved_kind" == symlink && "$moved_token" != "$observed_token" ) ||
          ( "$moved_kind" == legacy && "$moved_owner" != "$observed_owner" ) ]]; then
        if [[ ! -e "$lock" && ! -L "$lock" ]]; then
            mv "$quarantine" "$lock" 2>/dev/null || true
        fi
        return 1
    fi
    if [[ "$moved_kind" == symlink ]]; then
        rm -f "$quarantine"
    else
        rm -f "$quarantine/pid"
        rmdir "$quarantine" 2>/dev/null || return 1
    fi
}

atomic_symlink_lock_acquire() {
    local lock="${1:?lock path is required}"
    local token="keire-owner:$$:$RANDOM:$RANDOM"
    if [[ -e "$lock" || -L "$lock" ]]; then
        atomic_symlink_lock_recover_stale "$lock" || return 1
    fi
    ln -s "$token" "$lock" 2>/dev/null || return 1
    [[ -L "$lock" && "$(readlink "$lock" 2>/dev/null || true)" == "$token" ]] || return 1
    printf '%s\n' "$token"
}

atomic_symlink_lock_release() {
    local lock="${1:?lock path is required}"
    local token="${2:?lock ownership token is required}"
    local current
    [[ -L "$lock" ]] || return 1
    current="$(readlink "$lock" 2>/dev/null || true)"
    [[ "$current" == "$token" ]] || return 1
    rm -f "$lock"
}

extract_version() {
    awk '
        match($0, /[0-9]+([.][0-9]+)*/) && !found {
            print substr($0, RSTART, RLENGTH)
            found = 1
        }
        END { if (!found) exit 1 }
    '
}

resolve_llvm_tool() {
    local tool="$1" compiler="${2:-clang++}" compiler_path compiler_version compiler_major
    local candidate candidate_path tool_version
    compiler_path="$(command -v "$compiler" 2>/dev/null)" || { printf '%s was not found.\n' "$compiler" >&2; return 1; }
    compiler_version="$("$compiler_path" --version | extract_version)" || return 1
    compiler_major="${compiler_version%%.*}"

    for candidate in "$(dirname "$compiler_path")/$tool" "$tool-$compiler_major" "$tool$compiler_major" "$tool"; do
        if [[ "$candidate" == */* ]]; then
            [[ -x "$candidate" ]] || continue
            candidate_path="$candidate"
        else
            candidate_path="$(command -v "$candidate" 2>/dev/null)" || continue
        fi
        tool_version="$("$candidate_path" --version 2>/dev/null | extract_version)" || continue
        if [[ "${tool_version%%.*}" == "$compiler_major" ]]; then
            printf '%s' "$candidate_path"
            return 0
        fi
    done

    printf 'No %s matching Clang major version %s was found.\n' "$tool" "$compiler_major" >&2
    return 1
}

package_name() {
    local manager="$1" logical="$2"
    case "$manager:$logical" in
        apt-get:ninja|dnf:ninja) printf 'ninja-build' ;;
        pacman:ninja|zypper:ninja) printf 'ninja' ;;
        apt-get:cxx) printf 'g++' ;;
        dnf:cxx|zypper:cxx) printf 'gcc-c++' ;;
        pacman:cxx) printf 'gcc' ;;
        apt-get:modern-cxx) printf 'gcc-12 g++-12' ;;
        dnf:modern-cxx) printf 'gcc-toolset-12-gcc gcc-toolset-12-gcc-c++' ;;
        apt-get:build) printf 'build-essential' ;;
        dnf:build|zypper:build) printf 'gcc-c++ make' ;;
        pacman:build) printf 'base-devel' ;;
        pacman:python) printf 'python' ;;
        *:python) printf 'python3' ;;
        apt-get:perl-json) printf 'libjson-perl' ;;
        dnf:perl-json|zypper:perl-json) printf 'perl-JSON' ;;
        pacman:perl-json) printf 'perl-json' ;;
        dnf:perl-open) printf 'perl-open' ;;
        apt-get:perl-open|pacman:perl-open|zypper:perl-open) printf 'perl' ;;
        apt-get:dotnet-runtime-deps) printf 'ca-certificates libicu-dev libssl-dev libgssapi-krb5-2 zlib1g' ;;
        dnf:dotnet-runtime-deps) printf 'ca-certificates libicu openssl-libs krb5-libs zlib' ;;
        pacman:dotnet-runtime-deps) printf 'ca-certificates icu openssl krb5 zlib' ;;
        zypper:dotnet-runtime-deps) printf 'ca-certificates libicu-devel libopenssl3 krb5 libz1' ;;
        apt-get:curl-dev) printf 'libcurl4-openssl-dev' ;;
        dnf:curl-dev|zypper:curl-dev) printf 'libcurl-devel' ;;
        pacman:curl-dev) printf 'curl' ;;
        apt-get:awk) printf 'mawk' ;;
        dnf:awk|pacman:awk|zypper:awk) printf 'gawk' ;;
        apt-get:uuid) printf 'uuid-dev' ;;
        dnf:uuid|zypper:uuid) printf 'libuuid-devel' ;;
        pacman:uuid) printf 'util-linux-libs' ;;
        apt-get:sdl-video) printf 'libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libdrm-dev libgbm-dev' ;;
        dnf:sdl-video) printf 'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel libXtst-devel wayland-devel libxkbcommon-devel libdrm-devel mesa-libgbm-devel' ;;
        pacman:sdl-video) printf 'libx11 libxext libxrandr libxcursor libxfixes libxi libxss libxtst wayland libxkbcommon libdrm mesa' ;;
        zypper:sdl-video) printf 'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXss-devel libXtst-devel wayland-devel libxkbcommon-devel libdrm-devel libgbm-devel' ;;
        apt-get:sdl-input) printf 'libudev-dev' ;;
        dnf:sdl-input) printf 'systemd-devel' ;;
        pacman:sdl-input) printf 'systemd' ;;
        zypper:sdl-input) printf 'libudev-devel' ;;
        *:native-dialog) printf 'zenity' ;;
        *:llvm) printf 'llvm' ;;
        *:*) printf '%s' "$logical" ;;
    esac
}

package_install_arguments() {
    case "$1" in
        apt-get) printf '%s\n' -y ;;
        dnf) printf '%s\n' install -y ;;
        pacman) printf '%s\n' -Syu --needed --noconfirm ;;
        zypper) printf '%s\n' --non-interactive install ;;
        *) printf "Unsupported package manager '%s'.\n" "$1" >&2; return 1 ;;
    esac
}

run_checked() {
    "$@"
    local status=$?
    if [[ $status -ne 0 ]]; then
        printf "Command failed with exit code %s: %s\n" "$status" "$*" >&2
        return "$status"
    fi
}

build_parallel_jobs() {
    local detected
    if [[ -n "${KEIRE_BUILD_JOBS:-}" ]]; then
        if [[ ! "$KEIRE_BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
            printf 'KEIRE_BUILD_JOBS must be a positive integer.\n' >&2
            return 1
        fi
        printf '%s\n' "$KEIRE_BUILD_JOBS"
        return 0
    fi

    detected="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)"
    [[ "$detected" =~ ^[1-9][0-9]*$ ]] || detected=1
    ((detected > 4)) && detected=4
    printf '%s\n' "$detected"
}

resolve_compiler_cache() {
    local generator="$1" requested="${2:-auto}"
    case "$requested" in auto|off|sccache) ;; *)
        printf "Unsupported compiler cache '%s'. Use auto, off, or sccache.\n" "$requested" >&2
        return 1
    esac
    if [[ "$generator" != ninja && "$generator" != compilecommands ]] || [[ "$requested" == off ]]; then
        printf '%s\n' off
        return 0
    fi
    if command -v sccache >/dev/null 2>&1; then
        printf '%s\n' sccache
        return 0
    fi
    [[ "$requested" != sccache ]] || {
        printf '%s\n' 'sccache was requested but is not available in PATH.' >&2
        return 1
    }
    printf '%s\n' off
}

activate_linux_toolchain() {
    local root="$1" toolset="$2" environment
    [[ "$toolset" == gcc ]] || return 0
    environment="$root/Tools/Linux/gcc-environment.sh"
    if [[ -f "$environment" ]]; then
        # shellcheck disable=SC1090
        source "$environment"
    fi
}

parse_build_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --generator) GENERATOR="$2"; shift 2 ;;
            --configuration) CONFIGURATION="$(normalize_configuration "$2")"; shift 2 ;;
            --architecture) ARCHITECTURE="$(normalize_architecture "$2")"; shift 2 ;;
            --toolset) TOOLSET="$2"; shift 2 ;;
            --compiler-cache) COMPILER_CACHE="$2"; shift 2 ;;
            --profile-build) PROFILE_BUILD=1; shift ;;
            --target) TARGET="$2"; shift 2 ;;
            --ci) CI=1; shift ;;
            --update) UPDATE=1; shift ;;
            --force) FORCE=1; shift ;;
            --allow-dirty) ALLOW_DIRTY=1; shift ;;
            --install-optional) INSTALL_OPTIONAL=1; shift ;;
            *) printf "Unknown argument '%s'.\n" "$1" >&2; return 1 ;;
        esac
    done
}

managed_host_staging_targets() {
    local target="$1" client_target="$2" hub_target="$3" project_namespace="$4"
    local editor_dev_target="${project_namespace}EditorDev"
    {
        if [[ "$target" == "$client_target" || "$target" == "$hub_target" || "$target" == "$editor_dev_target" ]]; then
            printf '%s\n' "${project_namespace}AssetTool" "${project_namespace}Runtime" "$client_target"
        fi
        [[ "$target" == "$editor_dev_target" ]] || printf '%s\n' "$target"
    } | awk '!seen[$0]++'
}

validate_unix_asset_worker_ninja_commands() {
    local root="${1:?repository root is required}"
    local project_namespace="${2:?project namespace is required}"
    local ninja_file="$root/${project_namespace}AssetWorker/${project_namespace}AssetWorker.ninja"
    local line arguments source_argument destination_argument count=0
    [[ -f "$ninja_file" ]] || {
        printf 'Generated Asset Worker Ninja file is missing: %s\n' "$ninja_file" >&2
        return 1
    }
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ "$line" == *"copy-files-if-changed.sh "* ]] || continue
        count=$((count + 1))
        [[ "$line" == *" && touch "* ]] || {
            printf 'Generated Asset Worker runtime-copy command is malformed: %s\n' "$line" >&2
            return 1
        }
        arguments="${line#*copy-files-if-changed.sh }"
        arguments="${arguments%% && touch *}"
        source_argument="${arguments%% *}"
        destination_argument="${arguments#* }"
        [[ -n "$source_argument" && -n "$destination_argument" && "$destination_argument" != "$arguments" &&
           "$source_argument" != *[[:space:]]* && "$destination_argument" != *[[:space:]]* &&
           "$destination_argument" == */"${project_namespace}AssetWorker" ]] || {
            printf 'Generated Asset Worker runtime-copy command must contain exactly two path arguments: %s\n' \
                "$line" >&2
            return 1
        }
    done < "$ninja_file"
    [[ $count -gt 0 ]] || {
        printf 'Generated Asset Worker Ninja file has no runtime-copy commands: %s\n' "$ninja_file" >&2
        return 1
    }
}

stage_unix_asset_worker_runtime() {
    local root="${1:?repository root is required}" configuration="${2:?configuration is required}"
    local platform="${3:?platform is required}" architecture="${4:?architecture is required}"
    local project_namespace="${5:?project namespace is required}"
    local target="${6:?build target is required}"
    local dependency_configuration=Debug output_architecture source_directory destination_directory worker
    local family candidate name pattern index
    local runtime_names=(sentinel)
    case "$target" in
        "${project_namespace}AssetWorker"|"${project_namespace}AssetTool"|"${project_namespace}EditorTests"| \
        "${project_namespace}EditorDev") ;;
        *) return 0 ;;
    esac
    [[ "$configuration" == Release || "$configuration" == Profile || "$configuration" == Dist ]] && dependency_configuration=Release
    output_architecture="$(architecture_output_name "$architecture")"
    source_directory="$root/Build/Dependencies/ffmpeg/$dependency_configuration/install/lib"
    destination_directory="$root/Build/Bin/$configuration-$platform-$output_architecture/${project_namespace}AssetWorker"
    worker="$destination_directory/${project_namespace}AssetWorker"
    [[ -x "$worker" ]] || return 0
    [[ -d "$source_directory" ]] || {
        printf 'The pinned FFmpeg runtime directory is missing: %s\n' "$source_directory" >&2
        return 1
    }
    case "$platform" in
        linux) pattern=so ;;
        macosx) pattern=dylib ;;
        *) printf 'Unsupported Unix runtime platform: %s\n' "$platform" >&2; return 2 ;;
    esac
    for family in avformat avcodec swresample avutil; do
        name=""
        if [[ "$pattern" == so ]]; then
            for candidate in "$source_directory/lib$family.so."*; do
                [[ -e "$candidate" || -L "$candidate" ]] || continue
                [[ "$(basename "$candidate")" =~ ^lib${family}\.so\.[0-9]+$ ]] || continue
                name="$(basename "$candidate")"
                break
            done
        else
            for candidate in "$source_directory/lib$family."*.dylib; do
                [[ -e "$candidate" || -L "$candidate" ]] || continue
                [[ "$(basename "$candidate")" =~ ^lib${family}\.[0-9]+\.dylib$ ]] || continue
                name="$(basename "$candidate")"
                break
            done
        fi
        [[ -n "$name" ]] || {
            printf 'The pinned FFmpeg runtime is missing the %s SONAME link for %s.\n' "$family" "$platform" >&2
            return 1
        }
        runtime_names+=("$name")
    done
    bash "$root/Scripts/Unix/copy-files-if-changed.sh" "$source_directory" "$destination_directory"
    for candidate in "$destination_directory"/libavformat* "$destination_directory"/libavcodec* \
      "$destination_directory"/libswresample* "$destination_directory"/libavutil*; do
        [[ -e "$candidate" || -L "$candidate" ]] || continue
        name="$(basename "$candidate")"
        if [[ "$pattern" == so ]]; then
            [[ "$name" =~ ^lib(avformat|avcodec|swresample|avutil)\.so(\.[0-9]+)*$ ]] || continue
        else
            [[ "$name" =~ ^lib(avformat|avcodec|swresample|avutil)(\.[0-9]+)*\.dylib$ ]] || continue
        fi
        [[ -e "$source_directory/$name" || -L "$source_directory/$name" ]] || rm -f "$candidate"
    done
    for ((index = 1; index < ${#runtime_names[@]}; ++index)); do
        candidate="$destination_directory/${runtime_names[index]}"
        [[ -f "$candidate" && ! -L "$candidate" ]] || {
            printf 'The staged FFmpeg runtime is missing a regular SONAME file: %s\n' "$candidate" >&2
            return 1
        }
    done
}
