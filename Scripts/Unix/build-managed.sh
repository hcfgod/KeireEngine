#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dotnet="$root/Build/Dependencies/dotnet-sdk/dotnet"
project="$root/KeireEditorManaged/Keire.Editor.Managed.csproj"
generator_project="$root/KeireManaged.Generators/Keire.Managed.Generators.csproj"
source_roots=("$root/KeireManaged" "$root/KeireEditorManaged" "$root/KeireManaged.Generators")
output="$root/Build/Managed"
intermediate="$root/Build/Intermediates/Managed/"
assembly="$output/Keire.Managed.dll"
editor_assembly="$output/Keire.Editor.Managed.dll"
generator_assembly="$output/Keire.Managed.Generators.dll"

if [[ -f "$assembly" && -f "$editor_assembly" && -f "$generator_assembly" ]] &&
  [[ ! "${BASH_SOURCE[0]}" -nt "$assembly" ]] &&
  [[ ! "${BASH_SOURCE[0]}" -nt "$editor_assembly" ]] &&
  [[ ! "${BASH_SOURCE[0]}" -nt "$generator_assembly" ]] &&
  [[ -z "$(find "${source_roots[@]}" \( -path '*/bin' -o -path '*/obj' -o -path '*/Build' \) -prune -o \
    \( -type d -o -type f \( -name '*.cs' -o -name '*.csproj' \) \) \
    -newer "$assembly" -print -quit)" ]] &&
  [[ -z "$(find "${source_roots[@]}" \( -path '*/bin' -o -path '*/obj' -o -path '*/Build' \) -prune -o \
    \( -type d -o -type f \( -name '*.cs' -o -name '*.csproj' \) \) \
    -newer "$editor_assembly" -print -quit)" ]] &&
  [[ -z "$(find "${source_roots[@]}" \( -path '*/bin' -o -path '*/obj' -o -path '*/Build' \) -prune -o \
    \( -type d -o -type f \( -name '*.cs' -o -name '*.csproj' \) \) \
    -newer "$generator_assembly" -print -quit)" ]]; then
  printf '%s\n' '==> Managed runtime API is current'
  exit 0
fi

[[ -x "$dotnet" ]] || {
  printf 'The bundled .NET SDK is missing or not executable: %s\n' "$dotnet" >&2
  exit 1
}

"$dotnet" build "$project" --nologo --configuration Release --output "$output" \
  "--property:BaseIntermediateOutputPath=$intermediate"

"$dotnet" build "$generator_project" --nologo --configuration Release --output "$output" \
  "--property:BaseIntermediateOutputPath=$root/Build/Intermediates/ManagedGenerators/"

[[ -f "$assembly" ]] || {
  printf 'The managed runtime API build did not produce: %s\n' "$assembly" >&2
  exit 1
}

[[ -f "$editor_assembly" ]] || {
  printf 'The managed editor API build did not produce: %s\n' "$editor_assembly" >&2
  exit 1
}

[[ -f "$generator_assembly" ]] || {
  printf 'The managed generator build did not produce: %s\n' "$generator_assembly" >&2
  exit 1
}

touch "$assembly"
touch "$editor_assembly"
touch "$generator_assembly"
