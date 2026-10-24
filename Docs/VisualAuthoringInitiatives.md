# VFX And Shader Graph Initiatives

This is a capability and acceptance guide, not a list of promises inferred from another engine's UI.
For current user workflows, start with [Materials](Manual/MaterialGraph.md), [Shader Graph](Manual/ShaderGraph.md),
and [VFX Graph](Manual/VfxGraph.md).

Shader Graph owns shader logic. Materials own shader selection, reflected property overrides, and surface settings in
the Inspector. Historical graph-owned Materials remain a compatibility concern, not the primary material workflow.
All these authoring sources ultimately feed immutable runtime shader/material assets.

## Executive Baseline

| Area | Current workflow | Do not infer |
| --- | --- | --- |
| Materials | Create Material using shared Kéire/Lit; edit in Inspector; assign to a consumer | That a new Material requires a Material Graph canvas |
| Custom shaders | Create Shader Graph, save, then create Material from Shader | That a shader graph itself is assignable to a mesh slot |
| Reuse | Explicit functions/layers and eligible Shader Function extraction | That collapsing a comment creates an executable subgraph |
| Legacy conversion | Assets > Review Legacy Material Conversion..., then validated reviewed apply | That every old source is convertible or opening an asset converts it |
| VFX | Effect graph, explicit compile/save, scene VFX Emitter, backend-specific preview | That preview success establishes packaged-player performance |

The [generated VFX capability matrix](generated/VfxCapabilities.md) and
[machine-readable manifest](VfxParityManifest.json) own the parity rows and counts. Historical totals in planning notes
are not a replacement for those files. An enabled row must have implementation and test evidence; a nearby supported
node does not make a disabled row usable.

## Priority Contract

| Priority | Meaning |
| --- | --- |
| P0 | Required for representative production simulation, resources, events, geometry, or shader-output workflows |
| P1 | Common authoring, preview, and iteration capability |
| P2 | Specialized production depth after the basic workflow is reliable |
| Deferred | Requires an explicit portable design rather than imitation of a different renderer |

Keep release acceptance distinct from source availability. A source-backed feature can still require a target build,
runtime exercise, performance measurement, or recovery test before a release is accepted.

## Shader Graph Capability Matrix

| Capability | Source-backed behavior | Remaining acceptance boundary |
| --- | --- | --- |
| Create shader and Material | Project creation exposes Surface, UI, Fullscreen, VFX, and Custom Graphics templates and Material from Shader | Compute creation is disabled; program-target support is not universal execution support |
| Material properties | Reflected controls, surface settings, reset, compatible copy/paste, inactive-value retention, keyword selection | Pending shader loads and failed imports must not be mistaken for successful changes |
| Persistent variants | Material Instance inherits from a Material or another instance | Validate parent resolution and override compatibility |
| Function extraction | Eligible shader expression selection becomes a Shader Function asset and call | Output/parameter selection, invalid boundary, and filename conflicts must fail safely |
| Graph interaction | Selection, layout, comments, annotations, clipboard, framing, routing, and undo | Metadata organization must preserve executable results |
| Preview and diagnostics | Validated shader candidates retain last-good output on failure | Thumbnails do not execute UI documents or sample a camera framebuffer |
| Camera effects | Fullscreen Material slots before tonemapping, after tonemapping, and after UI | Scene depth/history and arbitrary multi-pass scheduling are not implied |
| Particle materials | Authored VFX materials feed Sprite/Ribbon and existing mesh consumers | Volumetric output retains built-in density shading; validate supported backend behavior |
| Project conversion | Reviewed legacy conversion stages source/catalog changes and retains backups | Unsupported materials remain legacy; target-player testing is a separate gate |
| Shared shaders | Project-pinned library and Copy to Project for editable copies | Do not silently replace pinned content with current templates |

This table describes the workflow boundary, not an exhaustive catalog of shader operations or VFX parity rows.
Use the open graph's typed palette and validation diagnostics for legal nodes and connections.

## Production Scenarios

### A Material Variant That Survives Reopening

Create a Material, create two instances from it, assign different colors, and place them on two meshes. Change a
non-overridden parent value, finish the Inspector edits, save the scene, and reopen everything. Both instances should
inherit the parent change while retaining their colors. See [the worked example](Manual/MaterialGraphExamples.md).

### A Recoverable Shader Failure

Save a valid graph and Material, then introduce an invalid draft. The diagnostic must explain the failure without
silently replacing the last-good executable shader. Repair the draft, apply pending node properties, save, and reopen.
Repeat in a player build to distinguish editor recovery from cooked-asset acceptance.

### A Camera Effect On The Actual Camera

Create the fullscreen tint example, assign its Material to **After Tonemapping**, and change Tint to white and then
blue. The rendered camera image should respond; the graph thumbnail cannot prove it. Verify save/reopen and the
intended camera, then test the target player.

### A Bounded VFX Effect

Start with a small burst or continuous stream. Compare **Effect Settings > Loop** with **Loop Preview**, check the
backend badge and dropped count, then save and assign a scene emitter. Test representative simultaneous emitters in
the target player. A GPU spawned estimate is not an exact alive count or a performance pass.

## Delivery Order

1. Fix failures in the complete create/edit/save/reopen/assign/build workflow before expanding the node catalog.
2. Close resource and target boundaries with explicit supported/unsupported behavior and focused tests.
3. Validate failure recovery, migration publication, and identity preservation.
4. Measure representative CPU/GPU workloads on stated hardware; do not substitute node counts for performance.
5. Expand specialized features only with executable examples and corresponding backend acceptance.

## Validation

Read-only VFX ledger checks from the repository root:

```powershell
python Scripts/Vfx/reconcile_vfx_manifest.py --check
python Scripts/Vfx/validate_vfx_parity_manifest.py
python Scripts/Vfx/generate_vfx_capabilities.py --check
python Scripts/Vfx/test_vfx_parity_tooling.py
```

For engine/editor validation, use the repository test launcher:

```powershell
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
```

Relevant authorities include `ShaderGraphPanelFunctions.cpp` for extraction eligibility,
`AssetInspectorPanel.cpp` and `MaterialInspectorPanel.cpp` for Material editing,
`EditorWorkspaceMaterialUpgrade.cpp` for conversion gates, and `VfxEffectPanel.cpp` for preview/settings behavior.
These are under `KeireClient/Source/Editor`. Runtime material consumer behavior is implemented under
`KeireCore/Source/Rendering`. The source audit behind this guide is not a claim that all these tests or scenarios
have been executed for the current release.
