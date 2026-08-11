#!/usr/bin/env bash
set -euo pipefail

distribution_root="${1:?Usage: backup-distribution.sh <distribution-root> <off-machine-destination> [backup-id]}"
destination_root="${2:?Usage: backup-distribution.sh <distribution-root> <off-machine-destination> [backup-id]}"
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd "$script_root/.." && pwd)"
[[ -d "$distribution_root" ]] || { printf 'Distribution root does not exist.\n' >&2; exit 2; }
mkdir -p -- "$destination_root"
source_path="$(cd "$distribution_root" && pwd -P)"
destination_path="$(cd "$destination_root" && pwd -P)"
case "$source_path/" in "$destination_path/"*) printf 'Backup paths may not contain one another.\n' >&2; exit 2;; esac
case "$destination_path/" in "$source_path/"*) printf 'Backup paths may not contain one another.\n' >&2; exit 2;; esac

publisher="$service_root/tools/publisher/KeireDistributionPublisher"
validate() {
  if [[ -x "$publisher" ]]; then
    "$publisher" validate --root "$1"
  else
    "${KEIRE_DOTNET:-dotnet}" run --project \
      "$service_root/Source/KeireDistributionPublisher/KeireDistributionPublisher.csproj" \
      --configuration Release -- validate --root "$1"
  fi
}
validate "$source_path"
current="$(tr -d '\r\n' < "$source_path/current")"
[[ "$current" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || { printf 'Invalid current pointer.\n' >&2; exit 2; }
backup_id="${3:-keire-distribution-$(date -u +%Y%m%dT%H%M%SZ)-$current}"
[[ "$backup_id" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || { printf 'Invalid backup ID.\n' >&2; exit 2; }
final="$destination_path/$backup_id"
[[ ! -e "$final" ]] || { printf 'Immutable backup already exists: %s\n' "$final" >&2; exit 2; }
temporary="$(mktemp -d "$destination_path/.$backup_id.tmp-XXXXXX")"
cleanup() { [[ ! -d "$temporary" ]] || rm -rf -- "$temporary"; }
trap cleanup EXIT
started="$(date +%s)"
cp -a -- "$source_path/snapshots" "$temporary/"
cp -a -- "$source_path/current" "$temporary/"
validate "$temporary"
mv -- "$temporary" "$final"
trap - EXIT
printf 'Validated immutable distribution backup: %s\n' "$final"
printf 'Backup elapsed seconds: %s\n' "$(( $(date +%s) - started ))"
