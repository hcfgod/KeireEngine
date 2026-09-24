# Shader Graph

A Shader Graph is a target-based renderer program. It defines legal stages, resources, reflected properties, keywords,
variant policy, output contract, and generated shaders for UI, Fullscreen, VFX, Custom Graphics, or Compute work. It is
not directly assignable to a Mesh Renderer. Surface templates provide shader logic for assignable property-only
materials; existing graph-owned materials remain supported for compatibility.

## Create And Open

1. In Project, choose **Create > Shader Graph**.
2. Select a **Surface / ...** template for mesh materials, or UI, Fullscreen Effect, VFX, or Custom Graphics for those
   targets. **Compute (not supported)** is disabled in the creation menu.
3. Name the `.keireshadergraph` asset and double-click it.
4. Use the Blackboard for exposed values and the search-first palette for nodes.
5. Connect required results to the protected Shader Output.

The toolbar and canvas context menu open the same ranked palette. Typing filters names, categories, aliases, and
descriptions. Starting a drag from a pin filters the picker to type-compatible nodes. Pin and cable menus provide
compatible-add, endpoint selection, unlink, and deletion actions.

## Build A Stable Contract

Expose the properties that materials should control, give them durable stable property IDs, and choose clear names.
Materials bind stable IDs before display names, which keeps compatible renames from silently losing values. Avoid
duplicating renderer implementation details in every Material Graph; that is the reason the template boundary exists.

Typical graphics-program flow is:

```text
Blackboard properties -> texture/coordinate/math nodes -> target stage logic -> Shader Output
```

Supported catalogs include texture sampling, UV/coordinate work, scalar/vector math, color operations, noise and
procedural helpers, and target-specific output pins. The actual palette and its compatibility filtering are
authoritative for the open graph and target. Compute compiler/runtime work has separate acceptance records; it does
not make Compute creation available through this editor menu.

## Preview, Compile, And Save

### Where Each Target Executes

| Target | How to use its material | What the authoring preview cannot establish |
| --- | --- | --- |
| Surface | Assign to a Mesh Renderer material slot. | Final scene lights, shadows, and player rendering. |
| UI | Assign the UI material on the UI Document. | The retained document's complete layout and draw order. |
| Fullscreen Effect | Assign to one of the Camera's three Fullscreen Effects slots. | Scene Color sampling; verify in Scene/Game or camera preview. |
| VFX | Assign to the Sprite/Ribbon output's Material field, or the mesh output. CPU and GPU particles execute the graph. | Particle simulation; Volumetric output still uses built-in density shading. |
| Custom Graphics | Use the existing mesh material consumer. | Arbitrary custom-pass scheduling is not exposed. |
| Compute | Creation is disabled in this menu. | Separate compiler work does not enable an editor workflow. |

Select the saved shader and choose **Material from Shader** to create the assignable asset. A shader asset and its
material are different assets: assigning the graph itself to a material slot is not the supported workflow.
See [worked camera-effect steps](ShaderGraphExamples.md#example-4-camera-tint-effect) and
[particle material steps](VfxGraph.md#use-an-authored-particle-material).

Graph edits update the live preview through a validated candidate. Invalid topology reports diagnostics and retains the
last valid result. Shader and Material Graph documents autosave after 500 ms of inactivity; **Save** immediately flushes
a still-dirty document. Source changes trigger targeted compilation and hot reload.

For surface graphs, check a preview mesh and then assign a material using the graph to real scene geometry. UI and
Fullscreen use a flat CPU preview with opacity over a checker background; mesh and environment controls are hidden.
That preview does not execute a UI document or sample the scene framebuffer. Preview success does not replace target
cooking or consumer validation.

## Reuse

Material Functions provide reusable typed expression bodies callable from Shader Graph and Material Graph. Material
Layers provide reusable surface contributions for layer stacks. Kéire 0.4.0 does not provide a generic command that
turns an arbitrary current Shader Graph selection into a new subgraph asset; use Functions/Layers deliberately.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Graph cannot be assigned to Mesh Renderer | Assign a Direct Material, Material Graph, or Material Instance instead. |
| Connection is refused | The pin types/stages are incompatible; start the palette from the destination pin. |
| Preview keeps an older image | Fix the reported candidate compile/validation error. |
| Material value disappeared after a rename | Confirm the Blackboard property's stable ID was preserved. |
| Build lacks a variant | Check the graph keyword policy and the material's selected canonical permutation. |

Use [Graph Editing](GraphEditing.md) for selection, comments, clipboard, and cable routing, and
[Shaders and Materials](../ShadersAndMaterials.md) for schemas, compilation, reflection, targets, and fallback behavior.
