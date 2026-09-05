#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
for generator in build-info builtin-shaders builtin-skinning builtin-vfx builtin-occlusion builtin-spatial-selection; do
    bash "$script_directory/$generator.sh"
done
