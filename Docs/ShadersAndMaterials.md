# Shaders And Materials

Kéire separates surface-material authoring from target-program authoring. A Material owns the editable OpenPBR
surface program, parameters, domain, shading model, authoring mode, closure budget, and render state. A Shader Graph
owns a Material, UI, Fullscreen, VFX, Custom Graphics, or Compute program target, including legal stages, resources,
output contract, and render integration. Both authoring paths share typed graph validation, deterministic lowering,
HLSL generation, reflected program artifacts, explicit material-pass contracts, and last-good publication.

```text
Raw HLSL + .keireshader ---------------------------> Legacy Material -----\
Material OpenPBR surface program ----------------------------------------+--> MaterialAsset --> RenderSystem
                                                        Material Instance/

Shader Graph target program --> ShaderAsset --> UI / Fullscreen / VFX / Custom Graphics
```

The renderer never needs to understand either authoring graph format. It consumes immutable shader/material artifacts,
and public authoring contracts do not expose native GPU handles or backend compiler types.

Double-clicking a `.keireshadergraph` opens its target program in the Shader Graph panel. Double-clicking a
`.keirematerial` opens its surface graph, parameter blackboard, and material preview. Material Instances open in the
Inspector. Shader Graphs are not assignable to Mesh Renderers: users assign a Material, Material Instance, or explicit
`.keiremateriallegacy` compatibility asset.

## Asset Roles

| Asset | Extension | Responsibility |
| --- | --- | --- |
| Raw shader | `.hlsl` + `.keireshader` | Expert-authored HLSL, entries, defines, render state, and exposed properties. |
| Shader Graph | `.keireshadergraph` | Target-based UI, Fullscreen, VFX, Custom Graphics, or Compute program plus resources, variants, generated shaders, and live preview. |
| Material | `.keirematerial` | Schema-7 OpenPBR surface program with stable parameters, domains, shading models, closures/layers, textures, and render state. |
| Material Instance | `.keirematerialinstance` | Lightweight property and surface overrides inherited from a Material or Material Instance. |
| Legacy Material | `.keiremateriallegacy` | Compatibility values against a raw shader or historical surface Shader Graph. |
| Shader Subgraph | `.keiresubgraph` | Reusable typed material/shader function, Material Attributes layer, or layer-blend body selected by its purpose. |
| Material Parameter Collection | `.keireparametercollection` | Project-global scalar, vector, and color defaults with stable parameter identities and runtime state. |
| Legacy Shader Graph instance | `.keireshadergraphinstance` | Readable compatibility format from 0.1.x; new assets use Material Instance. |

Materials and Material Instances are the primary assignment workflows. Legacy Materials keep raw/custom shader
projects readable without exposing generated code; every path cooks to the same immutable `MaterialAsset` consumed by
the renderer.

## Shader Graph Authoring

Create **Shader Graph** in the Project panel, then choose UI, Fullscreen Effect, VFX, Custom Graphics, or Compute. The
creation menu also exposes Legacy Surface templates for existing projects during migration. The
isolated worker restores the published source index instead of rescanning unrelated assets, validates the new source,
and opens the resulting `NewShaderGraph.keireshadergraph` directly in the dockable editor. Nodes,
pins, connections, and parameter properties use stable opaque identities. Validated cable replacement, bounded
undo/redo, deterministic schema upgrades, and last-good compilation make incomplete edits recoverable.
The header deliberately uses **Shader Target**, **Shader Output**, and **Live Shader Preview** terminology so shader
program authoring remains visibly distinct from material-value authoring.

The shared search-first node palette is available from the toolbar and the canvas context menu. Typing filters names
and categories; Up/Down wraps through results; Enter creates at the requested canvas position. The current catalog has
125 stable operations organized under Parameters, Constants, Inputs, Coordinates, Texture, Surface,
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
- Target-specific outputs for UI, Fullscreen, VFX, Custom Graphics, and Compute contracts.
- Legacy Material Attributes, Standard Surface, clear coat, sheen, subsurface, transmission, Hair, Eye, Decal,
  Transparent, Unlit, Fullscreen, and Surface workflows during migration.
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

Shader Graph schema 6 stores its explicit target definition and retains the finite, non-negative maximum
world-position-displacement radius introduced by schema 5. Schemas 1–5 migrate in memory; old Fullscreen outputs infer
the Fullscreen target and other old graphs infer Material. Generated shader contract 8 publishes target/stage metadata,
the validated displacement bound, and exact pass roles in a schema-3 shader manifest. Compute target graphs serialize
and validate today but compilation fails explicitly until the compute-program artifact ABI is available.

Compilation reports active and total nodes, unused work, texture samples, estimated ALU instructions, and variant
count. The editor previews the last-good result on a sphere, plane, cube, or selected mesh and provides exposure and
environment controls. Parameter-only edits take a material fast path; shader-affecting edits compile on a
generation-checked background job, and stale completions are discarded.

Saving stages generated HLSL and manifests outside the asset root, compiles through the normal shader importer, and
transactionally replaces `Assets/Generated/ShaderGraphs/<graph-id>/`. Each keyword variant is a stable generated
`ShaderAsset`. Legacy Surface graphs retain a private default `MaterialAsset` for preview and compatibility; program
targets publish only shader variants. DXIL, SPIR-V, and MSL outputs pass the same reflection and ABI validation as raw
shaders. Cooked shader asset schema 3 identifies every binary lane by both its exact material/program pass role and
backend format. Schema-1/2 assets migrate in memory to the `primary` role, while new multi-pass assets may carry
coexisting roles such as `primary` and `deferredGBufferStandard` without ambiguous first-format selection. The version-7
shader importer compiles each bounded role/format lane with its optional pass define, rejects duplicate roles or defines
and collisions with global defines, and keeps reflection selection independent of manifest order.

## Materials Using Custom Shader Graphs

Creating a Material produces a standalone OpenPBR surface and opens it as soon as its validated creation transaction
completes. The Material owns its executable surface expressions and publishes them through the shared typed compiler.
Historical sources may retain a tagged shader reference while they transition from reusable surface templates:

- `asset` selects a raw `ShaderAsset`.
- `graph` selects a Shader Graph target and canonical keyword permutation.
- `builtin` reserves explicit engine-provided shader contracts.

The Material editor exposes the shared searchable/context-compatible node catalog, direct surface preview, and
domain/shading/authoring summary. Schema 7 stores one authoritative `surfaceGraph`; schemas 1–6 remain readable and
save to the canonical field. External Shader Graph and Raw Shader pickers appear only for compatibility sources.

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
instead of silently changing the material. Schema-1/2/3/4/5/6 sources upgrade in memory to schema 7 while retaining
former values and executable surface connections under the canonical `surfaceGraph`. Graphs retain the shader
compiler's portable limits of 1,024 nodes, 4,096 connections, 16 keywords, and 1,024 generated variants. A graph emits
a warning after 128 variants so accidental permutation growth remains visible; source and cooked payloads remain
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
the function output node forms call outputs. Shader Graph lists project functions and layers under **Functions &
Layers**. Material Graph exposes OpenPBR Surface, Mix Slabs, Add Slabs, Coat, and Fuzz composition together with the
existing typed Material Attributes and BSDF nodes. Saving preserves asset metadata and reimports dependents.

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

Schema-7 readers preserve historical Material Graph and Shader Graph identities without rewriting source on open.
Explicitly saving a Material writes its executable surface program under the canonical `surfaceGraph` field; old
Fullscreen Shader Graphs infer the Fullscreen target and other old Shader Graphs infer the Material target.

The complete identity-preserving conversion of Legacy Surface `.keireshadergraph` assets into material assets is not
yet shipped. It requires check/preview/apply modes, before/after artifact hashes, staged source and metadata backups,
whole-project validation, redirectors, and atomic rollback. The existing `migrate-shader-graphs` utility implements the
earlier Material-Graph-to-template layout and must not be used as evidence that this reverse migration is complete.

## Render Path And Global Illumination Selection

Project Rendering settings store requested intent separately from effective runtime support. `Deferred Hybrid` is a
live production path whose active backend passes the exact GBuffer, sampled-depth, shader,
and pipeline probe. It retains the compiled forward escape pass required by Hair, Eye, transparency, unsupported
closure features, and backend fallback. Simple OpenPBR and Unlit surfaces publish the standard GBuffer contract;
closure/layer authoring may publish the extended contract; Hair and Eye are forward-only. Decal, volume, shadow,
surface-cache, bake, depth/velocity, and selection passes remain explicit artifact entries rather than hidden shader
conventions. Automatic prefers Deferred Hybrid when that runtime capability is present. Explicit Deferred Hybrid on
unsupported hardware or missing surface resources resolves to Forward+ without changing saved project intent. With
MSAA, its deferred data lanes remain single-sample and final visible geometry uses the matching multisampled forward
lane, so materials do not require multisampled GBuffer formats.

Generated Shader Graph version 10 gives Surface, Unlit, Hair, and Eye depth/velocity lanes a real previous-frame
camera/object contract. Surface displacement can use Time or Delta Time, and the velocity lane re-evaluates it at the
previous time over a second vertex stream containing the previous skinned position. Lit generated materials advertise
spatial-lighting ABI v3, consume the fixed lightmap/probe/reflection/cookie resources in forward shading, and encode the
same frame-owned selection into the deferred GBuffer. Decal output publishes both `decalDBuffer` and
`forwardTransparent`: the deferred renderer routes
scene decal geometry exclusively into the depth-tested, alpha-composited DBuffer list, while Forward+ and MSAA retain a
visible fallback lane. Deferred lighting applies the authored decal opacity independently to base color, normal, and
metallic/roughness/specular channels before evaluating the shared tiled point/spot light data and shadow atlases.

Global illumination intent is `Disabled`, `Baked`, `Realtime`, `Irradyn`, or `Hybrid`, with an independent
Irradyn Performance/Balanced/Quality choice. The capability resolver returns the requested and effective modes plus a
specific fallback reason. Selecting a GI mode without an available backend is preserved as project intent but executes
through the documented GI fallback. Irradyn is advertised only when the active backend passes the complete Deferred
Hybrid pipeline and the surface owns the required single-sample spatial-lighting resources; those resources remain
present when final coverage uses MSAA.

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

Compilation service adapters use a schema-2 canonical compile-unit manifest. The manifest fingerprints the exact
toolchain, source, include closure, defines, logical pass, stage, entry point, target platform/architecture,
D3D12/Vulkan/Metal backend, output format, optimization/debug policy, and program ABI. Its work key is the lowercase
SHA-256 of a domain-separated deterministic CBOR encoding. Policy (`LocalOnly`, `RemotePreferred`, or `RemoteRequired`)
and interactive/background priority do not enter the key, so scheduling cannot change artifact identity. The remote queue
foundation is feature-flagged off by default, owner-readable under forced RLS, and writable only through service-role
coordinator RPCs. Priority leases use `SKIP LOCKED`, bounded renewals, retry exhaustion, private artifact storage, and
lease-token-checked completion. The desktop configuration remains publishable-key-only; networkless compiler executors
never receive Supabase credentials.

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
```

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
```

Cooking retains every pass role but only the formats needed by the requested target: DXIL plus SPIR-V for Windows,
SPIR-V for Linux, and MSL for macOS. A cook fails if any role lacks a required target lane. Host imports retain every
supported format. Shader and material dependencies cook through the ordinary asset closure, so a material cannot
package without its selected graph, generated shader variant, and textures.

## Examples And Validation

Twelve progressive Shader Graph/Material Graph pairs live in
`Samples/KeireSandbox/Assets/Examples/MaterialLab`, with separate `ShaderGraphs` and `MaterialGraphs` trees organized
into Foundations, Production, and Advanced tiers. The examples cover studio paint, tiled textures, animated emission,
procedural cutout, clear coat, anisotropy, transmission and refraction, world-aligned texturing, dissolve, holographic
scanlines, vertex displacement, and iridescent Fresnel shading. Every example Shader Graph retains a target-program
contract; its paired canonical Material owns the executable surface expression network and migrates historical
template bindings without changing asset identity.

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
