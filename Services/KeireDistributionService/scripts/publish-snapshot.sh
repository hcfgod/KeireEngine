#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
  printf 'Usage: %s <source> <distribution-root> <snapshot-id> <public-key> [--activate]\n' "$0" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd -- "$script_dir/.." && pwd)"
dotnet_command="${KEIRE_DOTNET:-dotnet}"
publisher="$service_root/tools/publisher/KeireDistributionPublisher"
arguments=(publish --source "$1" --root "$2" --snapshot "$3" --public-key "$4")
if [[ ${5:-} == --activate ]]; then
  arguments+=(--activate)
elif [[ $# -eq 5 ]]; then
  printf "Unknown option '%s'.\n" "$5" >&2
  exit 2
fi

if [[ -x "$publisher" ]]; then
  "$publisher" "${arguments[@]}"
else
  "$dotnet_command" run --project \
    "$service_root/src/KeireDistributionPublisher/KeireDistributionPublisher.csproj" \
    --configuration Release -- "${arguments[@]}"
fi
