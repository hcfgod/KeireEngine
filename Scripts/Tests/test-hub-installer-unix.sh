#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
launcher="$ROOT/Scripts/project.sh"
packager="$ROOT/Scripts/Unix/package-hub-installer.sh"

bash -n "$launcher" "$packager"
grep -q 'package-hub-installer)' "$launcher"
grep -Fq '$SCRIPT_DIR/Unix/package-hub-installer.sh' "$launcher"
grep -q 'Scripts/Unix/package-hub.sh' "$packager"
grep -q -- '--stage-only' "$packager"
grep -q 'validate_hub_package_stage' "$packager"
if grep -Eq 'Scripts/Unix/package-editor\.sh|validate_editor_package_stage|launch-editor\.sh' "$packager"; then
  printf 'The standalone Unix Hub installer must not stage or launch the editor package.\n' >&2
  exit 1
fi

grep -Fq 'Resources/Hub/bin/$HUB_TARGET' "$packager"
grep -Fq 'CFBundleExecutable' "$packager"
grep -Fq 'KEIRE_MACOS_SIGNING_IDENTITY' "$packager"
grep -Fq 'sign_macos_app_inside_out "$app" "$payload"' "$packager"
grep -Fq '"$KEIRE_MACOS_SIGNING_IDENTITY" "" ""' "$packager"
grep -Fq 'validate_macos_macho_minimum "$payload" "$macos_deployment_target"' "$packager"
grep -Fq '<string>$macos_deployment_target</string>' "$packager"
grep -Fq 'KEIRE_MACOS_NOTARY_PROFILE' "$packager"
grep -Fq 'notarytool submit' "$packager"
grep -Fq 'hdiutil create' "$packager"
grep -Fq 'install_relative="opt/$ARTIFACT_PREFIX-hub"' "$packager"
grep -Fq 'cat > "$package_root/usr/bin/$ARTIFACT_PREFIX-hub"' "$packager"
grep -Fq 'exec "/$install_relative/bin/$HUB_TARGET" "\$@"' "$packager"
grep -Fq 'expected_hub_exec=' "$packager"
grep -Fq 'dpkg-deb --field "$artifact" Depends' "$packager"
grep -Fq 'Exec=/usr/bin/$ARTIFACT_PREFIX-hub' "$packager"
grep -Fq 'Package: $ARTIFACT_PREFIX-hub' "$packager"
grep -Fq 'Depends: libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4' "$packager"
grep -Fq 'StartupWMClass=$HUB_TARGET' "$packager"
grep -Fq 'dpkg-deb --build' "$packager"
grep -Fq 'usr/share/applications' "$packager"
grep -Fq 'preferences, caches, and editor roots are preserved' "$packager"
if grep -Fq -- '--force --deep' "$packager"; then
  printf 'The macOS Hub installer must sign explicit nested code instead of using --deep.\n' >&2
  exit 1
fi
if grep -Fq 'KeireManagedHost.entitlements' "$packager"; then
  printf 'The native Hub must not receive managed editor-host entitlements.\n' >&2
  exit 1
fi
if grep -Fq 'ln -s "/$install_relative/launch-hub.sh"' "$packager"; then
  printf 'The standalone Unix Hub command must not symlink to a dirname-relative package launcher.\n' >&2
  exit 1
fi
if grep -Eq 'rm -rf.*(\$HOME|XDG_CONFIG_HOME|XDG_CACHE_HOME)|DEBIAN/(pre|post)rm' "$packager"; then
  printf 'The standalone Unix Hub installer must not remove per-user Hub or editor data.\n' >&2
  exit 1
fi

printf 'Unix standalone Hub installer checks passed.\n'
