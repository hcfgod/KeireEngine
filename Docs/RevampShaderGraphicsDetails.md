# Shader graphics target lane — September 12, 2026

This lane adds the following changes beyond the restored seed. GPU preview and runtime target acceptance remain open.

## Added implementation

- `KeireCore/Source/Rendering/ShaderGraphManifest.cpp`: UI programs now emit alpha blending, no depth testing or
  depth writes, and no face culling. VFX programs emit no face culling for two-sided particle geometry while retaining
  depth-tested transparent state. Fullscreen replacement, custom graphics, and historical material output states
  remain unchanged. The generated shader version must advance with these manifest changes to invalidate cached imports.
- `KeireClient/Source/Editor/ShaderGraphPanelPreview.cpp`: labels the existing CPU graph evaluation as an approximation,
  explicitly states that compiled GPU execution is absent, and names missing particle simulation/billboards,
  custom-pass inputs/scheduling, and fullscreen scene color/depth/injection. No GPU execution was added by this lane.
- `KeireTests/Source/Rendering/ShaderGraphGraphicsTargetTests.cpp`: two focused tests cover the five graphics targets'
  culling, depth, blend, and pass roles, plus preservation of the legacy transparent material contract.

The seed already supplied the flat UI/fullscreen CPU preview; this lane does not claim that implementation as new.

## Runtime consumer trace and required next implementation

`ShaderGraphPanelPreview.cpp` expands the last-good graph and passes it to `RenderShaderGraphPreview` in a CPU job.
`UiFrame::CreateImage` uploads those evaluated pixels. `ShaderGraphCompilation` holds HLSL/manifest strings and properties,
not an executable GPU asset. `IShaderGraphPanelController` exposes no renderer or compiled-asset resolver.

`RenderSystem::CreateView` and `UiFrame::Image(RenderSurface)` can display GPU offscreen rendering already. However,
`SceneRenderRequest` only accepts scenes, VFX snapshots, environment, and material values; it has no graphics-program
submission or fullscreen/custom-pass list. Reusing that surface alone would not execute the authored target.

For UI, `RenderDeviceFrame.cpp` selects embedded `BuiltinRuntimeUiVertex`/`BuiltinRuntimeUiFragment` binaries in
`CreateRuntimeUiShader`. `RuntimeUiRenderSubmission` has no shader/material override or target-program binding set.
A UI graph needs a retained-UI vertex/input ABI, property/resource binding, and a supported per-element/submission
program selection before its generated primary pass can replace those binaries.

For VFX, `RenderVfxDrawing.cpp` can bind a composed material pipeline for GPU mesh particles. The billboard/ribbon
path resolves only material tint and first texture, then binds the built-in `GpuVfx`/`GpuVfxRibbon` pipeline. Executing
the VFX graph requires particle-instance vertex inputs and graph parameter/resource bindings in that path, plus
equivalent CPU-particle behavior and appropriate lifetime/last-good handling.

For fullscreen/custom graphics, no renderer-owned submission list consumes the graph's `fullscreenInjectionPoint`
or `programTarget` metadata. Those fields currently occur in graph serialization/manifest generation and tests,
not renderer dispatch. Required work includes preserving target/injection in the imported runtime shader contract,
validating stage and resource layouts, scene color/depth inputs, frame-graph ordering/hazards, and retirement of
target pipelines/resources after reload and device loss. Core ProgramArtifact/reflection changes belong to the
compute lane and must be coordinated before introducing this ABI.

These are concrete missing execution paths. A generic mesh illustration, corrected raster-state manifest, or an
offscreen surface displaying CPU pixels does not close them.

## Validation

Performed in this isolated worktree:

- LLVM `clang-format -i` on the three changed C++ files above.
- `git diff --check`: passed after the implementation changes.

The lead coordinates native builds with the shared exclusive lock and two Ninja jobs. This lane did not run a
native build, test executable, Windows UI automation, GPU capture, ASan, Release, packaged player, SDK consumer,
Linux/Vulkan, or macOS/Metal validation. Test execution results must be appended only after those runs complete.

## Proposed integration documentation

README: Shader Graph previews currently show a bounded CPU approximation, with target-specific notes about inputs
and execution that are absent. Compiled GPU preview acceptance is pending for UI, VFX, fullscreen, and custom graphics.

CHANGELOG: Corrected generated UI shader blend/depth/culling state and made VFX shaders two-sided. Shader Graph preview
labels now identify CPU approximation and absent target-specific execution.

Architecture: GPU RenderViews are reusable offscreen surfaces, but graph preview currently evaluates on CPU. A future
target preview must submit the same imported runtime shader, reflected resources, and target pass contract used by
the corresponding player consumer; it must not introduce a second shader execution contract just for the editor.
