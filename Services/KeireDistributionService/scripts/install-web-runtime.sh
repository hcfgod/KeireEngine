#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
web_root="${1:-$script_dir/../Web}"
node_command="${KEIRE_NODE:-node}"
npm_command="${KEIRE_NPM:-npm}"
web_root="$(cd -- "$web_root" && pwd)"

for required_file in package.json package-lock.json dist/server/entry.mjs; do
  if [[ ! -f "$web_root/$required_file" ]]; then
    printf "The packaged web runtime is missing '%s'.\n" "$web_root/$required_file" >&2
    exit 2
  fi
done

"$node_command" -e '
const [major, minor] = process.versions.node.split(".").map(Number);
if (major < 22 || (major === 22 && minor < 12)) process.exit(2);
' || {
  printf 'Kéire Web requires Node.js 22.12.0 or newer.\n' >&2
  exit 2
}
npm_version="$("$npm_command" --version)"
"$node_command" -e '
const [major, minor, patch] = process.argv[1].split(".").map(Number);
if (major < 10 || (major === 10 && minor < 8) || (major === 10 && minor === 8 && patch < 2)) process.exit(2);
' "$npm_version" || {
  printf 'Kéire Web requires npm 10.8.2 or newer; found %s.\n' "$npm_version" >&2
  exit 2
}

"$npm_command" --prefix "$web_root" ci --omit=dev --ignore-scripts --no-audit --no-fund
"$node_command" --check "$web_root/dist/server/entry.mjs"
printf "Kéire Web runtime dependencies are installed in '%s'.\n" "$web_root"
