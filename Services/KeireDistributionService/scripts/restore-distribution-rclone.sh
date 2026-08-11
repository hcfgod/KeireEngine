#!/usr/bin/env bash
set -euo pipefail

destination_root="${1:?Usage: restore-distribution-rclone.sh <destination-root> <remote-root> <rclone-config> [backup-id] [rclone]}"
remote_root="${2:?Usage: restore-distribution-rclone.sh <destination-root> <remote-root> <rclone-config> [backup-id] [rclone]}"
rclone_config="${3:?Usage: restore-distribution-rclone.sh <destination-root> <remote-root> <rclone-config> [backup-id] [rclone]}"
backup_id="${4:-}"
rclone_command="${5:-rclone}"
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd "$script_root/.." && pwd)"

[[ -f "$rclone_config" ]] || { printf 'rclone configuration does not exist.\n' >&2; exit 2; }
[[ "$remote_root" =~ ^[^:]+:.+ ]] || { printf 'Remote root must be a non-root rclone path.\n' >&2; exit 2; }
if [[ "$rclone_command" == */* ]]; then
  [[ -x "$rclone_command" ]] || { printf 'rclone executable does not exist.\n' >&2; exit 2; }
else
  command -v "$rclone_command" >/dev/null 2>&1 || { printf 'rclone is unavailable.\n' >&2; exit 2; }
fi

mkdir -p -- "$destination_root"
destination="$(cd "$destination_root" && pwd -P)"
[[ -z "$(find "$destination" -mindepth 1 -maxdepth 1 -print -quit)" ]] || {
  printf 'Restore destination must be empty: %s\n' "$destination" >&2
  exit 2
}
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

record_directory="$(mktemp -d "${TMPDIR:-/tmp}/keire-distribution-restore-record-XXXXXX")"
cleanup() { rm -rf -- "$record_directory"; }
trap cleanup EXIT
started="$(date +%s)"
if [[ -z "$backup_id" ]]; then
  run_rclone copyto "$remote/latest" "$record_directory/latest" --checksum
  backup_id="$(tr -d '\r\n' < "$record_directory/latest")"
fi
[[ "$backup_id" =~ ^backup-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{16}-[a-f0-9]{8}$ ]] || {
  printf 'Invalid remote backup ID.\n' >&2
  exit 2
}
run_rclone copy "$remote/records/$backup_id" "$record_directory" --checksum
[[ -f "$record_directory/current" && -f "$record_directory/backup.json" ]] || {
  printf 'Remote backup record is incomplete.\n' >&2
  exit 2
}
current="$(tr -d '\r\n' < "$record_directory/current")"
[[ "$current" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || {
  printf 'Invalid remote current pointer.\n' >&2
  exit 2
}
mkdir -p -- "$destination/snapshots/$current"
run_rclone copy "$remote/snapshots/$current" "$destination/snapshots/$current" --checksum
run_rclone check "$remote/snapshots/$current" "$destination/snapshots/$current" --checksum --one-way
cp -- "$record_directory/current" "$destination/current"
validate "$destination"

printf 'Validated off-machine distribution restore %s: %s\n' "$backup_id" "$destination"
printf 'Recovery time seconds: %s\n' "$(( $(date +%s) - started ))"
