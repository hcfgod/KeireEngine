#!/usr/bin/env bash
set -euo pipefail

backup_root="${1:?Usage: restore-distribution.sh <backup-root> [destination-root|--validate-only]}"
destination="${2:---validate-only}"
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd "$script_root/.." && pwd)"
[[ -d "$backup_root" ]] || { printf 'Distribution backup does not exist.\n' >&2; exit 2; }
backup_path="$(cd "$backup_root" && pwd -P)"
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
started="$(date +%s)"
validate "$backup_path"
if [[ "$destination" == --validate-only ]]; then
  printf 'Distribution restore drill validation passed: %s\n' "$backup_path"
  printf 'Validation elapsed seconds: %s\n' "$(( $(date +%s) - started ))"
  exit 0
fi
mkdir -p -- "$destination"
destination_path="$(cd "$destination" && pwd -P)"
[[ -z "$(find "$destination_path" -mindepth 1 -maxdepth 1 -print -quit)" ]] || {
  printf 'Restore destination must be empty: %s\n' "$destination_path" >&2
  exit 2
}
case "$backup_path/" in "$destination_path/"*) printf 'Restore paths may not contain one another.\n' >&2; exit 2;; esac
case "$destination_path/" in "$backup_path/"*) printf 'Restore paths may not contain one another.\n' >&2; exit 2;; esac
cp -a -- "$backup_path/snapshots" "$destination_path/"
cp -a -- "$backup_path/current" "$destination_path/"
validate "$destination_path"
printf 'Validated distribution restore: %s\n' "$destination_path"
printf 'Recovery time seconds: %s\n' "$(( $(date +%s) - started ))"
