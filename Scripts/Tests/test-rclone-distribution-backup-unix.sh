#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixture="$(mktemp -d)"
previous_fake_root="${KEIRE_TEST_RCLONE_ROOT-}"
cleanup() {
  if [[ -n "$previous_fake_root" ]]; then
    export KEIRE_TEST_RCLONE_ROOT="$previous_fake_root"
  else
    unset KEIRE_TEST_RCLONE_ROOT
  fi
  rm -rf -- "$fixture"
}
trap cleanup EXIT

distribution="$fixture/distribution"
remote_storage="$fixture/remote"
restore="$fixture/restore"
mkdir -p -- "$distribution/snapshots/snapshot-a" "$remote_storage"
printf 'snapshot-a\n' > "$distribution/current"
printf 'immutable-payload' > "$distribution/snapshots/snapshot-a/payload.bin"
printf '[test-remote]\ntype = drive\n' > "$fixture/rclone.conf"

cat > "$fixture/fake-rclone.py" <<'PY'
import filecmp
import os
from pathlib import Path
import shutil
import sys

remote_root = Path(os.environ["KEIRE_TEST_RCLONE_ROOT"])
arguments = sys.argv[1:]
command = arguments.pop(0)
value_flags = {"--config", "--log-file", "--log-level"}
positionals = []
immutable = "--immutable" in arguments
index = 0
while index < len(arguments):
    value = arguments[index]
    if value.startswith("--"):
        index += 2 if value in value_flags else 1
    else:
        positionals.append(value)
        index += 1

def resolve(value: str) -> Path:
    prefix = "test-remote:"
    if value.startswith(prefix):
        return remote_root / value[len(prefix):].lstrip("/")
    return Path(value)

def copy_directory(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"missing source directory: {source}")
    for item in source.rglob("*"):
        if not item.is_file():
            continue
        target = destination / item.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and immutable and not filecmp.cmp(item, target, shallow=False):
            raise RuntimeError(f"immutable destination changed: {target}")
        if not target.exists() or not filecmp.cmp(item, target, shallow=False):
            shutil.copy2(item, target)

try:
    if command == "copy":
        copy_directory(resolve(positionals[0]), resolve(positionals[1]))
    elif command == "copyto":
        source = resolve(positionals[0])
        destination = resolve(positionals[1])
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    elif command == "check":
        source = resolve(positionals[0])
        destination = resolve(positionals[1])
        for item in source.rglob("*"):
            if item.is_file():
                target = destination / item.relative_to(source)
                if not target.is_file() or not filecmp.cmp(item, target, shallow=False):
                    raise RuntimeError(f"check failed: {item}")
    else:
        raise RuntimeError(f"unsupported fake rclone command: {command}")
except Exception as error:
    print(error, file=sys.stderr)
    sys.exit(3)
PY
cat > "$fixture/rclone" <<EOF
#!/usr/bin/env bash
exec python3 "$fixture/fake-rclone.py" "\$@"
EOF
cat > "$fixture/publisher" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == validate && "$2" == --root ]]
current="$(tr -d '\r\n' < "$3/current")"
[[ -n "$current" && -d "$3/snapshots/$current" ]]
EOF
chmod +x -- "$fixture/rclone" "$fixture/publisher"
export KEIRE_TEST_RCLONE_ROOT="$remote_storage"

backup_script="$root/Services/KeireDistributionService/scripts/backup-distribution-rclone.sh"
restore_script="$root/Services/KeireDistributionService/scripts/restore-distribution-rclone.sh"
KEIRE_DISTRIBUTION_PUBLISHER="$fixture/publisher" \
  "$backup_script" "$distribution" test-remote:backups "$fixture/rclone.conf" "$fixture/rclone"
[[ -f "$remote_storage/backups/snapshots/snapshot-a/payload.bin" ]]
[[ "$(find "$remote_storage/backups/records" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 1 ]]

KEIRE_DISTRIBUTION_PUBLISHER="$fixture/publisher" \
  "$backup_script" "$distribution" test-remote:backups "$fixture/rclone.conf" "$fixture/rclone"
[[ "$(find "$remote_storage/backups/records" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 2 ]]

KEIRE_DISTRIBUTION_PUBLISHER="$fixture/publisher" \
  "$restore_script" "$restore" test-remote:backups "$fixture/rclone.conf" '' "$fixture/rclone"
[[ "$(cat "$restore/snapshots/snapshot-a/payload.bin")" == immutable-payload ]]

printf 'mutated-payload' > "$distribution/snapshots/snapshot-a/payload.bin"
if KEIRE_DISTRIBUTION_PUBLISHER="$fixture/publisher" \
    "$backup_script" "$distribution" test-remote:backups "$fixture/rclone.conf" "$fixture/rclone" 2>/dev/null; then
  printf 'Mutated immutable snapshot was accepted by the remote backup.\n' >&2
  exit 1
fi

printf 'Unix rclone distribution backup checks passed.\n'
