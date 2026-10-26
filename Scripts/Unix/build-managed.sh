#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dotnet="$root/Build/Dependencies/dotnet-sdk/dotnet"
project="$root/KeireEditorManaged/Keire.Editor.Managed.csproj"
generator_project="$root/KeireManaged.Generators/Keire.Managed.Generators.csproj"
design_time_project="$root/Scripts/ManagedDesignTime/Keire.Editor.Managed.DesignTime.csproj"
source_roots=("$root/KeireManaged" "$root/KeireEditorManaged" "$root/KeireManaged.Generators"
  "$root/Scripts/ManagedDesignTime")
output="$root/Build/Managed"
intermediate="$root/Build/Intermediates/Managed/"
assembly="$output/Keire.Managed.dll"
editor_assembly="$output/Keire.Editor.Managed.dll"
generator_assembly="$output/Keire.Managed.Generators.dll"
design_time_output="$output/DesignTime"
design_time_assembly="$design_time_output/Keire.Managed.dll"
design_time_editor_assembly="$design_time_output/Keire.Editor.Managed.dll"
required_outputs=("$assembly" "$editor_assembly" "$generator_assembly"
  "$design_time_assembly" "$design_time_editor_assembly")

managed_outputs_current() {
  local required_output oldest_output=""
  for required_output in "${required_outputs[@]}"; do
    [[ -f "$required_output" ]] || return 1
    if [[ "${BASH_SOURCE[0]}" -nt "$required_output" ||
      "$root/Config/Dependencies.lock" -nt "$required_output" ]]; then
      return 1
    fi
    if [[ -z "$oldest_output" || "$required_output" -ot "$oldest_output" ]]; then
      oldest_output="$required_output"
    fi
  done
  local source_root
  for source_root in "${source_roots[@]}"; do
    [[ -d "$source_root" ]] || return 1
  done
  [[ -z "$(find "${source_roots[@]}" \( -path '*/bin' -o -path '*/obj' -o -path '*/Build' \) -prune -o \
    \( -type d -o -type f \( -name '*.cs' -o -name '*.csproj' -o -name '*.props' -o -name '*.targets' -o -name '*.md' \) \) \
    -newer "$oldest_output" -print -quit)" ]]
}

if managed_outputs_current; then
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

"$dotnet" build "$design_time_project" --nologo --configuration Release --output "$design_time_output"

for required_output in "${required_outputs[@]}"; do
  [[ -f "$required_output" ]] || {
    printf 'The managed build did not produce: %s\n' "$required_output" >&2
    exit 1
  }
done

touch "${required_outputs[@]}"
