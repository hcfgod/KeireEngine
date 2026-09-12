# Compute compiler lane acceptance

This records additions to the restored September 12 seed. Work began in the isolated compute worktree and root
transferred the eight initial file increments to the canonical checkout after the user authorized that move.
Subsequent edits were made in `C:/Users/keith/Desktop/KéireEngine`.

## Implemented contract

`CompileShaderGraphProgram` can produce a compute source artifact for a one-dimensional, constant/arithmetic graph.
Its `Color` output writes `float4` elements to `KeireComputeOutput`, an `RWStructuredBuffer<float4>` at `u0, space1`.
The default template uses 64 threads in X and one in Y/Z. The kernel queries the buffer element count before writing;
excess X invocations return. Invocations with nonzero global Y/Z also return, preserving one writer per element.

Supported nodes are constants, add/subtract/multiply/divide, min/max, clamp, lerp, one-minus, absolute, floor, ceiling,
fraction, sine, cosine, and reroutes. Connected non-Color master inputs, graphics/custom nodes, parameters, external
resource declarations, and multidimensional thread groups fail with explicit diagnostics. This bounded subset does
not establish general compute graph authoring acceptance.

The manifest declares only `CSMain` and its compute storage binding. Reflection uses the existing ABI: compute stage,
write-only storage buffer, space 1, binding 0, array count 1, stride 16. No new ProgramArtifact schema fields were added.
Compute validation rejects missing/multiple kernel entry points, invalid dimensions or groups exceeding 1024 threads,
incorrect resource stages/access, unaligned or empty structured/storage strides, and binary reflection that differs
from the program reflection. A source artifact still fails cooked validation until it contains backend binaries.

The contextual Shader Graph importer explicitly rejects compute publication through the graphics ShaderAsset path.
The generated-shader version advances to 12 and the Shader Graph importer version to 23. Material pickers and creation
menus were not changed by this lane.

## Changed-file manifest

- `KeireCore/Include/Keire/Rendering/ShaderGraph.h`: generated source contract version.
- `KeireCore/Source/Rendering/ShaderGraphCompilation.cpp`: remove blanket compute code-generation rejection.
- `KeireCore/Source/Rendering/ShaderGraphHlsl.cpp`: bounded compute arithmetic lowering.
- `KeireCore/Source/Rendering/ShaderGraphTemplates.cpp`: default one-dimensional group.
- `KeireCore/Source/Rendering/ShaderGraphManifest.cpp`: compute-only manifest branch.
- `KeireCore/Source/Rendering/ShaderGraphImporters.cpp`: explicit graphics-import guard and importer version.
- `KeireCore/Source/Rendering/ProgramArtifact.cpp`: output reflection and compute contract validation.
- `KeireTests/Source/Rendering/ProgramArtifactTests.cpp`: output binding, group/resource rejection, cooked reflection tests.
- `KeireTests/Source/Rendering/ShaderGraphTests.cpp`: typed arithmetic, source roundtrip, import rejection, version tests.
- `Docs/RevampComputeCompilerLane.md`: this evidence and integration proposals.

The source/test files already contained seed changes. Only the additions described above belong to this lane.

## Evidence and unresolved acceptance

Executed successfully on the canonical checkout: clang-format on all nine changed C++ files; clang-format
`--dry-run --Werror` on the same files; scoped `git diff --check` on the same files. No native build, test executable,
ASan, Release, GPU, package, SDK, or alternate-platform run was performed by this lane. Root coordinates native
validation after the shared UI validation interval and under the shared exclusive build lock.

Focused tests to run include `compute graph*`, `compute binary*`, and the existing Shader Graph template/schema tests.
The synthetic three-byte binary in the container validation test is solely a digest/reflection fixture and is never
submitted to a backend. It does not prove executable backend acceptance.

Remaining gates include general graph resources and access operations, invocation/group IDs, atomics/barriers,
texture operations, graph catalog/import/cook publication, dispatch/indirect integration, managed ownership,
device-loss/reload behavior, real backend output, packaging and both SDK consumers. Other lanes may implement some
of these; their evidence must be integrated explicitly rather than inferred from this compiler work.

## Proposed integration documentation

README: Compute source graphs currently support a bounded arithmetic-to-float4-buffer kernel through
`CompileShaderGraphProgram`; use cooked compute artifacts for execution. Contextual graphics ShaderAsset import does
not publish compute graphs.

Architecture: Compute graph compilation emits a separate compute-only stage/resource contract. Its storage output
uses SDL-compatible writable space 1, and compiled binary reflection must match the canonical program reflection.
Material assignment and graphics ShaderAsset publication remain separate ownership boundaries.

Changelog: Added bounded one-dimensional compute graph HLSL generation with reflected float4 storage output and
validation of kernel dimensions, resource access and backend reflection compatibility.
