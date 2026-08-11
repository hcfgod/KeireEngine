#!/usr/bin/env bash
set -euo pipefail
umask 0022

PLATFORM="$1"
shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$ROOT/Scripts/Unix/common.sh"

GENERATOR=ninja
ARCHITECTURE="$(native_architecture)"
TOOLSET=default
TARGET=KeireClient
CI=0
UPDATE=0
FORCE=0
INSTALL_OPTIONAL=0
ALLOW_DIRTY=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"
managed_host_entitlements="$ROOT/Config/Signing/KeireManagedHost.entitlements"

common=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $CI -eq 1 ]] && common+=(--ci)
[[ $UPDATE -eq 1 ]] && common+=(--update)
[[ $FORCE -eq 1 ]] && common+=(--force)
[[ $ALLOW_DIRTY -eq 1 ]] && common+=(--allow-dirty)
bash "$ROOT/Scripts/Unix/package-editor.sh" "$PLATFORM" "${common[@]}"
[[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"

os_name=linux
[[ "$PLATFORM" == Mac ]] && os_name=macos
distribution_name="$ARTIFACT_PREFIX-editor-$os_name-$ARCHITECTURE-Dist"
distribution="$ROOT/Build/Distributions/$distribution_name"
validate_editor_package_stage "$distribution" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" \
  "$PROJECT_NAMESPACE" "$PLATFORM"

temporary_root="$(mktemp -d)"
trap 'rm -rf "$temporary_root"' EXIT

sha256_artifact() {
    local artifact="$1" digest
    local checksum="$artifact.sha256"
    if command -v sha256sum >/dev/null 2>&1; then
        digest="$(sha256sum "$artifact" | awk '{print $1}')"
    else
        digest="$(shasum -a 256 "$artifact" | awk '{print $1}')"
    fi
    printf '%s  %s\n' "$digest" "$(basename "$artifact")" > "$checksum"
    printf '==> Installer checksum created: %s\n' "$checksum"
}

xml_escape() {
    printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' \
      -e 's/"/\&quot;/g' -e "s/'/\&apos;/g"
}

if [[ "$PLATFORM" == Mac ]]; then
    command -v hdiutil >/dev/null 2>&1 || { printf 'hdiutil is required to create the macOS installer.\n' >&2; exit 1; }
    command -v sips >/dev/null 2>&1 || { printf 'sips is required to create the macOS application icon.\n' >&2; exit 1; }
    command -v iconutil >/dev/null 2>&1 || { printf 'iconutil is required to create the macOS application icon.\n' >&2; exit 1; }

    dmg_root="$temporary_root/dmg"
    app="$dmg_root/$PROJECT_DISPLAY_NAME Editor.app"
    contents="$app/Contents"
    resources="$contents/Resources"
    payload="$resources/Editor"
    mkdir -p "$contents/MacOS" "$payload"
    cp -R "$distribution/." "$payload/"
    validate_macos_macho_minimum "$payload" "$macos_deployment_target" \
      "$payload/bin/Managed/Dotnet"

    printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
      'macos_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
      "exec \"\$macos_dir/../Resources/Editor/bin/$CLIENT_TARGET\" \"\$@\"" \
      > "$contents/MacOS/$CLIENT_TARGET"
    chmod +x "$contents/MacOS/$CLIENT_TARGET"

    display_name_xml="$(xml_escape "$PROJECT_DISPLAY_NAME Editor")"
    bundle_id="org.keire.$(printf '%s' "$ARTIFACT_PREFIX" | tr '[:upper:]' '[:lower:]').editor"
    cat > "$contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$CLIENT_TARGET</string>
    <key>CFBundleIconFile</key>
    <string>KeireEditor.icns</string>
    <key>CFBundleIdentifier</key>
    <string>$bundle_id</string>
    <key>CFBundleName</key>
    <string>$display_name_xml</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$PROJECT_VERSION</string>
    <key>CFBundleVersion</key>
    <string>$PROJECT_VERSION</string>
    <key>LSMinimumSystemVersion</key>
    <string>$macos_deployment_target</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

    iconset="$temporary_root/Keire.iconset"
    mkdir -p "$iconset"
    for specification in \
      '16 icon_16x16.png' '32 icon_16x16@2x.png' '32 icon_32x32.png' '64 icon_32x32@2x.png' \
      '128 icon_128x128.png' '256 icon_128x128@2x.png' '256 icon_256x256.png' \
      '512 icon_256x256@2x.png' '512 icon_512x512.png' '1024 icon_512x512@2x.png'; do
        read -r size filename <<< "$specification"
        sips -z "$size" "$size" "$ROOT/Config/Branding/Keire.png" --out "$iconset/$filename" >/dev/null
    done
    iconutil -c icns "$iconset" -o "$resources/KeireEditor.icns"
    ln -s /Applications "$dmg_root/Applications"

    if [[ -n "${KEIRE_MACOS_SIGNING_IDENTITY:-}" ]]; then
        sign_macos_app_inside_out "$app" "$payload" "$KEIRE_MACOS_SIGNING_IDENTITY" \
          "$payload/bin/$CLIENT_TARGET" "$managed_host_entitlements" "$temporary_root"
    fi

    artifact="$ROOT/Artifacts/$ARTIFACT_PREFIX-editor-macos-$ARCHITECTURE-$PROJECT_VERSION.dmg"
    rm -f "$artifact" "$artifact.sha256"
    hdiutil create -ov -format UDZO -fs HFS+ -volname "$PROJECT_DISPLAY_NAME Editor" \
      -srcfolder "$dmg_root" "$artifact"
    hdiutil verify "$artifact"

    if [[ -n "${KEIRE_MACOS_NOTARY_PROFILE:-}" ]]; then
        [[ -n "${KEIRE_MACOS_SIGNING_IDENTITY:-}" ]] || {
          printf 'KEIRE_MACOS_NOTARY_PROFILE requires KEIRE_MACOS_SIGNING_IDENTITY.\n' >&2
          exit 1
        }
        xcrun notarytool submit "$artifact" --keychain-profile "$KEIRE_MACOS_NOTARY_PROFILE" --wait
        xcrun stapler staple "$artifact"
        xcrun stapler validate "$artifact"
    fi

    sha256_artifact "$artifact"
    printf '==> macOS editor installer created: %s\n' "$artifact"
    exit 0
fi

[[ "$PLATFORM" == Linux ]] || { printf "Unsupported installer platform '%s'.\n" "$PLATFORM" >&2; exit 1; }
command -v dpkg-deb >/dev/null 2>&1 || {
  printf 'dpkg-deb is required to create the Linux installer. Install the dpkg tooling and retry.\n' >&2
  exit 1
}

case "$(normalize_architecture "$ARCHITECTURE")" in
    x86_64) deb_architecture=amd64 ;;
    ARM64) deb_architecture=arm64 ;;
esac

package_root="$temporary_root/package"
install_relative="opt/$ARTIFACT_PREFIX-editor"
install_root="$package_root/$install_relative"
mkdir -p "$package_root/DEBIAN" "$install_root" "$package_root/usr/bin" \
  "$package_root/usr/share/applications" "$package_root/usr/share/icons/hicolor/256x256/apps"
cp -R "$distribution/." "$install_root/"
cat > "$package_root/usr/bin/$ARTIFACT_PREFIX-editor" <<EOF
#!/usr/bin/env sh
set -eu
exec "/$install_relative/bin/$CLIENT_TARGET" "\$@"
EOF
chmod 0755 "$package_root/usr/bin/$ARTIFACT_PREFIX-editor"
cp "$ROOT/Config/Branding/Keire.png" \
  "$package_root/usr/share/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-editor.png"

cat > "$package_root/usr/share/applications/$ARTIFACT_PREFIX-editor.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=$PROJECT_DISPLAY_NAME Editor
Comment=Edit an existing $PROJECT_DISPLAY_NAME project
Exec=/usr/bin/$ARTIFACT_PREFIX-editor
Icon=$ARTIFACT_PREFIX-editor
Terminal=false
Categories=Development;IDE;
StartupNotify=true
StartupWMClass=$CLIENT_TARGET
EOF

installed_size="$(du -sk "$install_root" | awk '{print $1}')"
cat > "$package_root/DEBIAN/control" <<EOF
Package: $ARTIFACT_PREFIX-editor
Version: $PROJECT_VERSION
Section: devel
Priority: optional
Architecture: $deb_architecture
Installed-Size: $installed_size
Depends: libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4
Maintainer: $PROJECT_DISPLAY_NAME Engine
Description: $PROJECT_DISPLAY_NAME versioned editor
 Includes the editor, companion tools, managed SDK, samples, documentation,
 and third-party notices. Install the standalone Hub separately.
EOF
chmod 0755 "$package_root/DEBIAN"
find "$package_root" -type d -exec chmod 0755 {} +

artifact="$ROOT/Artifacts/${ARTIFACT_PREFIX}-editor_${PROJECT_VERSION}_${deb_architecture}.deb"
rm -f "$artifact" "$artifact.sha256"
dpkg-deb --build --root-owner-group "$package_root" "$artifact"
dpkg-deb --info "$artifact" >/dev/null
[[ "$(dpkg-deb --field "$artifact" Depends)" == \
   'libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4' ]] || {
  printf 'Linux editor installer runtime dependencies are incomplete.\n' >&2
  exit 1
}

extracted="$temporary_root/extracted"
mkdir -p "$extracted"
dpkg-deb --extract "$artifact" "$extracted"
validate_editor_package_stage "$extracted/$install_relative" "$CLIENT_TARGET" "$HUB_TARGET" "$CORE_TARGET" \
  "$PROJECT_NAMESPACE" Linux
editor_launcher="$extracted/usr/bin/$ARTIFACT_PREFIX-editor"
[[ -f "$editor_launcher" && -x "$editor_launcher" && ! -L "$editor_launcher" ]] || {
  printf 'Linux editor installer is missing its command launcher.\n' >&2
  exit 1
}
expected_editor_exec="exec \"/$install_relative/bin/$CLIENT_TARGET\" \"\$@\""
grep -Fqx "$expected_editor_exec" "$editor_launcher" || {
  printf 'Linux editor installer command launcher does not resolve to its explicit /opt executable.\n' >&2
  exit 1
}
[[ -f "$extracted/usr/share/applications/$ARTIFACT_PREFIX-editor.desktop" ]] || {
  printf 'Linux editor installer is missing its desktop entry.\n' >&2
  exit 1
}
grep -Fqx "Exec=/usr/bin/$ARTIFACT_PREFIX-editor" \
  "$extracted/usr/share/applications/$ARTIFACT_PREFIX-editor.desktop" || {
  printf 'Linux editor desktop entry does not use the verified command wrapper.\n' >&2
  exit 1
}
[[ -f "$extracted/usr/share/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-editor.png" ]] || {
  printf 'Linux editor installer is missing its application icon.\n' >&2
  exit 1
}
for path in "usr/bin/$ARTIFACT_PREFIX-hub" \
  "usr/share/applications/$ARTIFACT_PREFIX-hub.desktop" \
  "usr/share/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-hub.png"; do
  [[ ! -e "$extracted/$path" ]] || {
    printf 'Linux editor installer contains standalone Hub-owned integration: %s\n' "$path" >&2
    exit 1
  }
done

# The editor package owns only its /opt root and editor-named desktop integration; the standalone Hub is untouched.
if [[ -e "$extracted/opt/$ARTIFACT_PREFIX-hub" ]]; then
  printf 'Linux editor installer contains the standalone Hub installation root.\n' >&2
  exit 1
fi

sha256_artifact "$artifact"
printf '==> Linux editor installer created: %s\n' "$artifact"
