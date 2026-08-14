#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
service_root="$(cd -- "$script_dir/.." && pwd)"
repository_root="$(cd -- "$service_root/../.." && pwd)"
if [[ -n "${KEIRE_DOTNET:-}" ]]; then
  dotnet_command="$KEIRE_DOTNET"
elif [[ -x "$repository_root/Build/Dependencies/dotnet-sdk/dotnet" ]]; then
  dotnet_command="$repository_root/Build/Dependencies/dotnet-sdk/dotnet"
else
  dotnet_command=dotnet
fi
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
  "$dotnet_command" publish "$service_root/Source/KeireMarketplaceValidator/KeireMarketplaceValidator.csproj" \
    --configuration "$configuration" --runtime "$runtime_identifier" --self-contained true \
    --output "$package_directory/tools/marketplace-validator/worker" -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true
  "$dotnet_command" publish \
    "$service_root/Source/KeireMarketplaceValidatorBroker/KeireMarketplaceValidatorBroker.csproj" \
    --configuration "$configuration" --runtime "$runtime_identifier" --self-contained true \
    --output "$package_directory/tools/marketplace-validator/broker" -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true
  "$dotnet_command" publish \
    "$service_root/Source/KeireMarketplacePublicationSigner/KeireMarketplacePublicationSigner.csproj" \
    --configuration "$configuration" --runtime "$runtime_identifier" --self-contained true \
    --output "$package_directory/tools/marketplace-publication-signer" -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true

  cp -- "$service_root/README.md" "$package_directory/"
  cp -- "$service_root/THIRD_PARTY_NOTICES.md" "$package_directory/"
  cp -R -- "$service_root/Licenses" "$package_directory/"
  cp -- "$documentation_site/node_modules/astro/LICENSE" "$package_directory/Licenses/Astro.txt"
  cp -- "$documentation_site/node_modules/@astrojs/node/LICENSE" "$package_directory/Licenses/AstroNode.txt"
  cp -- "$documentation_site/node_modules/@astrojs/sitemap/LICENSE" "$package_directory/Licenses/AstroSitemap.txt"
  cp -- "$documentation_site/node_modules/@astrojs/starlight/LICENSE" "$package_directory/Licenses/Starlight.txt"
  cp -- "$documentation_site/node_modules/expressive-code/LICENSE" "$package_directory/Licenses/ExpressiveCode.txt"
  cp -- "$documentation_site/node_modules/beautiful-mermaid/LICENSE" "$package_directory/Licenses/BeautifulMermaid.txt"
  cp -- "$documentation_site/node_modules/@supabase/ssr/LICENSE" "$package_directory/Licenses/SupabaseSsr.txt"
  cp -- "$documentation_site/node_modules/@supabase/supabase-js/LICENSE" \
    "$package_directory/Licenses/SupabaseJavaScript.txt"
  cp -- "$documentation_site/node_modules/sharp/LICENSE" "$package_directory/Licenses/Sharp.txt"
  mkdir -p -- "$package_directory/Web"
  cp -R -- "$documentation_output" "$package_directory/Web/"
  cp -- "$documentation_site/package.json" "$documentation_site/package-lock.json" "$package_directory/Web/"
  mkdir -p -- "$package_directory/Deployment" "$package_directory/scripts"
  cp -- "$service_root/Deployment/Caddyfile.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/appsettings.Production.example.json" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-distribution.service.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-web.service.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-marketplace-validator.service.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-marketplace-validator-broker.service.example" \
    "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/keire-marketplace-publication-signer.service.example" \
    "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/marketplace-validator.env.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/marketplace-validator-broker.env.example" "$package_directory/Deployment/"
  cp -- "$service_root/Deployment/marketplace-publication-signer.env.example" \
    "$package_directory/Deployment/"
  if [[ "$runtime_identifier" == win-* ]]; then
    cp -- "$service_root/Deployment/configure-windows-validator-firewall.ps1" "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/install-windows-marketplace-validator-tasks.ps1" \
      "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/install-windows-marketplace-publication-signer-task.ps1" \
      "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/protect-windows-marketplace-secret.ps1" "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/provision-windows-marketplace-signing-keys.ps1" \
      "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/protect-windows-validator-broker-secret.ps1" "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/start-windows-marketplace-publication-signer.ps1" \
      "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/start-windows-marketplace-validator.ps1" "$package_directory/Deployment/"
    cp -- "$service_root/Deployment/start-windows-marketplace-validator-broker.ps1" "$package_directory/Deployment/"
  fi
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
  cp -- "$service_root/scripts/install-web-runtime.ps1" "$package_directory/scripts/"
  cp -- "$service_root/scripts/install-web-runtime.sh" "$package_directory/scripts/"
  chmod +x -- "$package_directory/scripts/health-check.sh" "$package_directory/scripts/monitor-distribution.sh" \
    "$package_directory/scripts/backup-distribution.sh" "$package_directory/scripts/backup-distribution-rclone.sh" \
    "$package_directory/scripts/restore-distribution.sh" "$package_directory/scripts/restore-distribution-rclone.sh" \
    "$package_directory/scripts/publish-snapshot.sh" "$package_directory/scripts/start-wsl2-host-bridge.sh" \
    "$package_directory/scripts/install-wsl2-host-bridge.sh" "$package_directory/scripts/install-web-runtime.sh"

  archive="$package_directory.tar.gz"
  tar -czf "$archive" -C "$output_root" "$(basename -- "$package_directory")"
  printf 'Created %s\n' "$archive"
done
