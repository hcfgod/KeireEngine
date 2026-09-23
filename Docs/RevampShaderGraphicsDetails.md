# Shader graphics target lane — September 12, 2026

## September 22 runtime implementation follow-up

The camera now stores three fullscreen material slots. Renderer execution uses frame-workset scene-color copies,
the shared screen material shader cache, and replacement pipelines before tone mapping, after tone mapping, and
after camera UI. Fullscreen graphs use screen vertex ABI 4; generator 14/importer 25 invalidates old cached imports.
Scene Color is a new fragment-only graph node. Editor and runtime camera propagation, material authoring, and
same-target shader replacement are implemented. UI material creation is enabled alongside VFX/custom graphics
material creation for existing consumers. Billboard/ribbon shader execution and arbitrary custom render-pass
submission remain open. The historical notes below describe the earlier state.

Automated Debug regression: 49 cases / 1,519 assertions passed, including production imports for all graphics
templates plus a Scene Color tint graph, node target rejection, and camera persistence/reset checks. Native Windows
verification now confirms all three camera stages, Scene/Game/camera-preview output, material tint changes, undo,
save/restart persistence, Forward+ and Deferred Hybrid execution, incompatible-material fallback, and last-good output
during an invalid shader import. Material creation worker timings were 244 and 282 ms. Continuous drag-time flicker
is not declared resolved. Optimized tests passed 53 cases / 1,583 assertions; editor tests passed 126 / 14,184; the
focused ASan shader/material run passed 7 / 229.

Scene UI Documents now expose a saved UI Material reference and C# property. Game/player screen overlays are routed
through the camera GPU pass, so UI materials and after-UI camera effects share the actual rendered output. Camera UI
geometry scales to the physical render surface, preserving UVs and clip rectangles. Windows control verified UI material assignment, visible custom/default color differences, clear/undo, and saved
assignment after restart. After-UI gradient replacement hides the composed UI and undo restores it. ASan coverage
for UI lifecycle, material propagation, scaling, clipping, and incompatible submission passed 51 cases / 769 assertions,
also passed in optimized Dist. Native assignment of a fullscreen material to a UI Document preserved the visible HUD
using standard UI rendering; undo restored the custom shader.

The first cooked-player check exposed missing camera/UI material dependencies in the scene importer. Scene importer
9 and prefab importer 2 now collect those references and invalidate old dependency caches. The corrected root cook
contains 30 assets, including both materials. Windows control verified the actual cooked player shows the same
yellow Scene Color tint and blue custom UI as the editor. Focused scene/prefab dependency ASan tests passed
2 cases / 19 assertions. This is a local cooked-player check, not a published installer or SDK package validation.

## September 22 follow-up

The historical consumer trace below is not wholly current: retained UI now has a dedicated generated vertex ABI,
`ValidateRuntimeUiShader`, and material binding/pipeline reload in `RenderRuntimeUiMaterials.cpp`. Fullscreen scene
injection and custom graphics submission are still absent. A successful graph preview or backend compilation does
not establish those runtime workflows. VFX mesh-material rendering must also be distinguished from the built-in
billboard/ribbon paths.

The production-import regression now covers Lit, Unlit, Transparent, Decal, Hair, Eye, UI, Fullscreen, VFX, and Custom
Graphics templates with an exposed color parameter. All ten compiled, preserved target/property reflection, and
produced DXIL, SPIR-V, and MSL binaries for every declared pass: 1 case / 131 assertions. This is compilation evidence;
it is not graphics execution evidence on three platforms.

Actual compute dispatch/readback passed separately on Windows D3D12 and Vulkan: 1 case / 3,112 assertions each,
including direct/indirect work and lifecycle/error checks. Compute remains unavailable in the editor creation menu.
The existing shader editor selection passed 87 cases / 13,722 assertions.

Windows control created `QA_FullscreenGradient` in the disposable `MaterialAnimationUserTest0922` project, added UV0
and Linear Gradient nodes, wired them to the fullscreen color output, and exercised Save and graph Undo/Redo.
The preview showed the horizontal gradient. Additional target fixtures and a vertical gradient are retained in that
project for reopen checks. New nodes were initially placed outside the visible canvas; creation now requests framing
through the existing node-focus path. The compiling status also no longer promises a scene update for all targets.

Logs: `Build/Validation/shader-targets-compiler-tests.log`, `shader-targets-compute-d3d12.log`,
`shader-targets-compute-vulkan.log`, and `shader-targets-editor-tests.log`. No Metal execution, packaged-player parity,
fullscreen scene effect, or arbitrary custom-pass execution is claimed by this follow-up.

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
