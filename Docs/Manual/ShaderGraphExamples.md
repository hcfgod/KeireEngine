# Shader Graph Examples

These examples describe supported graph shapes without depending on a screen position or undocumented node. Open the
search-first palette and choose the compatible texture, coordinate, math, color, or procedural node shown by the open
template. Pin-started search is the quickest way to see only valid choices for a destination.

For ordinary scene surfaces, create a Material and author its OpenPBR graph directly; no separate Shader Graph is
required. The surface examples below describe the compatible legacy Material shader target. Specialized Shader Graph
targets such as UI expose their own output pins and display their target in the graph header.

## Example 1: Tint A Tiled Surface

Use this as the first Lit/PBR graph because every connection has an obvious preview result.

```mermaid
flowchart LR
    UV["UV coordinate"] --> Tile["Multiply by Tiling"]
    Tile --> Sample["Sample Texture2D"]
    Texture["Texture2D property<br/>Base Texture"] --> Sample
    Sample --> Tint["Multiply color"]
    TintProp["Color property<br/>Tint"] --> Tint
    Tint --> Base["Shader Output<br/>Base Color"]
    Metallic["Float property<br/>Metallic"] --> MetalOut["Shader Output<br/>Metallic"]
    Roughness["Float property<br/>Roughness"] --> RoughOut["Shader Output<br/>Roughness"]
```

1. Create a Lit/PBR Shader Graph.
2. Add exposed Base Texture, Tint, Tiling, Metallic, and Roughness properties with durable stable IDs.
3. Scale UV by Tiling, sample Base Texture, multiply its color by Tint, and connect the result to Base Color.
4. Connect the two scalar controls to the compatible surface pins exposed by the template.
5. Save, create a Material Graph or Direct Material using the shader, and assign that material to scene geometry.

If a pin is named differently for the selected renderer/template, the open Shader Output and compatible picker are
authoritative. Do not force a conversion simply to reproduce the diagram.

## Example 2: Animated Emissive Pulse

```mermaid
flowchart LR
    Time["Total Time"] --> Speed["Multiply by Speed"]
    Speed --> Wave["Sine"]
    Wave --> Remap["Remap -1..1 to 0..1"]
    Remap --> Strength["Multiply by Intensity"]
    Glow["Color property<br/>Glow Color"] --> ColorScale["Multiply color"]
    Strength --> ColorScale
    ColorScale --> Emission["Shader Output<br/>Emission"]
```

Expose Speed, Intensity, and Glow Color. Remapping the sine wave prevents negative emission. Keep default values modest,
then test the material in scene lighting and in the target player; the thumbnail alone is not a lighting validation.

## Example 3: World-Space Height Blend

```mermaid
flowchart LR
    Position["World Position Y"] --> Subtract["Subtract Blend Start"]
    Subtract --> Divide["Divide by Blend Width"]
    Divide --> Clamp["Clamp 0..1"]
    Low["Low Color"] --> Lerp["Lerp"]
    High["High Color"] --> Lerp
    Clamp --> Lerp
    Lerp --> Base["Shader Output<br/>Base Color"]
```

This produces a stable vertical transition for terrain, water edges, or stylized props. Guard Blend Width with a safe
non-zero default. If the desired result must move with the object, choose the corresponding object-space coordinate
instead of world position.

## Turn A Working Graph Into A Contract

- Rename exposed properties without changing their stable IDs.
- Keep renderer/stage choices in Shader Graph and object-specific appearance in materials.
- Move repeated typed expressions into a Material Function; use Material Layers for reusable surface contributions.
- Use keywords only for genuine structural variants, then verify the material's canonical permutation is cooked.
- Fix the first candidate diagnostic when preview remains on the last-good result.

Continue with [Material Graph Examples](MaterialGraphExamples.md) to turn the shader contract into assignable surfaces,
or [Graph Editing](GraphEditing.md) for comments, collapse, clipboard, routing, and reusable assets.
