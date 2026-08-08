#!/usr/bin/env bash
set -euo pipefail

configuration="${1:-Debug}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dotnet="$root/Build/Dependencies/dotnet-sdk/dotnet"

if [[ ! -x "$dotnet" ]]; then
    echo "The bundled .NET SDK was not found. Generate dependencies before running distribution service tests." >&2
    exit 1
fi

"$dotnet" run \
    --project "$root/Services/KeireDistributionService/tests/KeireDistributionService.Tests/KeireDistributionService.Tests.csproj" \
    --configuration "$configuration" \
    --nologo
