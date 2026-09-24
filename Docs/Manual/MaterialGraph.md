# Material Graph

New Materials store a shader selection, property overrides, and surface settings. **Create > Material** selects the
project's pinned shared **Kéire/Lit** shader and writes a schema-5 property-only `.keirematerial`. Double-click it to
edit its reflected properties in the Inspector. Shader logic belongs to the selected Shader Graph or code shader.

Older graph-owned Materials remain readable and editable. Their schema-7 `surfaceGraph` stores executable OpenPBR
expressions. The graph instructions below apply to those existing assets, not the ordinary Material creation command.

## Choose The Right Asset

| Asset | Use it for |
| --- | --- |
| Shader Graph | Surface shader logic or UI, Fullscreen, VFX, and Custom Graphics programs. Compute creation is disabled. |
| Material | Assignable shader selection, property overrides, and surface settings. |
| Existing graph-owned Material | Compatibility editing of an executable surface graph or layer stack. |
| Material Instance | Lightweight inherited overrides from a Material or another instance. |

## Create And Edit A Material

1. In Project, choose **Create > Material**.
2. Enter a name and choose **Create**. Wait for the asset worker to finish importing its shader dependencies.
3. Open the new `.keirematerial` in the Inspector and edit the shader's exposed properties and surface settings.
4. Save and assign the Material asset to a Mesh Renderer material slot.

For custom logic, create a **Shader Graph > Surface / Lit** or **Surface / Unlit** graph, expose properties, and save
it. Select that shader and choose **Material from Shader** to create its assignable material. New materials inherit
shader defaults until overridden; editing their values does not compile another copy of the shader.

## Edit An Existing Material Graph

Open an existing graph-owned `.keirematerial`. Choose its surface domain, shading model, and authoring mode, add
parameters and expression nodes, and connect the final values to its protected Material Output. Save and validate
the material on scene geometry. Opening an existing graph does not automatically convert it to a property-only source.

The surface catalog includes OpenPBR Surface, Mix Slabs, Add Slabs, Coat, Fuzz, typed Material Attributes, and the
existing BSDF modifiers. Mix clamps its factor and Add normalizes non-negative weights so the preview and generated
shader cannot create an unbounded closure contribution. The shared picker, compatible wire search, pin/cable menus,
routing knots, comments, and clipboard rules work the same way.

## Material State And Instances

Material Output owns surface state and the final surface branches. Use instances when many renderers share one graph
but need different property values. Use a per-renderer `MaterialPropertyBlock` or `DynamicMaterial` when gameplay needs
a bounded runtime override without mutating the shared asset:

```csharp
MeshRenderer? renderer = Entity.GetComponent<MeshRenderer>();
if (renderer is not null)
{
    renderer.PropertyBlock.SetColor("BaseColor", new Color(1.0f, 0.25f, 0.1f, 1.0f));
    renderer.PropertyBlock.SetFloat("Roughness", 0.5f);
}
```

These names match the shared Lit shader. For a custom shader, names must match its reflected compatible properties.
Reset one value with `Reset(name)` or all per-renderer overrides with
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
