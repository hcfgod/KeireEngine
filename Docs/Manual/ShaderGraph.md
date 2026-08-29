# Shader Graph

A Shader Graph is a target-based renderer program. It defines legal stages, resources, reflected properties, keywords,
variant policy, output contract, and generated shaders for UI, Fullscreen, VFX, Custom Graphics, or Compute work. It is
not directly assignable to a Mesh Renderer. Legacy Surface templates remain available only while surface graphs migrate
into materials.

## Create And Open

1. In Project, choose **Create > Shader Graph**.
2. Select UI, Fullscreen Effect, VFX, Custom Graphics, or Compute. Use a Legacy Surface template only for compatibility.
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
authoritative for the open graph and target. Compute graphs serialize and validate their thread-group contract but do
not compile until the compute-program artifact ABI ships.

## Preview, Compile, And Save

Graph edits update the live preview through a validated candidate. Invalid topology reports diagnostics and retains the
last valid result. Shader and Material Graph documents autosave after 500 ms of inactivity; **Save** immediately flushes
a still-dirty document. Source changes trigger targeted compilation and hot reload.

Use the preview to check the selected prototype mesh and obvious parameter behavior, then assign a material using the
graph to real scene geometry. Preview success does not replace target cooking or scene validation.

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
