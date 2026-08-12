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
TARGET=KeireHub
CI=0
UPDATE=0
FORCE=0
INSTALL_OPTIONAL=0
ALLOW_DIRTY=0
parse_build_arguments "$@"
load_project_config "$ROOT"
TOOLSET="$(resolve_unix_toolset "$PLATFORM" "$TOOLSET")"
macos_deployment_target="$(config_value "$ROOT/Config/Dependencies.lock" MACOS_DEPLOYMENT_TARGET)"

common=(--generator "$GENERATOR" --architecture "$ARCHITECTURE" --toolset "$TOOLSET")
[[ $CI -eq 1 ]] && common+=(--ci)
[[ $UPDATE -eq 1 ]] && common+=(--update)
[[ $FORCE -eq 1 ]] && common+=(--force)
[[ $ALLOW_DIRTY -eq 1 ]] && common+=(--allow-dirty)
bash "$ROOT/Scripts/Unix/package-hub.sh" "$PLATFORM" "${common[@]}" --stage-only
[[ "$PLATFORM" == Linux ]] && activate_linux_toolchain "$ROOT" "$TOOLSET"

os_name=linux
[[ "$PLATFORM" == Mac ]] && os_name=macos
distribution_name="$ARTIFACT_PREFIX-hub-$os_name-$ARCHITECTURE-Dist"
distribution="$ROOT/Build/Distributions/$distribution_name"
validate_hub_package_stage "$distribution" "$HUB_TARGET" "$CLIENT_TARGET" "$PROJECT_NAMESPACE" "$PLATFORM"

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
    command -v hdiutil >/dev/null 2>&1 || {
      printf 'hdiutil is required to create the macOS Hub installer.\n' >&2; exit 1;
    }
    command -v sips >/dev/null 2>&1 || {
      printf 'sips is required to create the macOS Hub application icon.\n' >&2; exit 1;
    }
    command -v iconutil >/dev/null 2>&1 || {
      printf 'iconutil is required to create the macOS Hub application icon.\n' >&2; exit 1;
    }

    dmg_root="$temporary_root/dmg"
    app="$dmg_root/$PROJECT_DISPLAY_NAME Hub.app"
    contents="$app/Contents"
    resources="$contents/Resources"
    payload="$resources/Hub"
    mkdir -p "$contents/MacOS" "$payload"
    cp -R "$distribution/." "$payload/"
    validate_macos_macho_minimum "$payload" "$macos_deployment_target" \
      "$payload/bin/Managed/Dotnet"

    printf '%s\n' '#!/usr/bin/env sh' 'set -eu' \
      'macos_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' \
      "exec \"\$macos_dir/../Resources/Hub/bin/$HUB_TARGET\" \"\$@\"" \
      > "$contents/MacOS/$HUB_TARGET"
    chmod +x "$contents/MacOS/$HUB_TARGET"

    display_name_xml="$(xml_escape "$PROJECT_DISPLAY_NAME Hub")"
    bundle_id="org.keire.$(printf '%s' "$ARTIFACT_PREFIX" | tr '[:upper:]' '[:lower:]').hub"
    cat > "$contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$HUB_TARGET</string>
    <key>CFBundleIconFile</key>
    <string>Keire.icns</string>
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
    iconutil -c icns "$iconset" -o "$resources/Keire.icns"
    ln -s /Applications "$dmg_root/Applications"

    if [[ -n "${KEIRE_MACOS_SIGNING_IDENTITY:-}" ]]; then
        sign_macos_app_inside_out "$app" "$payload" "$KEIRE_MACOS_SIGNING_IDENTITY" "" "" \
          "$temporary_root"
    fi

    artifact="$ROOT/Artifacts/$ARTIFACT_PREFIX-hub-macos-$ARCHITECTURE-$PROJECT_VERSION.dmg"
    rm -f "$artifact" "$artifact.sha256"
    hdiutil create -ov -format UDZO -fs HFS+ -volname "$PROJECT_DISPLAY_NAME Hub" \
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
    printf '==> macOS standalone Hub installer created: %s\n' "$artifact"
    exit 0
fi

[[ "$PLATFORM" == Linux ]] || { printf "Unsupported Hub installer platform '%s'.\n" "$PLATFORM" >&2; exit 1; }
install_relative="opt/$ARTIFACT_PREFIX-hub"
linux_root="$temporary_root/linux-root"
install_root="$linux_root/$install_relative"
mkdir -p "$install_root" "$linux_root/usr/bin" "$linux_root/usr/share/applications" \
  "$linux_root/usr/share/icons/hicolor/256x256/apps"
cp -a "$distribution/." "$install_root/"
cat > "$linux_root/usr/bin/$ARTIFACT_PREFIX-hub" <<EOF
#!/usr/bin/env sh
set -eu
exec "/$install_relative/bin/$HUB_TARGET" "\$@"
EOF
chmod 0755 "$linux_root/usr/bin/$ARTIFACT_PREFIX-hub"
cp "$ROOT/Config/Branding/Keire.png" \
  "$linux_root/usr/share/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-hub.png"

cat > "$linux_root/usr/share/applications/$ARTIFACT_PREFIX-hub.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=$PROJECT_DISPLAY_NAME Hub
Comment=Manage $PROJECT_DISPLAY_NAME projects, editors, templates, and components
Exec=/usr/bin/$ARTIFACT_PREFIX-hub
Icon=$ARTIFACT_PREFIX-hub
Terminal=false
Categories=Development;IDE;
StartupNotify=true
StartupWMClass=$HUB_TARGET
EOF

find "$linux_root" -type d -exec chmod 0755 {} +
find "$linux_root" -type f ! -path "$linux_root/$install_relative/*" -exec chmod 0644 {} +
chmod 0755 "$linux_root/usr/bin/$ARTIFACT_PREFIX-hub"

source_date_epoch="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct)}"
[[ "$source_date_epoch" =~ ^[0-9]+$ ]] || {
  printf 'SOURCE_DATE_EPOCH must be a non-negative integer.\n' >&2
  exit 1
}
find "$linux_root" -exec touch -h -d "@$source_date_epoch" {} +
export SOURCE_DATE_EPOCH="$source_date_epoch"

resolve_linux_installer_format() {
    local requested="${KEIRE_LINUX_INSTALLER_FORMAT:-auto}"
    requested="${requested,,}"
    case "$requested" in
        deb|rpm) printf '%s' "$requested"; return ;;
        auto) ;;
        *) printf "Unsupported Linux installer format '%s'. Use auto, deb, or rpm.\n" "$requested" >&2; return 1 ;;
    esac

    local identities=""
    if [[ -r /etc/os-release ]]; then
        identities="$(. /etc/os-release; printf '%s %s' "${ID:-}" "${ID_LIKE:-}")"
    fi
    identities="${identities,,}"
    if [[ "$identities" =~ (debian|ubuntu) ]]; then
        printf 'deb'
    elif [[ "$identities" =~ (fedora|rhel|centos|rocky) ]]; then
        printf 'rpm'
    elif command -v dpkg-deb >/dev/null 2>&1 && ! command -v rpmbuild >/dev/null 2>&1; then
        printf 'deb'
    elif command -v rpmbuild >/dev/null 2>&1 && ! command -v dpkg-deb >/dev/null 2>&1; then
        printf 'rpm'
    else
        printf '%s\n' 'Could not select a native Linux installer format. Pass --linux-installer-format deb or rpm.' >&2
        return 1
    fi
}

validate_linux_installer_tree() {
    local extracted="$1"
    validate_hub_package_stage "$extracted/$install_relative" "$HUB_TARGET" "$CLIENT_TARGET" \
      "$PROJECT_NAMESPACE" Linux
    local hub_launcher="$extracted/usr/bin/$ARTIFACT_PREFIX-hub"
    [[ -f "$hub_launcher" && -x "$hub_launcher" && ! -L "$hub_launcher" ]] || {
      printf 'Linux Hub installer is missing its command launcher.\n' >&2
      return 1
    }
    local expected_hub_exec="exec \"/$install_relative/bin/$HUB_TARGET\" \"\$@\""
    grep -Fqx "$expected_hub_exec" "$hub_launcher" || {
      printf 'Linux Hub installer command launcher does not resolve to its explicit /opt executable.\n' >&2
      return 1
    }
    [[ -f "$extracted/usr/share/applications/$ARTIFACT_PREFIX-hub.desktop" ]] || {
      printf 'Linux Hub installer is missing its desktop entry.\n' >&2
      return 1
    }
    grep -Fqx "Exec=/usr/bin/$ARTIFACT_PREFIX-hub" \
      "$extracted/usr/share/applications/$ARTIFACT_PREFIX-hub.desktop" || {
      printf 'Linux Hub desktop entry does not use the verified command wrapper.\n' >&2
      return 1
    }
}

installer_format="$(resolve_linux_installer_format)"
printf '==> Selected native Linux Hub installer format: %s\n' "${installer_format^^}"

if [[ "$installer_format" == deb ]]; then
    command -v dpkg-deb >/dev/null 2>&1 || {
      printf 'dpkg-deb is required to create the DEB Hub installer. Install dpkg tooling and retry.\n' >&2
      exit 1
    }
    case "$(normalize_architecture "$ARCHITECTURE")" in
        x86_64) deb_architecture=amd64 ;;
        ARM64) deb_architecture=arm64 ;;
    esac

    package_root="$temporary_root/deb-package"
    mkdir -p "$package_root"
    cp -a "$linux_root/." "$package_root/"
    mkdir -p "$package_root/DEBIAN"
    installed_size="$(du -sk "$package_root/$install_relative" | awk '{print $1}')"
    cat > "$package_root/DEBIAN/control" <<EOF
Package: $ARTIFACT_PREFIX-hub
Version: $PROJECT_VERSION
Section: devel
Priority: optional
Architecture: $deb_architecture
Installed-Size: $installed_size
Depends: libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4, zenity
Maintainer: $PROJECT_DISPLAY_NAME Engine
Description: $PROJECT_DISPLAY_NAME standalone project and editor Hub
 Manages projects, independently installed editors, Build Support, templates,
 learning content, package tasks, and updates without bundling an editor.
EOF
    chmod 0755 "$package_root/DEBIAN"
    chmod 0644 "$package_root/DEBIAN/control"
    find "$package_root/DEBIAN" -exec touch -h -d "@$source_date_epoch" {} +

    artifact="$ROOT/Artifacts/${ARTIFACT_PREFIX}-hub_${PROJECT_VERSION}_${deb_architecture}.deb"
    rm -f "$artifact" "$artifact.sha256"
    dpkg-deb --build --root-owner-group "$package_root" "$artifact"
    dpkg-deb --info "$artifact" >/dev/null
    [[ "$(dpkg-deb --field "$artifact" Depends)" == \
       'libc6, libstdc++6, libgcc-s1, libcurl4t64 | libcurl4, zenity' ]] || {
      printf 'DEB Hub installer runtime dependencies are incomplete.\n' >&2
      exit 1
    }

    extracted="$temporary_root/deb-extracted"
    mkdir -p "$extracted"
    dpkg-deb --extract "$artifact" "$extracted"
    validate_linux_installer_tree "$extracted"
else
    for tool in rpmbuild rpm rpm2cpio cpio; do
        command -v "$tool" >/dev/null 2>&1 || {
          printf '%s is required to create and validate the RPM Hub installer. Install rpm-build, rpm, and cpio.\n' \
            "$tool" >&2
          exit 1
        }
    done
    case "$(normalize_architecture "$ARCHITECTURE")" in
        x86_64) rpm_architecture=x86_64 ;;
        ARM64) rpm_architecture=aarch64 ;;
    esac

    rpm_root="$temporary_root/rpmbuild"
    mkdir -p "$rpm_root/BUILD" "$rpm_root/BUILDROOT" "$rpm_root/RPMS" "$rpm_root/SOURCES" \
      "$rpm_root/SPECS" "$rpm_root/SRPMS"
    cp -a "$linux_root" "$rpm_root/SOURCES/package-root"
    spec="$rpm_root/SPECS/$ARTIFACT_PREFIX-hub.spec"
    cat > "$spec" <<EOF
Name:           $ARTIFACT_PREFIX-hub
Version:        $PROJECT_VERSION
Release:        1
Summary:        $PROJECT_DISPLAY_NAME standalone project and editor Hub
License:        MIT
URL:            https://keireengine.duckdns.org
BuildArch:      $rpm_architecture
Requires:       glibc
Requires:       libstdc++
Requires:       libgcc
Requires:       libcurl
Requires:       zenity

%description
Manages projects, independently installed editors, Build Support, templates,
learning content, package tasks, and updates without bundling an editor.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a %{_sourcedir}/package-root/. %{buildroot}/

%files
%defattr(-,root,root,-)
/$install_relative
%{_bindir}/$ARTIFACT_PREFIX-hub
%{_datadir}/applications/$ARTIFACT_PREFIX-hub.desktop
%{_datadir}/icons/hicolor/256x256/apps/$ARTIFACT_PREFIX-hub.png
EOF
    rpmbuild -bb --define "_topdir $rpm_root" --define '_build_id_links none' \
      --define '__os_install_post %{nil}' --define '_buildhost keire-release' \
      --define 'use_source_date_epoch_as_buildtime 1' --define 'clamp_mtime_to_source_date_epoch 1' \
      --target "$rpm_architecture" "$spec"
    mapfile -t built_rpms < <(find "$rpm_root/RPMS" -type f -name '*.rpm' -print)
    [[ ${#built_rpms[@]} -eq 1 ]] || {
      printf 'RPM packaging produced %s artifacts; expected exactly one.\n' "${#built_rpms[@]}" >&2
      exit 1
    }

    artifact="$ROOT/Artifacts/${ARTIFACT_PREFIX}-hub-${PROJECT_VERSION}-1.${rpm_architecture}.rpm"
    rm -f "$artifact" "$artifact.sha256"
    cp "${built_rpms[0]}" "$artifact"
    [[ "$(rpm -qp --queryformat '%{NAME}' "$artifact")" == "$ARTIFACT_PREFIX-hub" &&
       "$(rpm -qp --queryformat '%{VERSION}' "$artifact")" == "$PROJECT_VERSION" &&
       "$(rpm -qp --queryformat '%{ARCH}' "$artifact")" == "$rpm_architecture" ]] || {
      printf 'RPM Hub installer identity validation failed.\n' >&2
      exit 1
    }
    rpm_requirements="$(rpm -qp --requires "$artifact")"
    for requirement in glibc libstdc++ libgcc libcurl zenity; do
        grep -Eq "^${requirement}([[:space:](]|$)" <<< "$rpm_requirements" || {
          printf "RPM Hub installer is missing its '%s' runtime dependency.\n" "$requirement" >&2
          exit 1
        }
    done

    extracted="$temporary_root/rpm-extracted"
    mkdir -p "$extracted"
    (cd "$extracted" && rpm2cpio "$artifact" | cpio -idm --quiet --no-absolute-filenames)
    validate_linux_installer_tree "$extracted"
fi

# The native package owns only /opt and desktop integration; per-user preferences, caches, and editor roots are preserved.
sha256_artifact "$artifact"
printf '==> Linux %s standalone Hub installer created: %s\n' "${installer_format^^}" "$artifact"
