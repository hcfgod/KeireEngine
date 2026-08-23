# Material Graph

A Material Graph is an assignable artist-authored surface graph. It selects a Shader Graph template, reflects that
template's inputs, builds surface expressions, and publishes a material asset for Mesh Renderers. Opening a Material
Graph never redirects into its referenced Shader Graph.

## Choose The Right Asset

| Asset | Use it for |
| --- | --- |
| Shader Graph | Reusable renderer and stage contract. |
| Direct Material | Property values against a Shader Graph or raw shader without a surface-expression graph. |
| Material Graph | Full surface-expression graph against a selected Shader Graph. |
| Material Instance | Lightweight inherited overrides from a Direct Material, Material Graph, or another instance. |

## Create A Material Graph

1. In Project, choose **Create > Material Graph**.
2. Select the Shader Graph template in the creation picker.
3. Open the new `.keirematerialgraph` asset.
4. Create Blackboard inputs and expression nodes.
5. Connect final values to the protected Material Output.
6. Save and assign the Material Graph asset to a Mesh Renderer material slot.

The node catalog matches Shader Graph's production expression catalog. The shared picker, compatible wire search,
pin/cable menus, routing knots, comments, and clipboard rules work the same way.

## Material State And Instances

Material Output owns surface state and the final surface branches. Use instances when many renderers share one graph
but need different property values. Use a per-renderer `MaterialPropertyBlock` or `DynamicMaterial` when gameplay needs
a bounded runtime override without mutating the shared asset:

```csharp
MeshRenderer? renderer = Entity.GetComponent<MeshRenderer>();
if (renderer is not null)
{
    renderer.PropertyBlock.SetColor("Tint", new Color(1.0f, 0.25f, 0.1f, 1.0f));
    renderer.PropertyBlock.SetFloat("Damage", 0.5f);
}
```

Names must match reflected compatible properties. Reset one value with `Reset(name)` or all per-renderer overrides with
`Clear()`. `GetMaterialInstance(slot)` provides slot-specific dynamic overrides.

## Reusable Surface Logic

Use Material Functions for typed expressions and Material Layers for surface contributions. Functions and layers are
explicit assets with validated boundaries. Material Parameter Collections provide global values shared across
materials; managed code opens a live collection through `GlobalMaterialParameters.Open(collection)`. Double-click a
Function Call node in a Material Graph to open its referenced reusable asset. Its body is an expression-level shader
graph, so Kéire intentionally edits it in the shared Shader Graph panel while identifying its Material Function purpose.

Kéire does not equate comments or collapsed regions with reusable subgraphs. Collapse is visual only. Reuse requires a
Function, Layer, or—within VFX—the dedicated VFX Subgraph asset.

## Validate In Context

Check the graph preview, a scene object using the asset, lighting and transparency behavior, and a player build for the
target renderer. If a candidate graph is invalid, the last good compiled material remains visible and the diagnostic
explains the failed edit. Preserve stable property IDs when renaming user-facing controls.

Continue with [Graph Editing](GraphEditing.md) and [Shader Graph](ShaderGraph.md).
