# Shaders And Materials

Kéire deliberately separates shader programs from material data. A Shader Graph defines executable rendering logic;
a Material Graph or direct Material selects a shader and supplies values, textures, keywords, and surface state. Import
bakes both material authoring forms to the same immutable `MaterialAsset` consumed by the renderer.

```text
Raw HLSL + .keireshader ----\
                             +--> ShaderAsset --> Material / Material Graph --> MaterialAsset --> RenderSystem
.keireshadergraph ----------/
```

This boundary keeps Material Graphs as first-class material assets. Adding Shader Graph does not replace them, and the
renderer never needs to understand either authoring graph format.

Double-clicking a `.keireshadergraph` asset opens it in the Shader Graph panel. Double-clicking a
`.keirematerialgraph` asset keeps the material binding selected in the Inspector and opens its referenced Shader Graph
in the graph panel when the material uses one. Ordinary `.keirematerial` assets open directly in the Material
Inspector. This keeps shader authoring and per-material authoring connected without conflating their ownership.

## Asset Roles

| Asset | Extension | Responsibility |
| --- | --- | --- |
| Raw shader | `.hlsl` + `.keireshader` | Expert-authored HLSL, entries, defines, render state, and exposed properties. |
| Shader Graph | `.keireshadergraph` | Visual shader logic, exposed interface, keyword variants, generated shaders, and live preview. |
| Direct Material | `.keirematerial` | Compact material authoring with a tagged raw-shader or Shader-Graph reference. |
| Material Graph | `.keirematerialgraph` | Stable property bindings, textures, keywords, surface state, and a raw-shader or Shader-Graph reference. |
| Shader Graph instance | `.keireshadergraphinstance` | Bounded inherited property and keyword overrides for a Shader Graph. |

Direct Materials and Material Graphs are equally valid workflows. Both support custom shaders without exposing
generated code, both cook to ordinary `MaterialAsset` data, and both remain assignable to Mesh Renderer slots and
viewport drops.

## Shader Graph Authoring

Create **Shader Graph** in the Project panel and open the `.keireshadergraph` asset in the dockable editor. Nodes,
pins, connections, and parameter properties use stable opaque identities. Validated cable replacement, bounded
undo/redo, deterministic schema upgrades, and last-good compilation make incomplete edits recoverable.

The shared search-first node palette is available from the toolbar and the canvas context menu. Typing filters names
and categories; Up/Down wraps through results; Enter creates at the requested canvas position. The current catalog has
more than 100 stable operations organized under Parameters, Constants, Inputs, Coordinates, Texture, Surface,
Attributes, BSDF, Color, Vector, Math, Procedural, Scene, Utility, Logic & Variants, and Advanced.

Supported authoring includes:

- Scalars, vectors, colors, Texture2D parameters, metadata, ranges, categories, and stable property IDs.
- UV, vertex/world/object/camera/screen inputs, time, derivatives, normal operations, and texture sampling.
- Scalar/vector math, masks, color transforms, procedural noise, waves, dithering, and utility operations.
- Material Attributes, Standard Surface, clear coat, sheen, subsurface, transmission, Hair, Eye, Decal, Transparent,
  Unlit, and Surface output workflows.
- Boolean and enumerated keywords with deterministic, bounded variant enumeration.
- Confined custom HLSL functions beneath declared project include roots.

The compiler rejects cycles, incompatible connections, duplicate symbols, unsupported future schemas, non-finite
defaults, malformed canonical pins, undeclared keywords, and resource or collection limit violations. Diagnostics carry
stable codes plus node, pin, and generated-line context where available. Failed edits retain the last-good preview and
published runtime assets.

Compilation reports active and total nodes, unused work, texture samples, estimated ALU instructions, and variant
count. The editor previews the last-good result on a sphere, plane, cube, or selected mesh and provides exposure and
environment controls. Parameter-only edits take a material fast path; shader-affecting edits compile on a
generation-checked background job, and stale completions are discarded.

Saving stages generated HLSL and manifests outside the asset root, compiles through the normal shader importer, and
transactionally replaces `Assets/Generated/ShaderGraphs/<graph-id>/`. Each keyword variant is a stable generated
`ShaderAsset`; a default generated `MaterialAsset` remains available for direct Shader Graph assignment. DXIL,
SPIR-V, and MSL outputs pass the same reflection and ABI validation as raw shaders.

## Materials Using Custom Shader Graphs

Select either a raw Shader or Shader Graph before creating a Material or Material Graph. The created source stores a
tagged shader reference:

- `asset` selects a raw `ShaderAsset`.
- `graph` selects a Shader Graph target and canonical keyword permutation.
- `builtin` reserves explicit engine-provided shader contracts.

The Material Inspector edits both direct Materials and Material Graphs. It exposes separate **Shader Graph** and **Raw
Shader** pickers, resolves the selected graph variant to its generated shader, and builds property controls from that
shader interface. Double-clicking a Material Graph focuses this Inspector workflow. Saving preserves the graph identity,
target, keyword map, and stable Material Graph property bindings; live preview publishes only the resolved runtime
material subasset. Legacy material schemas 1 through 3 remain readable and upgrade to the tagged schema when edited.

Shader Graph parameters publish stable property IDs. Material Graph bindings resolve those IDs before display names,
so a property rename retains its value. Unknown properties and type changes produce `MAT` diagnostics instead of
silently binding unrelated data. Material Graph source is bounded to 80 properties and 16 keywords; source and cooked
payloads are capped at 4 MiB.

The immutable runtime material source schema version 3 includes alpha mode, alpha cutoff, double-sided state,
baked-emission contribution, emissive GI intensity, and validated property overrides. Opaque and masked materials
write depth; masked surfaces apply the cutoff; blended surfaces use premultiplied alpha, retain depth testing, disable
depth writes, and submit back-to-front.

## Safe Legacy Migration

Earlier Kéire releases used `.keirematerialgraph` for shader logic. Migration extracts that logic to a sibling
`*_Shader.keireshadergraph` and replaces the original path with a Material Graph that references it. The original
Material Graph ID, runtime material subasset ID, and generated shader subasset IDs are retained.
Material Graph, direct Material, and Shader Graph instance import all resolve variants through that preserved owner;
their importer versions force stale caches to rebuild without changing those identities.

Inspect without writing:

```text
KeireAssetTool migrate-shader-graphs --project <path> --check
```

Apply the migration:

```text
KeireAssetTool migrate-shader-graphs --project <path>
```

The command validates the complete project before mutation, reports destination conflicts, stages all source and
metadata pairs under `Library/ShaderGraphMigration`, and rolls the transaction back if publication fails. Already
migrated assets are reported as current. Invalid or conflicting projects are not partially rewritten.

## Renderer And Compiler Boundary

Generated and raw shaders share the fixed Kéire graphics ABI. Position, normal, UV0, and color occupy vertex locations
0 through 3. Object/view/projection and normal data use vertex `b0/space1`; scene lighting uses fragment `b0/space3`;
material numerics use fragment `b1/space3`; material textures use declaration-ordered `tN/sN/space2` pairs. Optional
Forward+, shadows, image-based lighting, instancing, world-position offset, and pixel-depth offset use explicit
versioned contracts.

Compiler libraries do not link into KeireCore or runtime applications. Bootstrap builds the pinned host
`KeireShaderCompiler`; import invokes it with bounded input, time, and output and retains last-good assets after a
failure. Compiler discovery is executable-relative, with `KEIRE_SHADER_COMPILER` as an intentional override.

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
```

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
```

Cooking retains only formats needed by the requested target: DXIL plus SPIR-V for Windows, SPIR-V for Linux, and MSL
for macOS. Host imports retain every supported format. Shader and material dependencies cook through the ordinary asset
closure, so a material cannot package without its selected graph, generated shader variant, and textures.

## Examples And Validation

Nine progressive Shader Graph examples live in
`Samples/KeireSandbox/Assets/Materials/MaterialGraphs`. Each example has a `*_Shader.keireshadergraph` shader and a
same-numbered `.keirematerialgraph` material binding. They cover basic paint, textured and normal-mapped surfaces,
procedural emission, clear coat, adaptive keyword variants, anisotropy, transmission, vertex displacement, and layered
holographic shading.

`Assets/Scenes/ShaderMaterialShowcase.keirescene` presents all nine pairs on production renderer primitives, ordered
from basic through advanced, with an active camera, lighting, and a staged gallery floor. It is the canonical Sandbox
startup scene and the startup scene for projects created from the packaged Sandbox template.

Engine tests cover deterministic encoding, graph compilation, variants, direct materials, Material Graph interface
validation, importer output, and transactional migration. Editor tests cover graph documents, publication, live
material documents, and custom Shader Graph references. Render tests keep generated output on the production graphics
path rather than validating screenshots alone.
