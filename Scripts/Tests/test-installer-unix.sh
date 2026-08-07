#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
launcher="$ROOT/Scripts/project.sh"
packager="$ROOT/Scripts/Unix/package-installer.sh"

bash -n "$launcher" "$packager"
grep -q 'package-installer)' "$launcher"
grep -q 'Scripts/Unix/package-installer.sh' "$launcher"
grep -q 'Scripts/Unix/package-editor.sh' "$packager"
grep -q 'validate_editor_package_stage' "$packager"
grep -q 'hdiutil create' "$packager"
grep -q 'Applications' "$packager"
grep -q 'KEIRE_MACOS_SIGNING_IDENTITY' "$packager"
grep -q 'notarytool submit' "$packager"
grep -q 'dpkg-deb --build' "$packager"
grep -q 'usr/share/applications' "$packager"
grep -q 'usr/share/icons/hicolor/256x256/apps' "$packager"
grep -q 'sha256_artifact' "$packager"

printf 'Unix installer checks passed.\n'
