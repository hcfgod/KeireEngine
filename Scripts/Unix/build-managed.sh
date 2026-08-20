#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dotnet="$root/Build/Dependencies/dotnet-sdk/dotnet"
project="$root/KeireManaged/Keire.Managed.csproj"
source_root="$root/KeireManaged"
output="$root/Build/Managed"
intermediate="$root/Build/Intermediates/Managed/"
assembly="$output/Keire.Managed.dll"

if [[ -f "$assembly" ]] && [[ ! "${BASH_SOURCE[0]}" -nt "$assembly" ]] &&
  [[ -z "$(find "$source_root" \( -path '*/bin' -o -path '*/obj' -o -path '*/Build' \) -prune -o \
    \( -type d -o -type f \( -name '*.cs' -o -name '*.csproj' \) \) \
    -newer "$assembly" -print -quit)" ]]; then
  printf '%s\n' '==> Managed runtime API is current'
  exit 0
fi

[[ -x "$dotnet" ]] || {
  printf 'The bundled .NET SDK is missing or not executable: %s\n' "$dotnet" >&2
  exit 1
}

"$dotnet" build "$project" --nologo --configuration Release --output "$output" \
  "--property:BaseIntermediateOutputPath=$intermediate"

[[ -f "$assembly" ]] || {
  printf 'The managed runtime API build did not produce: %s\n' "$assembly" >&2
  exit 1
}

touch "$assembly"
