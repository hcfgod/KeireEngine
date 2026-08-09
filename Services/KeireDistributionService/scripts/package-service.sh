#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd -- "$script_dir/.." && pwd)"
dotnet_command="${KEIRE_DOTNET:-dotnet}"
npm_command="${KEIRE_NPM:-npm}"
configuration="${KEIRE_CONFIGURATION:-Release}"
output_root="${1:-$service_root/../../Build/Distributions/KeireDistributionService}"
documentation_site="$service_root/DocumentationSite"
documentation_output="$documentation_site/dist"
if [[ $# -gt 0 ]]; then
  shift
fi

if [[ $# -eq 0 ]]; then
  case "$(uname -m)" in
    x86_64) runtime_identifiers=(linux-x64) ;;
    aarch64|arm64) runtime_identifiers=(linux-arm64) ;;
    *) printf 'Unsupported host architecture. Pass an explicit runtime identifier.\n' >&2; exit 2 ;;
  esac
else
  runtime_identifiers=("$@")
fi

ASTRO_TELEMETRY_DISABLED=1 "$npm_command" --prefix "$documentation_site" ci
ASTRO_TELEMETRY_DISABLED=1 "$npm_command" --prefix "$documentation_site" run build

mkdir -p -- "$output_root"
for runtime_identifier in "${runtime_identifiers[@]}"; do
  if [[ ! "$runtime_identifier" =~ ^(win|linux)-(x64|arm64)$ ]]; then
    printf "Unsupported runtime identifier '%s'.\n" "$runtime_identifier" >&2
    exit 2
  fi

  package_directory="$output_root/keire-distribution-$runtime_identifier"
  if [[ -e "$package_directory" ]]; then
    printf "Package destination already exists: '%s'. Choose a clean output directory.\n" "$package_directory" >&2
    exit 2
  fi

  mkdir -p -- "$package_directory"
  "$dotnet_command" publish "$service_root/Source/KeireDistributionService/KeireDistributionService.csproj" \
    --configuration "$configuration" --runtime "$runtime_identifier" --self-contained true \
    --output "$package_directory" -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
  "$dotnet_command" publish "$service_root/Source/KeireDistributionPublisher/KeireDistributionPublisher.csproj" \
    --configuration "$configuration" --runtime "$runtime_identifier" --self-contained true \
    --output "$package_directory/tools/publisher" -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true

  cp -- "$service_root/README.md" "$package_directory/"
  cp -- "$service_root/THIRD_PARTY_NOTICES.md" "$package_directory/"
  cp -R -- "$service_root/Licenses" "$package_directory/"
  cp -- "$documentation_site/node_modules/astro/LICENSE" "$package_directory/Licenses/Astro.txt"
  cp -- "$documentation_site/node_modules/@astrojs/starlight/LICENSE" "$package_directory/Licenses/Starlight.txt"
  cp -- "$documentation_site/node_modules/expressive-code/LICENSE" "$package_directory/Licenses/ExpressiveCode.txt"
  cp -- "$documentation_site/node_modules/beautiful-mermaid/LICENSE" "$package_directory/Licenses/BeautifulMermaid.txt"
  cp -R -- "$service_root/Website" "$package_directory/"
  mkdir -p -- "$package_directory/Website/docs"
  cp -R -- "$documentation_output/." "$package_directory/Website/docs/"
  mkdir -p -- "$package_directory/Deployment" "$package_directory/scripts"
  cp -- "$service_root/Deployment/Caddyfile.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/appsettings.Production.example.json" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-distribution.service.example" "$package_directory/Deployment/"
  cp -- "$service_root/scripts/health-check.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/health-check.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/monitor-distribution.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/monitor-distribution.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/backup-distribution.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/backup-distribution.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/backup-distribution-rclone.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/backup-distribution-rclone.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/restore-distribution.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/restore-distribution.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/restore-distribution-rclone.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/restore-distribution-rclone.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/publish-snapshot.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/publish-snapshot.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/start-wsl2-host-bridge.sh" "$package_directory/scripts/"
  cp -- "$service_root/scripts/install-wsl2-host-bridge.sh" "$package_directory/scripts/"
  chmod +x -- "$package_directory/scripts/health-check.sh" "$package_directory/scripts/monitor-distribution.sh" \
    "$package_directory/scripts/backup-distribution.sh" "$package_directory/scripts/backup-distribution-rclone.sh" \
    "$package_directory/scripts/restore-distribution.sh" "$package_directory/scripts/restore-distribution-rclone.sh" \
    "$package_directory/scripts/publish-snapshot.sh" "$package_directory/scripts/start-wsl2-host-bridge.sh" \
    "$package_directory/scripts/install-wsl2-host-bridge.sh"

  archive="$package_directory.tar.gz"
  tar -czf "$archive" -C "$output_root" "$(basename -- "$package_directory")"
  printf 'Created %s\n' "$archive"
done
