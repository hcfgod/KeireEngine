#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$root/Scripts/Unix/generated-content-cache.sh"
source "$root/Scripts/Unix/shader-blob-cache.sh"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/source" "$fixture/tool" "$fixture/cache"
export SHADER_TEST_CALLS="$fixture/calls" SHADER_TEST_FAIL="$fixture/fail"
touch "$SHADER_TEST_CALLS"
compiler="$fixture/tool/compiler"
cat > "$compiler" <<'COMPILER'
#!/usr/bin/env bash
set -euo pipefail
printf 'compile\n' >> "$SHADER_TEST_CALLS"
[[ ! -f "$SHADER_TEST_FAIL" ]] || exit 7
arguments="$*"
while [[ $# -gt 0 ]]; do
  if [[ "$1" == -o ]]; then
    printf '%s\n' "$arguments" > "$2"
    exit 0
  fi
  shift
done
exit 8
COMPILER
chmod +x "$compiler"
printf 'library-v1' > "$fixture/tool/compiler.so"
printf '#include "nested.hlsli"\n' > "$fixture/source/main.hlsl"
printf '#include "leaf.hlsli"\n' > "$fixture/source/nested.hlsli"
printf 'leaf-v1\n' > "$fixture/source/leaf.hlsli"
printf 'visibility\n' > "$fixture/source/visibility.hlsl"

compile() {
  shader_blob_compile "$fixture/cache" "$compiler" "$fixture/source/${1:-main.hlsl}" \
    "$fixture/output" "${2:-fragment}" "${3:-PSMain}" "${4:-DXIL}" "$fixture/source"
}

expect_calls() {
  local actual
  actual="$(wc -l < "$SHADER_TEST_CALLS" | tr -d ' ')"
  [[ "$actual" == "$1" ]] || { printf 'Expected %s calls, got %s\n' "$1" "$actual" >&2; exit 1; }
}

compile
compile
expect_calls 1
compile visibility.hlsl compute CSMain
expect_calls 2
printf 'leaf-v2\n' > "$fixture/source/leaf.hlsli"
compile
compile visibility.hlsl compute CSMain
expect_calls 3
compile main.hlsl vertex VSMain
compile main.hlsl fragment PSOther
compile main.hlsl fragment PSMain SPIRV
expect_calls 6
printf 'library-v2' > "$fixture/tool/compiler.so"
compile
expect_calls 7
printf '\n# compiler revision\n' >> "$compiler"
compile
expect_calls 8

# Corruption is detected even when a cache key still matches.
for blob in "$fixture/cache"/*.blob; do printf 'corrupt' > "$blob"; done
compile
expect_calls 9
before="$(generated_content_hash_file "$fixture/output")"
printf 'leaf-v3\n' > "$fixture/source/leaf.hlsli"
touch "$SHADER_TEST_FAIL"
if compile; then printf 'Failed compiler was accepted\n' >&2; exit 1; fi
[[ "$(generated_content_hash_file "$fixture/output")" == "$before" ]]
rm "$SHADER_TEST_FAIL"
compile
expect_calls 11
compile
expect_calls 11
printf '#include "missing.hlsli"\n' > "$fixture/source/nested.hlsli"
if compile; then printf 'Missing include was accepted\n' >&2; exit 1; fi
expect_calls 11

# Global invalidation uses the same transitive closure and compiler libraries.
printf '#include "leaf.hlsli"\n' > "$fixture/source/nested.hlsli"
shader_blob_global_inputs "$compiler" "$fixture/source" "$fixture/dependencies" "$fixture/source/main.hlsl"
grep -Fq 'leaf.hlsli' "$fixture/dependencies"
grep -Fq 'compiler.so' "$fixture/dependencies"
grep -Fq 'shader-blob-cache.sh' "$fixture/dependencies"

# A previously prepared identity must not authorize a hit after source edits.
prepared_identity="$(shader_blob_source_identity "$compiler" "$fixture/source/main.hlsl" "$fixture/source" "$fixture")"
compile
before="$(generated_content_hash_file "$fixture/output")"
calls_before="$(wc -l < "$SHADER_TEST_CALLS" | tr -d ' ')"
printf 'leaf-v4\n' > "$fixture/source/leaf.hlsli"
if shader_blob_compile "$fixture/cache" "$compiler" "$fixture/source/main.hlsl" "$fixture/output" \
    fragment PSMain DXIL "$fixture/source" "$prepared_identity"; then
  printf 'Stale prepared identity reused a cached shader\n' >&2
  exit 1
fi
expect_calls "$calls_before"
[[ "$(generated_content_hash_file "$fixture/output")" == "$before" ]]
printf 'PASS shader blob cache: hits, isolated dependencies, variants, compiler/library invalidation, corruption and failure recovery.\n'
