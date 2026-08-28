#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 4 ]]; then
  printf 'Usage: %s <source> <distribution-root> <snapshot-id> <public-key> [--minimum-sequence <n>] [--minimum-validity-hours <hours>] [--activate]\n' "$0" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd -- "$script_dir/.." && pwd)"
dotnet_command="${KEIRE_DOTNET:-dotnet}"
publisher="$service_root/tools/publisher/KeireDistributionPublisher"
source_root="$1"
distribution_root="$2"
snapshot_id="$3"
public_key="$4"
shift 4

minimum_sequence=
minimum_validity_hours=24
minimum_validity_provided=false
activate=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --minimum-sequence)
      if [[ -n "$minimum_sequence" || $# -lt 2 ]]; then
        printf 'The minimum-sequence option is duplicated or missing its value.\n' >&2
        exit 2
      fi
      minimum_sequence="$2"
      shift 2
      ;;
    --minimum-validity-hours)
      if [[ $minimum_validity_provided == true || $# -lt 2 ]]; then
        printf 'The minimum-validity-hours option is duplicated or missing its value.\n' >&2
        exit 2
      fi
      minimum_validity_hours="$2"
      minimum_validity_provided=true
      shift 2
      ;;
    --activate)
      if [[ $activate == true ]]; then
        printf 'The activate option may be specified only once.\n' >&2
        exit 2
      fi
      activate=true
      shift
      ;;
    *)
      printf "Unknown option '%s'.\n" "$1" >&2
      exit 2
      ;;
  esac
done

if [[ -n "$minimum_sequence" ]]; then
  if [[ ! $minimum_sequence =~ ^[1-9][0-9]{0,18}$ ]]; then
    printf 'Minimum sequence must be an integer between 1 and 9223372036854775807.\n' >&2
    exit 2
  fi
  if (( ${#minimum_sequence} == 19 )) && [[ $minimum_sequence > 9223372036854775807 ]]; then
    printf 'Minimum sequence must be an integer between 1 and 9223372036854775807.\n' >&2
    exit 2
  fi
elif [[ $activate == true ]]; then
  printf 'Activation requires an explicit --minimum-sequence floor.\n' >&2
  exit 2
fi

if [[ ! $minimum_validity_hours =~ ^[0-9]+([.][0-9]+)?$ ]] ||
  ! LC_ALL=C awk -v value="$minimum_validity_hours" 'BEGIN { exit !(value >= 0 && value <= 87600) }'; then
  printf 'Minimum validity hours must be finite and between 0 and 87600.\n' >&2
  exit 2
fi

arguments=(publish --source "$source_root" --root "$distribution_root" --snapshot "$snapshot_id" \
  --public-key "$public_key" --minimum-validity-hours "$minimum_validity_hours")
if [[ -n "$minimum_sequence" ]]; then
  arguments+=(--minimum-sequence "$minimum_sequence")
fi
if [[ $activate == true ]]; then
  arguments+=(--activate)
fi

if [[ -x "$publisher" ]]; then
  "$publisher" "${arguments[@]}"
else
  "$dotnet_command" run --project \
    "$service_root/Source/KeireDistributionPublisher/KeireDistributionPublisher.csproj" \
    --configuration Release -- "${arguments[@]}"
fi
