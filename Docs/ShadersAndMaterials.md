# Shaders And Materials

Kéire deliberately separates shader programs from material data. A Shader Graph defines executable rendering logic;
a Material Graph or direct Material selects a shader and supplies values, textures, keywords, and surface state. Import
bakes both material authoring forms to the same immutable `MaterialAsset` consumed by the renderer.

```text
Raw HLSL + .keireshader ----\
                             +--> ShaderAsset --> Direct Material ----\
.keireshadergraph --> generated ShaderAsset --> Material Graph -------+--> MaterialAsset --> RenderSystem
                                                    Material Instance -/
```

This boundary keeps Material Graphs as first-class material assets. Adding Shader Graph does not replace them, and the
renderer never needs to understand either authoring graph format.

Double-clicking a `.keireshadergraph` asset opens its shader logic in the Shader Graph panel. Double-clicking a
`.keirematerialgraph` opens its own Material Graph canvas and Material Output node; it never redirects into the
referenced Shader Graph. Ordinary `.keirematerial` and `.keirematerialinstance` assets open in the Inspector. Shader
Graphs are not assignable to Mesh Renderers: users create a Direct Material or Material Graph that selects the shader.

## Asset Roles

| Asset | Extension | Responsibility |
| --- | --- | --- |
| Raw shader | `.hlsl` + `.keireshader` | Expert-authored HLSL, entries, defines, render state, and exposed properties. |
| Shader Graph | `.keireshadergraph` | Visual shader logic, exposed interface, keyword variants, generated shaders, and live preview. |
| Direct Material | `.keirematerial` | Compact material authoring with a tagged raw-shader or Shader-Graph reference. |
| Material Graph | `.keirematerialgraph` | Visual material values connected to a Material Output whose inputs reflect a raw Shader or Shader Graph. |
| Material Instance | `.keirematerialinstance` | Lightweight property and surface overrides inherited from a Direct Material, Material Graph, or Material Instance. |
| Legacy Shader Graph instance | `.keireshadergraphinstance` | Readable compatibility format from 0.1.x; new assets use Material Instance. |

Direct Materials, Material Graphs, and Material Instances are equally valid assignment workflows. All support custom
shaders without exposing generated code and cook to the same immutable `MaterialAsset` consumed by the renderer.

## Shader Graph Authoring

Create **Shader Graph** in the Project panel, choose Lit/PBR, Unlit, Transparent, Decal, Fullscreen, Hair, or Eye, and
open the resulting `NewShaderGraph.keireshadergraph` asset in the dockable editor. Nodes,
pins, connections, and parameter properties use stable opaque identities. Validated cable replacement, bounded
undo/redo, deterministic schema upgrades, and last-good compilation make incomplete edits recoverable.
The header deliberately uses **Shader Target**, **Shader Output**, and **Live Shader Preview** terminology so shader
program authoring remains visibly distinct from material-value authoring.

The shared search-first node palette is available from the toolbar and the canvas context menu. Typing filters names
and categories; Up/Down wraps through results; Enter creates at the requested canvas position. The current catalog has
more than 100 stable operations organized under Parameters, Constants, Inputs, Coordinates, Texture, Surface,
Attributes, BSDF, Color, Vector, Math, Procedural, Scene, Utility, Logic & Variants, and Advanced.

Supported authoring includes:

- Scalars, vectors, colors, Texture2D parameters, metadata, ranges, categories, and stable property IDs.
- UV, vertex/world/object/camera/screen inputs, time, derivatives, normal operations, and texture sampling.
- Scalar/vector math, masks, color transforms, procedural noise, waves, dithering, and utility operations.
- Material Attributes, Standard Surface, clear coat, sheen, subsurface, transmission, Hair, Eye, Decal, Transparent,
  Unlit, Fullscreen, and Surface output workflows.
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
`ShaderAsset`. The importer retains a private default `MaterialAsset` for shader preview and backward compatibility,
but editor material pickers and viewport drops do not expose it as a user material. DXIL, SPIR-V, and MSL outputs pass
the same reflection and ABI validation as raw shaders.

## Materials Using Custom Shader Graphs

Creating a Material Graph opens an explicit shader picker in the creation dialog. Choose either a Shader Graph or raw
Shader; that choice defines the Material Output inputs and runtime program. The created source stores a tagged shader
reference:

- `asset` selects a raw `ShaderAsset`.
- `graph` selects a Shader Graph target and canonical keyword permutation.
- `builtin` reserves explicit engine-provided shader contracts.

The Material Inspector edits Direct Materials with separate **Shader Graph** and **Raw Shader** pickers. The Material
Graph panel provides the visual workflow: selecting a shader rebuilds Material Output from its exposed interface,
unconnected inputs use editable defaults, and typed scalar, vector, color, or texture value nodes can be connected to
those inputs. Connections, node positions, surface state, and fallback values are undoable and serialized
deterministically. Live edits publish only the resolved runtime material subasset; saving queues an isolated import and
hot reload. Legacy material schemas 1 through 3 remain readable and upgrade when edited.

Shader Graph parameters publish stable property IDs. Material Graph bindings resolve those IDs before display names,
so a property rename retains its value. Unknown properties and type changes produce `MAT` diagnostics instead of
silently binding unrelated data. Schema-1 Material Graph bindings upgrade in memory to deterministic schema-2 Material
Output pins and retain their values. Material Graph source is bounded to 80 properties, 256 value nodes, 256
connections, and 16 keywords; source and cooked payloads are capped at 4 MiB.

Material Instances never compile or duplicate shader code. Creation requires a selected Direct Material, Material
Graph, or Material Instance parent. The Inspector presents the inherited shader interface and stores only explicit
property or surface overrides. Import resolves at most 16 ancestors, rejects cycles, missing roots, unknown properties,
and type changes, then publishes one stable runtime `MaterialAsset`. Resetting overrides returns to inherited values.

The immutable runtime material source schema version 3 includes alpha mode, alpha cutoff, double-sided state,
baked-emission contribution, emissive GI intensity, and validated property overrides. Opaque and masked materials
write depth; masked surfaces apply the cutoff; blended surfaces use premultiplied alpha, retain depth testing, disable
depth writes, and submit back-to-front.

## Safe Legacy Migration

Earlier Kéire releases used `.keirematerialgraph` for shader logic. Migration extracts that logic to a sibling
`*_Shader.keireshadergraph` and replaces the original path with a Material Graph that references it. The original
Material Graph ID, runtime material subasset ID, and generated shader subasset IDs are retained.
Material Graph, Direct Material, new Material Instance, and legacy Shader Graph instance import all resolve variants
through that preserved owner; importer versions force stale caches to rebuild without changing those identities.

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

Engine tests cover deterministic encoding, graph compilation, templates, variants, Direct Materials, visual Material
Graph topology and schema upgrades, Material Instance ancestry, importer output, and transactional migration. Editor
tests cover separate Shader and Material Graph documents, reflected pins, connection undo/redo, publication, and custom
Shader Graph references. Render tests keep generated output on the production graphics path rather than validating
screenshots alone.
