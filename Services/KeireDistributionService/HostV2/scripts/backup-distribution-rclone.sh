#!/usr/bin/env bash
set -euo pipefail

distribution_root="${1:?Usage: backup-distribution-rclone.sh <distribution-root> <remote-root> <rclone-config> [rclone]}"
remote_root="${2:?Usage: backup-distribution-rclone.sh <distribution-root> <remote-root> <rclone-config> [rclone]}"
rclone_config="${3:?Usage: backup-distribution-rclone.sh <distribution-root> <remote-root> <rclone-config> [rclone]}"
rclone_command="${4:-rclone}"
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd "$script_root/.." && pwd)"

[[ -d "$distribution_root" ]] || { printf 'Distribution root does not exist.\n' >&2; exit 2; }
[[ -f "$rclone_config" ]] || { printf 'rclone configuration does not exist.\n' >&2; exit 2; }
[[ "$remote_root" =~ ^[^:]+:.+ ]] || { printf 'Remote root must be a non-root rclone path.\n' >&2; exit 2; }
if [[ "$rclone_command" == */* ]]; then
  [[ -x "$rclone_command" ]] || { printf 'rclone executable does not exist.\n' >&2; exit 2; }
else
  command -v "$rclone_command" >/dev/null 2>&1 || { printf 'rclone is unavailable.\n' >&2; exit 2; }
fi

source_path="$(cd "$distribution_root" && pwd -P)"
snapshots_root="$source_path/snapshots"
[[ -d "$snapshots_root" ]] || { printf 'Distribution snapshots directory does not exist.\n' >&2; exit 2; }
remote="${remote_root%/}"
publisher="${KEIRE_DISTRIBUTION_PUBLISHER:-$service_root/tools/publisher/KeireDistributionPublisher}"
validate() {
  if [[ -x "$publisher" ]]; then
    "$publisher" validate --root "$1"
  else
    "${KEIRE_DOTNET:-dotnet}" run --project \
      "$service_root/Source/KeireDistributionPublisher/KeireDistributionPublisher.csproj" \
      --configuration Release -- validate --root "$1"
  fi
}
run_rclone() {
  "$rclone_command" "$@" --config "$rclone_config"
}
read_current() {
  local current
  current="$(tr -d '\r\n' < "$source_path/current")"
  [[ "$current" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || {
    printf 'Invalid current pointer.\n' >&2
    return 2
  }
  printf '%s' "$current"
}
sha256_text() {
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$1" | sha256sum | awk '{print $1}'
  else
    printf '%s' "$1" | shasum -a 256 | awk '{print $1}'
  fi
}

started="$(date +%s)"
current=
stable=0
for attempt in 1 2 3; do
  validate "$source_path"
  current_before="$(read_current)"
  found_current=0
  snapshot_count=0
  for snapshot_path in "$snapshots_root"/*; do
    [[ -d "$snapshot_path" ]] || continue
    snapshot_id="$(basename "$snapshot_path")"
    [[ "$snapshot_id" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || continue
    [[ "$snapshot_id" == "$current_before" ]] && found_current=1
    snapshot_count=$((snapshot_count + 1))
    run_rclone copy "$snapshot_path" "$remote/snapshots/$snapshot_id" --checksum --immutable
  done
  [[ $snapshot_count -gt 0 && $found_current -eq 1 ]] || {
    printf 'The active snapshot directory is missing.\n' >&2
    exit 2
  }
  current_after="$(read_current)"
  if [[ "$current_before" == "$current_after" ]]; then
    current="$current_after"
    stable=1
    break
  fi
  printf 'Active snapshot changed during backup attempt %s; retrying.\n' "$attempt" >&2
done
[[ $stable -eq 1 ]] || { printf 'Active snapshot changed during all backup attempts.\n' >&2; exit 2; }

run_rclone check "$snapshots_root/$current" "$remote/snapshots/$current" --checksum --one-way
created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf -v backup_nonce '%08x' "$(( (RANDOM << 16) ^ RANDOM ^ $$ ))"
backup_id="backup-$(date -u +%Y%m%dT%H%M%SZ)-$(sha256_text "$current" | cut -c1-16)-$backup_nonce"
record_directory="$(mktemp -d "${TMPDIR:-/tmp}/keire-distribution-record-XXXXXX")"
cleanup() { rm -rf -- "$record_directory"; }
trap cleanup EXIT
printf '%s\n' "$current" > "$record_directory/current"
printf '{\n  "schemaVersion": 1,\n  "backupId": "%s",\n  "createdAtUtc": "%s",\n  "snapshotId": "%s"\n}\n' \
  "$backup_id" "$created_at" "$current" > "$record_directory/backup.json"
run_rclone copy "$record_directory" "$remote/records/$backup_id" --checksum --immutable
run_rclone check "$record_directory" "$remote/records/$backup_id" --checksum --one-way
printf '%s\n' "$backup_id" > "$record_directory/latest"
run_rclone copyto "$record_directory/latest" "$remote/latest" --checksum

printf 'Verified off-machine distribution backup %s for snapshot %s.\n' "$backup_id" "$current"
printf 'Backup elapsed seconds: %s\n' "$(( $(date +%s) - started ))"
