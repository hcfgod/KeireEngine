#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
launcher="$ROOT/Scripts/project.sh"
packager="$ROOT/Scripts/Unix/package-hub-installer.sh"

bash -n "$launcher" "$packager"
grep -q 'package-hub-installer)' "$launcher"
grep -Fq '$SCRIPT_DIR/Unix/package-hub-installer.sh' "$launcher"
grep -Fq -- '--linux-installer-format' "$launcher"
grep -Fq 'KEIRE_LINUX_INSTALLER_FORMAT="$LINUX_INSTALLER_FORMAT"' "$launcher"
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
grep -Fq 'cat > "$linux_root/usr/bin/$ARTIFACT_PREFIX-hub"' "$packager"
grep -Fq 'exec "/$install_relative/bin/$HUB_TARGET" "\$@"' "$packager"
grep -Fq 'expected_hub_exec=' "$packager"
grep -Fq -- "-name 'libcoreclrtraceptprovider.so' -delete" "$packager"
grep -Fq 'find "$install_root" -type f -exec chmod 0644 {} +' "$packager"
grep -Fq -- "-name '*.so.*'" "$packager"
grep -Fq 'hub_worker="${PROJECT_NAMESPACE}HubWorker"' "$packager"
grep -Fq 'chmod 0755 "$install_root/bin/$HUB_TARGET"' "$packager"
grep -Fq 'find "$extracted/$install_relative" -type f -perm /022 -print -quit' "$packager"
grep -Fq 'KEIRE_LINUX_INSTALLER_FORMAT:-auto' "$packager"
grep -Eq 'debian\|ubuntu' "$packager"
grep -Eq 'fedora\|rhel\|centos\|rocky\|suse\|opensuse' "$packager"
grep -Fq 'SOURCE_DATE_EPOCH' "$packager"
grep -Fq 'dpkg-deb --field "$artifact" Depends' "$packager"
grep -Fq 'Exec=/usr/bin/$ARTIFACT_PREFIX-hub' "$packager"
grep -Fq 'Package: $ARTIFACT_PREFIX-hub' "$packager"
grep -Fq 'Depends: libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4, pkexec, zenity' "$packager"
grep -Fq 'StartupWMClass=$HUB_TARGET' "$packager"
grep -Fq 'dpkg-deb --build' "$packager"
grep -Fq 'for tool in rpmbuild rpm rpm2cpio cpio' "$packager"
grep -Fq 'Requires:       libc.so.6()(64bit)' "$packager"
grep -Fq 'Requires:       libstdc++.so.6()(64bit)' "$packager"
grep -Fq 'Requires:       libgcc_s.so.1()(64bit)' "$packager"
grep -Fq 'Requires:       libcurl.so.4()(64bit)' "$packager"
grep -Fq 'Requires:       polkit' "$packager"
grep -Fq 'Requires:       zenity' "$packager"
grep -Fq 'rpmbuild -bb' "$packager"
grep -Fq -- "--define '__os_install_post %{nil}'" "$packager"
grep -Fq -- "--define '_buildhost keire-release'" "$packager"
grep -Fq -- "--define 'use_source_date_epoch_as_buildtime 1'" "$packager"
grep -Fq "rpm -qp --queryformat '%{NAME}'" "$packager"
grep -Fq 'grep -Fqx "$requirement" <<< "$rpm_requirements"' "$packager"
grep -Fq "grep -Fqx 'liblttng-ust.so.0()(64bit)'" "$packager"
grep -Fq 'rpm2cpio "$artifact" | cpio' "$packager"
grep -Fq '${ARTIFACT_PREFIX}-hub-${PROJECT_VERSION}-1.${rpm_architecture}.rpm' "$packager"
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
if grep -Eq 'rm -rf.*(\$HOME|XDG_CONFIG_HOME|XDG_CACHE_HOME)|DEBIAN/(pre|post)rm|%(pre|post)un' "$packager"; then
  printf 'The standalone Unix Hub installer must not remove per-user Hub or editor data.\n' >&2
  exit 1
fi

rpm_requirements=$'libc.so.6()(64bit)\nlibstdc++.so.6()(64bit)\nlibgcc_s.so.1()(64bit)\nlibcurl.so.4()(64bit)\npolkit\nzenity'
rpm_runtime_requirements=(
  'libc.so.6()(64bit)'
  'libstdc++.so.6()(64bit)'
  'libgcc_s.so.1()(64bit)'
  'libcurl.so.4()(64bit)'
  polkit
  zenity
)
for requirement in "${rpm_runtime_requirements[@]}"; do
  grep -Fqx "$requirement" <<< "$rpm_requirements" || {
    printf "The RPM dependency validator rejected the literal '%s' package name.\n" "$requirement" >&2
    exit 1
  }
done

printf 'Unix standalone Hub installer checks passed.\n'
