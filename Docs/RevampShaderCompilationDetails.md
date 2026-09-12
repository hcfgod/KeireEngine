# Shader compilation authoring follow-up

This lane adds correctness fixes to the restored September 12 revamp baseline. Its changes were transferred from the
isolated worktree to the canonical checkout at the coordinator's direction. Existing seed work is not claimed here.

## Implementation

- Superseding Shader Graph edits now request cancellation of the running generation without waiting on the owner
  thread. A stopped generation exits after graph generation, before backend import. The variant loop also checks
  cancellation before starting each subsequent native import. The existing generation comparison still rejects
  stale results before publication.
- Runtime-source equality now requires a complete previously compiled executable variant set. First Save therefore
  compiles native programs even when the initial CPU-generated source has not changed.
- Runtime reuse compares normalized generated manifests, including pass, target, resource, include-root, and render
  state contracts. Property defaults and presentation metadata are excluded; binding identity/type/texture semantic
  checks remain explicit. Parameter-only immediate updates retain the existing zero-compilation path.
- CPU-only regeneration invalidates executable variants when the program contract changes, preventing later Save
  from matching new generated metadata against retained binaries from the prior contract.
- Generated HLSL contains include paths rather than include contents. Until executable dependency snapshots exist,
  include-dependent graph regeneration and Save conservatively invoke native import. A broken include blocks Save
  and retains the previous successful compilation and executable variants; repairing it permits publication.

## Focused coverage and validation

Four tests were added to `KeireEditorTests/Source/ShaderGraphDocumentTests.cpp`:

- First Save compiles executable variants; a repeated unchanged Save reuses them.
- CPU-only generated-source relocation changes the manifest without changing HLSL and invalidates old executables.
- An include-reader barrier holds a generation while an edit supersedes it; its include-read count proves that the
  cancelled generation never enters backend import, and only the latest graph publishes.
- Changing a custom include to a native preprocessor error rejects Save, retains last-good variants, and recovers
  after repair.

LLVM clang-format formatting and `--dry-run --Werror` passed for both changed C++ files. `git diff --check` passed.
`python -X utf8 Scripts/Tests/check-source-budgets.py` passed across 1,468 first-party files in the canonical checkout.
No native build or test executable was run by this worker: the coordinator owns serialized build access and the
validation lane currently owns the runtime interval. Debug and DebugASan execution remain required for these changes.
No new SDK, packaged-player, Windows UI, Linux/Vulkan, macOS/Metal, or GPU measurements are claimed.

## Remaining gates

Cancellation does not interrupt an already running contextual importer/compiler process. Close and synchronous Save
can still wait for that process. Per-stage process cancellation and bounded shutdown require follow-up integration.
Include handling currently favors correctness over warm compilation performance; it is not a complete dependency
snapshot cache. Source inspection found no production caller of `ShaderCompileWorkKey`, so the public canonical
manifest helper does not itself establish graph/code pipeline cache integration. Full common ProgramArtifact,
reflection, dependency, variant, target-consumer, and measured value-edit acceptance gates remain with their owners.
No core ProgramArtifact/reflection contract or public ABI was changed in this lane.

## File manifest

- `KeireClient/Source/Editor/ShaderGraphDocument.cpp`: cancellation, executable reuse, manifest/include invalidation.
- `KeireEditorTests/Source/ShaderGraphDocumentTests.cpp`: the four focused regressions above and explicit test includes.
- `Docs/RevampShaderCompilationDetails.md`: this evidence and the proposed integration text below.

## Proposed shared documentation additions

README / shader authoring workflow:

> Shader Graph Save validates executable variants, including changes to custom includes, before persisting the graph.
> Failed validation keeps the last successful preview revision. Ordinary parameter-only updates reuse executable code.

Architecture:

> Shader Graph cancellation is cooperative at graph-generation/backend-import and variant boundaries. Runtime reuse
> requires executable variants and equivalent source, binding, and generated-manifest contracts; include-dependent
> regeneration conservatively reimports until executable dependency snapshots are available.

Changelog:

> Fixed Shader Graph first-save compilation, obsolete-generation backend work, and stale executable reuse after
> generated-manifest or custom-include changes. Parameter-only updates retain their immediate reuse path.
