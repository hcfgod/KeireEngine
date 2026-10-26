# Shader Graph

A Shader Graph is a target-based renderer program. It defines legal stages, resources, reflected properties, keywords,
variant policy, output contract, and generated shaders for UI, Fullscreen, VFX, Custom Graphics, or Compute work. It is
not directly assignable to a Mesh Renderer. Surface templates provide shader logic for assignable Materials, whose
values are edited in the Inspector. Older graph-owned materials remain readable through a compatibility path.

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

Expose the properties that materials should control and choose clear names. The Editor generates their stable IDs;
preserve them by renaming existing parameters rather than deleting and recreating them. The node Inspector displays
the ID read-only and edits **Display Name** and **Shader Symbol** separately.
Materials bind stable IDs before display names, which keeps compatible renames from silently losing values. Avoid
duplicating shader logic just to give two objects different colors; use Materials or Material Instances for values.

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
last valid result. Idle processing compiles previews; it does not save the graph. Node-property fields can also remain
an unapplied draft. **Apply** updates the document, while **Save** or **Ctrl+S** applies that draft, waits for compilation,
and writes the source. Save after graph edits, including layout changes. Resolve
**Node changes pending Save / Apply** before assuming a property rename or default has been published. Source changes
trigger targeted compilation and hot reload. Reopen the graph to verify persistence.

For surface graphs, check a preview mesh and then assign a material using the graph to real scene geometry. UI and
Fullscreen use a flat CPU preview with opacity over a checker background; mesh and environment controls are hidden.
That preview does not execute a UI document or sample the scene framebuffer. Preview success does not replace target
cooking or consumer validation.

In source builds with the extended-math preview fix, Arc Tangent, Hyperbolic Sine/Cosine/Tangent, Degrees To Radians,
Radians To Degrees, Negate, Exponential, Logarithm, and Scale And Bias read their declared **Input** values in the CPU
preview for scalar, vector, and color types. Older preview evaluators incorrectly looked for **Value** and could report
"missing a canonical input" for these valid nodes. Do not rename graph pins or rewrite source data to work around
that error; use an Editor build containing the fix. Runtime shader generation and the graph schema are unchanged.

## Reuse

Shader Functions and Material Functions provide reusable typed expression bodies; Material Layers provide surface
contributions. The Project menu groups these under **Create > Reusable Material Graphs**—a retained menu label, not
a requirement to author new Materials on a canvas.

To extract existing shader logic, select expression nodes, right-click, and choose
**Extract Selection to Shader Function...**. Give the function a name and choose **Extract**. The editor creates a
function beside the graph and replaces the selected expressions with a call. Output and parameter nodes cannot be
included in the selection. Extraction validates the boundary; a disabled action or diagnostic is not permission to
force an incompatible selection. See [Graph Editing](GraphEditing.md#reuse-is-explicit).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Graph cannot be assigned to Mesh Renderer | Create a Material from the shader, then assign the Material or a Material Instance. |
| Connection is refused | The pin types/stages are incompatible; start the palette from the destination pin. |
| Preview keeps an older image | Fix the reported candidate compile/validation error. |
| Material value disappeared after a rename | Confirm the Blackboard property's stable ID was preserved. |
| Build lacks a variant | Check the graph keyword policy and the material's selected canonical permutation. |
| Changed node metadata is missing after import | Apply or Save the node-property draft; wait for compilation and check diagnostics. |
| Custom include fails before compilation | Use literal include names inside the project; check declared root order, missing files, cycles, and the aggregate source-size limit. Macro-computed includes are unsupported. |

Use [Graph Editing](GraphEditing.md) for selection, comments, clipboard, and cable routing, and
[Shaders and Materials](../ShadersAndMaterials.md) for schemas, compilation, reflection, targets, and fallback behavior.
