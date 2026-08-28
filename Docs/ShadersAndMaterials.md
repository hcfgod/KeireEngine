# Shaders And Materials

Kéire deliberately separates reusable shader contracts from artist-authored materials. A Shader Graph defines the
renderer-facing template: output contract, supported stages and passes, default branches, lighting integration, and
variant policy. A Material Graph selects that template and authors the surface expressions that feed its Material
Output. Import composes both graphs, compiles material-owned shader variants, and publishes the same immutable
`MaterialAsset` consumed by the renderer.

```text
Raw HLSL + .keireshader ------------------------------> Direct Material --\
                                                                           +--> MaterialAsset --> RenderSystem
Shader Graph template + Material Graph surface expressions --> compose ----+
                                                        Material Instance --/
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
| Shader Graph | `.keireshadergraph` | Reusable shader template, renderer contract, default logic, variants, generated shaders, and live preview. |
| Direct Material | `.keirematerial` | Compact material authoring with a tagged raw-shader or Shader-Graph reference. |
| Material Graph | `.keirematerialgraph` | Full artist surface-expression graph composed through a selected Shader Graph template. |
| Material Instance | `.keirematerialinstance` | Lightweight property and surface overrides inherited from a Direct Material, Material Graph, or Material Instance. |
| Material Function | `.keirematerialfunction` | Reusable typed material-expression body callable from Shader Graphs and Material Graphs. |
| Shader Function | `.keireshaderfunction` | Reusable lower-level shader-expression body with an explicit typed interface. |
| Material Layer | `.keiremateriallayer` | Reusable Material Attributes-producing layer graph. |
| Material Layer Blend | `.keirematerialblend` | Reusable two-layer blend graph with typed Bottom, Top, and Alpha inputs. |
| Material Parameter Collection | `.keirematerialcollection` | Project-global scalar, vector, and color defaults with stable parameter identities and runtime state. |
| Legacy Shader Graph instance | `.keireshadergraphinstance` | Readable compatibility format from 0.1.x; new assets use Material Instance. |

Direct Materials, Material Graphs, and Material Instances are equally valid assignment workflows. All support custom
shaders without exposing generated code and cook to the same immutable `MaterialAsset` consumed by the renderer.

## Shader Graph Authoring

Create **Shader Graph** in the Project panel, choose Lit/PBR, Unlit, Transparent, Decal, Fullscreen, Hair, or Eye. The
isolated worker restores the published source index instead of rescanning unrelated assets, validates the new source,
and opens the resulting `NewShaderGraph.keireshadergraph` directly in the dockable editor. Nodes,
pins, connections, and parameter properties use stable opaque identities. Validated cable replacement, bounded
undo/redo, deterministic schema upgrades, and last-good compilation make incomplete edits recoverable.
The header deliberately uses **Shader Target**, **Shader Output**, and **Live Shader Preview** terminology so shader
program authoring remains visibly distinct from material-value authoring.

The shared search-first node palette is available from the toolbar and the canvas context menu. Typing filters names
and categories; Up/Down wraps through results; Enter creates at the requested canvas position. The current catalog has
120 stable operations organized under Parameters, Constants, Inputs, Coordinates, Texture, Surface,
Attributes, BSDF, Color, Vector, Math, Procedural, Scene, Utility, Logic & Variants, and Advanced.
Right-clicking a node opens target-specific actions for inspection, cable removal, deletion, and adding a categorized
type-compatible node. Pin context menus provide the same compatible picker plus pin-level unlinking; cable context
menus select either endpoint or unlink that cable. Output nodes remain protected from deletion.
Double-click any Shader or Material Graph cable to insert a persistent routing knot without changing graph evaluation.
Each cable accepts multiple knots; drag a knot to reshape the cable, and select it then press **Delete** or double-click
it to remove only that knot. Routing edits participate in document undo/redo and survive save/reopen.
Wheel zoom claims the pointer wheel while the canvas is hovered, so its containing dock panel does not scroll too.
Pins and their labels remain visible down to 50% zoom. Input and output labels use separate clipped halves of each node
or block row, and block labels reserve the state badge area, so the additional zoom range does not create overlaps.

Supported authoring includes:

- Scalars, vectors, colors, Texture2D parameters, metadata, ranges, categories, and stable property IDs.
- UV, vertex/world/object/camera/screen inputs, time, derivatives, normal operations, and texture sampling.
- Scalar/vector math, masks, color transforms, procedural noise, waves, dithering, and utility operations.
- Material Attributes, Standard Surface, clear coat, sheen, subsurface, transmission, Hair, Eye, Decal, Transparent,
  Unlit, Fullscreen, and Surface output workflows.
- Boolean and enumerated keywords with deterministic, bounded variant enumeration.
- Confined custom HLSL functions beneath declared project include roots.
- Reusable Material Functions, Shader Functions, Material Layers, and Material Layer Blends with typed call-site pins,
  deterministic inlining, dependency tracking, recursion rejection, and depth limits.
- Branching and utility operations including If, Compare, boolean logic, reroute, scale-and-bias, unit conversion,
  general exponential/logarithm, inverse tangent, and hyperbolic math.

The compiler rejects cycles, incompatible connections, duplicate symbols, unsupported future schemas, non-finite
defaults, malformed canonical pins, undeclared keywords, and resource or collection limit violations. Diagnostics carry
stable codes plus node, pin, and generated-line context where available. Failed edits retain the last-good preview and
published runtime assets.

Shader Graph schema 5 stores a finite, non-negative maximum world-position-displacement radius for conservative GPU
occlusion. Schemas 1–4 migrate in memory with a zero radius, which keeps graphs that use vertex displacement fail-visible
until the author supplies a positive bound. Generated shader contract 6 publishes the validated bound in shader
metadata for renderer-side safety checks.

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

Creating a Material Graph opens an explicit shader picker and opens the new graph as soon as its validated creation
transaction completes. Selecting a Shader Graph creates a clean Material Output
whose pins inherit that template's actual Lit, Unlit, Transparent, Decal, Fullscreen, Hair, or Eye contract. A raw
Shader remains supported for the compact compatibility-value workflow, but full surface expressions deliberately
require a Shader Graph template so renderer-specific implementation details remain behind a versioned boundary. The
source stores a tagged shader reference:

- `asset` selects a raw `ShaderAsset`.
- `graph` selects a Shader Graph target and canonical keyword permutation.
- `builtin` reserves explicit engine-provided shader contracts.

The Material Inspector edits Direct Materials with separate **Shader Graph** and **Raw Shader** pickers. Material Graph
uses the same production expression catalog as Shader Graph: texture sampling, UV and coordinate operations, math,
masks, normals, scene inputs, procedural nodes, Material Attributes, BSDF composition, confined custom functions, and
120 typed operations. Right-clicking empty canvas space or using the toolbar opens a searchable palette grouped by node
category. Node, pin, and cable context menus expose the same targeted inspection, compatible-add, unlink, and deletion
workflow as Shader Graph. Material Output owns surface state and receives the final surface branches.

Parameter nodes become Material Instance properties and expose a stable symbol, display name, group, description,
sort priority, optional range, step, and typed default. Keyword nodes provide static parameters backed by deterministic
bounded variants. Node properties, pin defaults, positions, connections, duplication, deletion, surface state,
undo/redo, and fallback recovery are serialized deterministically. The optional **Template Defaults** view exposes old
reflected uniform bindings without crowding the primary surface canvas. Composition is validated against the selected
Shader Graph while editing. Edits autosave after 500 ms of inactivity; the normal source-change monitor then performs
one targeted compile and hot reload, so the Save button is only an immediate flush for a still-dirty document.
Parameter and texture defaults are also baked into an immutable development material immediately and published to the
loaded runtime-material identity used by scene renderers; topology changes still complete through the validated
background shader compile. Asset Browser thumbnails are invalidated with the live revision, bypass an unchanged-digest
disk-cache entry, and regenerate after the replacement runtime material is ready. A failed import leaves the previously
published material usable.

Shader Graph parameters publish stable property IDs. Compatibility bindings resolve those IDs before display names,
so a template rename retains its value. Unknown properties, type changes, output-contract mismatches, duplicate
symbols, cycles, invalid static parameters, and colliding identities produce `MAT` or underlying graph diagnostics
instead of silently changing the material. Schema-1/2/3 sources upgrade in memory to schema 4 with a deterministic
surface graph while retaining their former values and connections. Surface graphs use the shader compiler's portable
limits of 1,024 nodes, 4,096 connections, 16 keywords, and 64 generated variants; source and cooked payloads remain
capped at 4 MiB.

Material Instances never compile or duplicate shader code. Creation requires a selected Direct Material, Material
Graph, or Material Instance parent. The Inspector presents parameters produced by the composed Material Graph and
stores only explicit property, static-parameter, or surface overrides. Static overrides select one of the parent's
already compiled variants. Import resolves at most 16 ancestors, rejects cycles, missing roots, unknown properties,
invalid keyword options, and type changes, then publishes one stable runtime `MaterialAsset`. Schema-1 instances
upgrade to schema 2 with an empty static-override map. Resetting overrides returns to inherited values.

## Functions, Layers, Collections, And Runtime Overrides

Create reusable graph assets from **Reusable Material Graphs** in the Project panel. Double-clicking a function or
layer opens the shared typed graph canvas in reusable-graph mode: it validates the function body and interface without
pretending the asset is a standalone shader or publishing preview-only materials. Parameter nodes form call inputs;
the function output node forms call outputs. Both Shader Graph and Material Graph palettes list project functions and
layers under **Functions & Layers**. Saving preserves asset metadata and reimports dependents.

Function expansion is deterministic. Generated node, pin, and connection identities derive from the call site and
source identities, so identical source produces identical generated shader text. Expansion is recursive but bounded;
missing assets, wrong-purpose references, stale interfaces, cycles, and excessive depth fail with recoverable graph
diagnostics. The source graph is never mutated during expansion.

Material Parameter Collections open in the Inspector. Parameters have stable IDs, shader-safe names, display names,
descriptions, categories, sort order, types, and finite defaults. The editor supports explicit add, edit, remove,
save, and revert actions. `MaterialParameterCollectionState` provides revisioned, thread-safe runtime snapshots and
typed overrides; `DynamicMaterialInstance` provides the same bounded override/reset lifecycle for transient materials.
Editor Play Mode and packaged players own collection state per runtime world, retain compatible overrides across asset
reloads by stable parameter ID, and submit one bounded immutable global-property snapshot per render request. Mesh
Renderer material-slot instances provide narrower transient overrides. The renderer applies shared material, global
collection, renderer property-block, and material-slot instance values in that order; duplicate global names resolve by
stable collection asset ID, while renderer and slot scopes always win.

The immutable runtime material source schema version 3 includes alpha mode, alpha cutoff, double-sided state,
baked-emission contribution, emissive GI intensity, and validated property overrides. Opaque and masked materials
write depth; masked surfaces apply the cutoff. Blend uses straight source alpha, Additive accumulates source color,
Modulate multiplies the destination, Alpha Composite consumes premultiplied source color, and Alpha Holdout removes
destination coverage. These transparent modes retain depth testing, disable depth writes, bypass opaque instancing,
and submit back-to-front.

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
failure. Each import owns a UUID-named scratch directory below the operating system's temporary root and removes it on
completion. Process-aware leases preserve work owned by another live Editor, while a later import prunes abandoned jobs
after a one-hour grace period. Cleanup ignores links, files, and non-Kéire names. Compiler discovery is
executable-relative, with `KEIRE_SHADER_COMPILER` as an intentional override.

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

Twelve progressive Shader Graph/Material Graph pairs live in
`Samples/KeireSandbox/Assets/Examples/MaterialLab`, with separate `ShaderGraphs` and `MaterialGraphs` trees organized
into Foundations, Production, and Advanced tiers. The examples cover studio paint, tiled textures, animated emission,
procedural cutout, clear coat, anisotropy, transmission and refraction, world-aligned texturing, dissolve, holographic
scanlines, vertex displacement, and iridescent Fresnel shading. Every Shader Graph defines a reusable shader contract;
its paired schema-3 Material Graph selects that contract, binds every exposed input, and owns a complete surface
expression network.

`Assets/Scenes/SandboxShowcase.keirescene` presents all twelve pairs on production renderer primitives with an active
camera, lighting, a staged gallery floor, managed presentation behavior, and four edit-mode VFX examples. It is the
canonical Sandbox startup scene and the startup scene for projects created from the packaged Sandbox template. The
gallery floor uses its own tracked Direct Material, and Asset Browser thumbnails derive a representative surface color
and base texture from each generated shader interface so Material Graph previews stay identifiable. The broader
`SampleScene.keirescene` remains available for gameplay, input, physics, UI, audio, and animation workflows.

Engine tests cover deterministic encoding, graph compilation, templates, variants, Direct Materials, visual Material
Graph topology and schema upgrades, Material Instance ancestry, importer output, and transactional migration. Editor
tests cover separate Shader and Material Graph documents, reflected pins, connection undo/redo, publication, custom
Shader Graph references, the full twelve-example compiler progression, scene bindings, scripts, VFX staging, and
canonical/template parity. Render tests keep generated output on the production graphics path rather than validating
screenshots alone.
