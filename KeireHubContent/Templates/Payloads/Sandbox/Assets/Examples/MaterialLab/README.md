# Kéire Material Lab

This folder intentionally separates reusable custom shaders from authored materials:

- `ShaderGraphs/` contains twelve reusable Shader Graph contracts and their default implementations.
- `MaterialGraphs/` contains twelve Material Graphs that select those shaders, override exposed shader parameters,
  and add material-specific surface expression networks.
- `Assets/Scenes/SandboxShowcase.keirescene` stages every pair and four VFX effects in one production scene.

| # | Level | Example | Focus |
|---:|---|---|---|
| 01 | Foundations | Studio Paint | Exposed PBR inputs |
| 02 | Foundations | Tiled Ceramic | Texture sampling and UV tiling |
| 03 | Foundations | Neon Pulse | Time-driven unlit emission |
| 04 | Foundations | Procedural Cutout | Procedural opacity masking |
| 05 | Production | Automotive Clear Coat | Layered automotive clear coat |
| 06 | Production | Brushed Alloy | Anisotropic metallic response |
| 07 | Production | Frosted Glass | Transmission and refraction |
| 08 | Production | World-Aligned Stone | World-aligned triplanar detail |
| 09 | Advanced | Energy Dissolve | Procedural dissolve and emissive edge |
| 10 | Advanced | Hologram Scanlines | Animated scanlines and hue shift |
| 11 | Advanced | Vertex Wave | Vertex-stage displacement |
| 12 | Advanced | Iridescent Shield | Fresnel iridescence and transparency |

Start with 01 and progress in order. Shader Graphs define reusable rendering behavior. Material Graphs consume them and
remain the authoring surface for individual materials; they are separate asset types by design.
