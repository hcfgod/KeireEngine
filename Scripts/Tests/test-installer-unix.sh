#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
launcher="$ROOT/Scripts/project.sh"
packager="$ROOT/Scripts/Unix/package-installer.sh"

bash -n "$launcher" "$packager"
grep -q 'package-installer)' "$launcher"
grep -Fq '$SCRIPT_DIR/Unix/package-installer.sh' "$launcher"
grep -q 'Scripts/Unix/package-editor.sh' "$packager"
grep -q 'validate_editor_package_stage' "$packager"
grep -Fq 'resolve_existing_package_stage "$PLATFORM"' "$packager"
grep -Fq 'Reusing validated editor package stage' "$packager"
grep -q 'hdiutil create' "$packager"
grep -q 'Applications' "$packager"
grep -q 'KEIRE_MACOS_SIGNING_IDENTITY' "$packager"
grep -q 'managed_host_entitlements=' "$packager"
grep -Fq 'sign_macos_app_inside_out "$app" "$payload"' "$packager"
grep -Fq '"$payload/bin/$CLIENT_TARGET" "$managed_host_entitlements"' "$packager"
grep -Fq 'validate_macos_macho_minimum "$payload" "$macos_deployment_target"' "$packager"
grep -Fq '<string>$macos_deployment_target</string>' "$packager"
grep -q 'notarytool submit' "$packager"
grep -q 'dpkg-deb --build' "$packager"
grep -q 'usr/share/applications' "$packager"
grep -q 'usr/share/icons/hicolor/256x256/apps' "$packager"
grep -q 'sha256_artifact' "$packager"
grep -Fq '$PROJECT_DISPLAY_NAME Editor.app' "$packager"
grep -Fq 'Resources/Editor/bin/$CLIENT_TARGET' "$packager"
grep -Fq 'install_relative="opt/$ARTIFACT_PREFIX-editor"' "$packager"
grep -Fq 'usr/bin/$ARTIFACT_PREFIX-editor' "$packager"
grep -Fq 'exec "/$install_relative/bin/$CLIENT_TARGET" "\$@"' "$packager"
grep -Fq 'expected_editor_exec=' "$packager"
grep -Fq 'dpkg-deb --field "$artifact" Depends' "$packager"
grep -Fq 'Exec=/usr/bin/$ARTIFACT_PREFIX-editor' "$packager"
grep -Fq 'applications/$ARTIFACT_PREFIX-editor.desktop' "$packager"
grep -Fq 'apps/$ARTIFACT_PREFIX-editor.png' "$packager"
grep -Fq 'StartupWMClass=$CLIENT_TARGET' "$packager"
grep -Fq 'Depends: libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4' "$packager"
grep -Fq 'standalone Hub is untouched' "$packager"

source "$ROOT/Scripts/Unix/common.sh"
stage_fixture="$(mktemp -d)"
stage_link="${stage_fixture}-link"
trap 'rm -rf "$stage_fixture"; rm -f "$stage_link"' EXIT
[[ "$(KEIRE_EXISTING_PACKAGE_STAGE="$stage_fixture" resolve_existing_package_stage Linux)" == "$stage_fixture" ]]
if KEIRE_EXISTING_PACKAGE_STAGE=relative resolve_existing_package_stage Linux >/dev/null 2>&1; then
  printf 'The existing package stage must reject relative paths.\n' >&2
  exit 1
fi
if KEIRE_EXISTING_PACKAGE_STAGE="$stage_fixture" resolve_existing_package_stage Mac >/dev/null 2>&1; then
  printf 'The existing package stage must reject non-Linux installer assembly.\n' >&2
  exit 1
fi
ln -s "$stage_fixture" "$stage_link"
if KEIRE_EXISTING_PACKAGE_STAGE="$stage_link" resolve_existing_package_stage Linux >/dev/null 2>&1; then
  printf 'The existing package stage must reject a symbolic-link root.\n' >&2
  exit 1
fi

if grep -Fq -- '--force --deep' "$packager"; then
  printf 'The macOS editor installer must sign explicit nested code instead of using --deep.\n' >&2
  exit 1
fi
if grep -Fq 'ln -s "/$install_relative/launch-editor.sh"' "$packager"; then
  printf 'The Unix editor command must not symlink to a dirname-relative package launcher.\n' >&2
  exit 1
fi
if grep -Fq '$PROJECT_DISPLAY_NAME Hub.app' "$packager" ||
   grep -Fq 'package_root/usr/bin/$ARTIFACT_PREFIX-hub"' "$packager" ||
   grep -Fq 'package_root/usr/share/applications/$ARTIFACT_PREFIX-hub.desktop" <<EOF' "$packager" ||
   grep -Fq 'package_root/usr/share/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-hub.png"' "$packager"; then
  printf 'The Unix editor installer must not own standalone Hub launch or desktop integration.\n' >&2
  exit 1
fi

printf 'Unix installer checks passed.\n'
