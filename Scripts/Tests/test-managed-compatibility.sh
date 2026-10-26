#!/usr/bin/env bash
set -euo pipefail

dotnet="${1:-dotnet}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root/Scripts/Tests/ManagedCompatibilityConsumer"
"$dotnet" --version
"$dotnet" run --project ManagedCompatibilityConsumer.csproj --configuration Release
