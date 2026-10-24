# Material Examples

These exercises use the Material Inspector. Shader logic belongs in [Shader Graph](ShaderGraph.md); no Material Graph
canvas is required. Work outside Play Mode when creating persistent assets and save the scene after assignments.

## Example 1: Two Paint Variants

1. Choose **Create > Material**, name it `Paint`, and wait for import.
2. Open it and set the shared Kéire/Lit `Roughness` to `0.6` and `Metallic` to `0`.
3. Select `Paint` in Project, choose **Create > Material Instance**, and name it `RedPaint`.
4. Open the instance in the Inspector and override its base color with red.
5. Repeat from `Paint` to create `BluePaint` with a blue override.
6. Assign the instances to two visible meshes, save the scene, and reopen the assets and scene.

Expected result: both meshes inherit the same roughness but have different colors. Changing the parent's roughness
updates an instance only if that instance has not overridden it. Reset an instance's explicit color override to test
inheritance. Do not change the parent color when you intend to change only one variant.

## Example 2: Copy Compatible Values

1. Create two Materials using Kéire/Lit. Set a distinctive color and roughness on the first.
2. Choose **Copy Property Values** in its Inspector.
3. Open the second and choose **Paste Compatible Property Values**.
4. Compare the reflected values, then undo the paste and confirm the previous values return.

Expected result: compatible property values transfer without replacing the destination shader. Surface settings and
keyword selections are not property values. If you deliberately copy between unrelated shaders,
use **Paste Across Shaders > Paste Matching Property Names** and inspect which compatible values transferred.

## Example 3: A Custom Shader With Artist Controls

1. Complete [Tint A Tiled Surface](ShaderGraphExamples.md#example-1-tint-a-tiled-surface), or start with a simpler
   Surface / Lit graph exposing one Color parameter connected to its base-color output.
2. Save the graph and wait for compilation. Select it in Project and choose **Create > Material from Shader**.
3. Name the Material `CustomPaint`, create it, and open its Inspector.
4. Change the exposed color, finish the edit, and assign the Material to a mesh.
5. Reopen both assets: the graph still owns the calculation; the Material owns the chosen value.

Expected result: changing the Material does not require another copy of the shader graph. If the property is missing,
check that it is a parameter, apply/save its node-property draft, and resolve import diagnostics.

## Example 4: Temporary Per-Renderer Color

Inside a managed component method, this changes only the current entity's renderer. It assumes the assigned material
uses Kéire/Lit, whose shader symbols include `BaseColor` and `Roughness`:

```csharp
MeshRenderer? renderer = Entity.GetComponent<MeshRenderer>();
if (renderer is not null)
{
    renderer.PropertyBlock.SetColor("BaseColor", new Color(1.0f, 0.25f, 0.1f, 1.0f));
    renderer.PropertyBlock.SetFloat("Roughness", 0.5f);
}
```

To end the effect, call `renderer.PropertyBlock.Reset("BaseColor")` when the renderer is available. `Clear()` removes
all of its property-block overrides, including roughness. For a custom shader, use its **Shader Symbol**, not a renamed
display label. This snippet is method-body code, not a complete script; use the project's managed component template.

Expected result: another renderer sharing the Material is unchanged. Runtime overrides do not author a new Material
asset. Use `GetMaterialInstance(slot)` when the transient override must address one material slot.

## Before Shipping

- Reopen the Material and scene to check persistence separately from live preview.
- Check lighting, alpha behavior, and texture import diagnostics on real geometry.
- Build and run for the intended target; editor preview alone does not establish cooking or renderer compatibility.

See [Materials](MaterialGraph.md) for reset, inactive values, and legacy conversion, and
[Shaders and Materials](../ShadersAndMaterials.md) for the asset/runtime boundary.
