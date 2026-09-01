#!/usr/bin/env bash
set -euo pipefail

configuration="${1:-Debug}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dotnet="$root/Build/Dependencies/dotnet-sdk/dotnet"

if [[ ! -x "$dotnet" ]]; then
    echo "The bundled .NET SDK was not found. Generate dependencies before running managed API tests." >&2
    exit 1
fi

projects=(
    "$root/KeireManaged.Tests/Keire.Managed.Production.Tests.csproj"
    "$root/KeireEditorManaged.Tests/Keire.Editor.Managed.Production.Tests.csproj"
    "$root/KeireManaged.Generators.Tests/Keire.Managed.Generators.Production.Tests.csproj"
)
for project in "${projects[@]}"; do
    "$dotnet" run --project "$project" --configuration "$configuration" --nologo
done
