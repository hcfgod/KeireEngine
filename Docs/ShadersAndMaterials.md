# Shaders And Materials

Material source schema version 3 adds a `bakedLighting` object that controls emissive contribution and intensity for
offline GI. Schema 2 introduced the explicit `surface` object with `alphaMode`, `alphaCutoff`, and `doubleSided`.
Opaque and masked surfaces write depth. Masked surfaces reject base-color alpha below the cutoff. Blend surfaces use
premultiplied-alpha output, retain depth testing, disable depth writes, and are submitted back-to-front. Existing
schema-v1 and schema-v2 materials remain readable; older material drafts receive deterministic surface and baked-
lighting defaults without rewriting their property names.

Meshes expose named material slots. `MeshRendererComponent::Material()` and `SetMaterial()` remain slot-zero
compatibility APIs, while indexed overloads author per-slot overrides. A missing override uses the imported mesh slot's
default material when available.

## Source Assets

A `.keireshader` manifest names a project-relative HLSL source, vertex and fragment entry points, bounded defines and
include roots, fixed-function render state, and scalar/vector/color/Texture2D material properties. Paths are relative to the
project `Assets` directory. Absolute paths, traversal, symlinks, include cycles, incompatible stages, duplicate
properties, and unsupported resource bindings are rejected before publication.

`renderState.topology` accepts `TriangleList`, `LineList`, or `PointList` and must match the rendered mesh submesh.
Point and line imports report the matching manifest value so custom diagnostic and procedural shaders remain explicit.

`.keirematerial` assets reference a stable shader asset ID and store validated property overrides. `ShaderAsset`,
`MaterialAsset`, and the built-in `MeshAsset` are immutable runtime assets with safe fallbacks. Failed shaders and
materials resolve to a conspicuous error material instead of leaving an invalid GPU pipeline. Numeric properties are
limited to 64 `float4` slots and fragment textures to 16 declaration-ordered bindings.

The fixed vertex ABI is position, normal, UV0, and color at locations 0 through 3. Object/view/projection and normal
data use vertex `b0/space1`; scene lighting/exposure use fragment `b0/space3`; packed numeric material data uses
fragment `b1/space3`; Texture2D properties use matching `tN/sN/space2` pairs. The PBR ABI additionally consumes a
bounded local-light block at fragment `b2/space3`. Existing custom shaders with only the two original fragment uniform
buffers remain valid and ignore that binding.

A manifest with `usesImageBasedLighting: true` opts into fragment `b3/space3`, containing nine diffuse-irradiance
coefficients plus environment rotation, diffuse/specular intensity, mip count, layout, and RGBE encoding metadata. The
renderer appends the active environment and its BRDF integration LUT after declaration-ordered material textures and
the optional two shadow samplers. The opt-in therefore requires exactly four reflected fragment uniform buffers and two
additional reflected samplers; shaders that omit it retain the original ABI and cost.

The Project menu creates an Unlit Shader transactionally: its manifest, HLSL template, and metadata are either all
published or all rolled back. It can also create a Material that references the selected shader.

## Material Graph Authoring

Create **Material Graph** from the Project panel and double-click the `.keirematerialgraph` asset to open the dockable
editor. Material graphs reuse the stable node-canvas foundation used by VFX: nodes and pins retain opaque IDs, cable
replacement is validated before mutation, layout is ordinary authoring metadata, and every accepted change participates
in bounded undo/redo. The schema-v2 catalog gives every node type a stable identifier, declares legal shader stages,
and migrates schema-v1 pins deterministically. The Master selector changes among Surface PBR, Transparent PBR, Decal
PBR, Unlit, Hair PBR, and Eye PBR outputs while preserving compatible Master inputs and removing only cables whose
destination no longer exists.

Open **Add Node**, or right-click anywhere on the graph canvas, to use the same searchable node palette. Its search
field receives keyboard focus on open and filters while typing. **Up/Down** wraps through the visible matches and
**Enter** creates the selected node. With no query, the menu keeps recent/common nodes at the top and preserves the
full category tree for browsing. Canvas-created nodes are placed at the clicked graph position.

The public compatibility contract exposes the latest source schema, generated-shader version, and renderer vertex
layout version. Generated HLSL records its generator and source-schema versions, and each `.keireshader` manifest
records all three values. A source newer than the running editor fails before graph collections are decoded, with a
specific upgrade diagnostic; it is never interpreted as an older graph or partially rewritten. Historical schema-v1
sources still migrate deterministically to the current schema-v2 representation.

The searchable graph library is grouped into Parameters, Constants, Inputs, Coordinates, Texture, Surface, Attributes,
BSDF, Color, Vector, Math, Procedural, Scene, Utility, Logic & Variants, and Advanced categories. Its 100-plus typed
operations include UV0/UV1, vertex/world/object/camera/screen data, time, derivatives, panning, polar coordinates,
gradients, parallax, regular/explicit-level/triplanar texture sampling, normal decode/detail/height/flatten operations,
Fresnel and reflection vectors, masks, vector construction, comprehensive scalar/vector math, luminance, hue, contrast,
saturation, overlay and blackbody color tools, checker/Voronoi/gradient noise, waves, dithering, declared keywords,
static switches, and confined custom functions.
Parameter texture values use the Project asset picker and declare their sampling semantic. Disconnected typed input
defaults remain directly editable, so a Master node can be tuned without placeholder Constant nodes.

The PBR Master implements metallic/roughness shading with explicit dielectric specular level, clear coat, sheen,
subsurface, anisotropy, transmission, thickness, index of refraction, refraction, normal/detail-normal, parallax,
emission, occlusion, opacity, world-position offset, and pixel-depth offset inputs. A typed Material Attributes input can
replace the per-field surface pins. Make/Break/Blend Material Attributes and Standard Surface/Clear Coat/Sheen/
Subsurface/Transmission BSDF nodes let reusable layer chains be composed without lossy untyped vectors. Hair and Eye
outputs install physically useful lobe, culling, IOR, and clear-coat defaults. Older graphs without newer pins remain
source compatible and receive neutral defaults. Boolean and enumerated
keywords produce a sorted, bounded permutation set with deterministic 16-character suffixes; duplicate symbols,
undeclared keyword tokens, cycles, malformed canonical pins, incompatible cables, excessive collections, and non-finite
defaults are rejected before publication.

Saving a publishable graph stages one generated `.hlsl` plus `.keireshader` manifest per variant outside the asset root,
then transactionally replaces `Assets/Generated/MaterialGraphs/<graph-id>/` and writes the canonical source. Retained
variant metadata moves with the staged directory, stale variants and their sidecars disappear together, and a source
write failure restores the complete previous generated directory. The manifests opt into the fixed
instancing ABI and carry output-specific blend, depth-write, and culling state. Generated shaders are passed through the
normal production importer, including DXIL, SPIR-V, and MSL compilation and reflection validation. Graph import also
publishes each compiled variant and one default `MaterialAsset` as stable generated subassets. The Inspector presents
that runtime material under its parent graph, and graph drops resolve to it before a Mesh Renderer is mutated; scene
data therefore retains the same renderer-safe material type used by ordinary `.keirematerial` assets. A failed edit keeps
the document's last-good definition and compilation available to the preview host, surfaces `MG` diagnostics in the
graph panel, and does not overwrite a valid source asset. Successful compilation reports active/total nodes, unused
work, texture-sample count, estimated ALU cost, and variant count; unused nodes, high variant pressure, and elevated
arithmetic cost produce actionable `MG1xxx` diagnostics without blocking a valid graph. The panel renders an adaptive
live shaded sphere, plane, cube, or asset-picked custom mesh from the last-good state with exposure, environment
intensity, and rotation controls. Built-in graph nodes are evaluated per preview sample, so procedural noise, UV
transforms, masks, remaps, emission, and normal paths agree with the generated shader instead of collapsing to property
name guesses. Texture inputs use neutral semantic placeholders until a GPU-backed preview sampler is selected, and
custom-function nodes retain their bounded node default. Custom meshes load through the ordinary asset system without
blocking the UI; an invalid in-progress graph cannot replace a valid preview. The shared node canvas clips every card,
pin, cable, label, and drag overlay to its own panel rectangle.

Every successful graph revision also resolves the graph's parameter defaults into its stable generated material and
publishes an owner-thread development revision, so already-assigned scene renderers update during authoring. Continuous
edits are coalesced on a 75 ms live interval. Changes to exposed parameter defaults use a material-only fast path; edits
that alter generated HLSL compile on a background worker and publish the new in-memory shader variants before the
material revision. Stale completions are discarded by revision and failed work retains the last-good preview and scene
shader. Save atomically reloads the parent graph, all generated variants, the default material, and loaded dependents.

The Sandbox includes nine source graphs intended both as learning material and importer fixtures:

| Graph | Progression | Features |
| --- | --- | --- |
| `01_BasicPaint` | Basic | Exposed paint color and roughness |
| `02_TexturedSurface` | Intermediate | Shared tiled UVs, base/normal textures, normal decoding, metallic and roughness |
| `03_ProceduralEmissive` | Advanced | Rotated UVs, layered noise, smooth mask, remap, desaturation, HDR emission |
| `04_ClearCoatDetail` | Layered | Two texture samples, decoded/detail normals, procedural coat variation, sheen |
| `05_AdaptiveTechSurface` | Hero | Parallax, procedural/texture blend, posterization, Fresnel coat/emission, keyword variant |
| `06_AnisotropicBrushedMetal` | Specialist | Directional brushed-metal response with anisotropic controls |
| `07_TransmissionGlass` | Specialist | Live tint, opacity, transmission, thickness, IOR, and refraction controls |
| `08_ProceduralVertexDisplacement` | Advanced geometry | Animated procedural world-position offset with a grounded PBR surface |
| `09_HolographicVoronoi` | Hero layered | Voronoi color/emission through Standard, Coat, Sheen, Subsurface, and Transmission BSDF layers |

They live in `Samples/KeireSandbox/Assets/Materials/MaterialGraphs` and use ordinary project texture dependencies, so
opening and saving one exercises the same deterministic publication path as a newly authored graph.

Mesh Renderer slots accept ordinary materials and the compiled material exposed by a Material Graph. Drop either asset
onto a rendered Scene entity to replace slot zero, or choose/drop it directly in a named Inspector slot. Viewport drops
use imported local bounds rather than a generic primitive-sized proxy; dropping on a transform-only model root expands
the assignment to every rendered descendant. The built-in cube is an explicit mesh-picker choice and owns the same
one-slot material layout whether it was selected directly or loaded from older scene data that encoded the default cube
as an empty mesh ID.

`.keirematerialinstance` stores a parent graph or instance ID, typed property overrides, and keyword overrides. Resolve
the root-to-leaf ancestry with `ResolveMaterialGraphInstance`, then use `BakeMaterialGraphInstance` with the project's
stable variant resolver to produce the ordinary `MaterialAssetDefinition` consumed by rendering. Resolution is capped
at 16 ancestors, validates property types and keyword options, and records parent and texture references as asset
dependencies. The instance importer performs that resolution transactionally and publishes a stable runtime
`MaterialAsset` subasset, so an instance can be selected in a Mesh Renderer slot or dropped on a rendered entity without
teaching the renderer about authoring assets. Instances never copy or mutate their parent graph. Select a graph or
existing instance in the Project panel before choosing **Create > Material Instance** to establish its parent without
copying graph data.

Custom nodes name one identifier-safe HLSL function and one project-relative include beneath a graph-declared include
root. Editor reads are confined to the project source root after canonical path resolution and are capped at 1 MiB per
file. Recursive includes are cycle checked and bounded; absolute paths, traversal, binary input, missing files, and
root escapes produce generated-shader diagnostics rather than unrestricted filesystem access.

## Compiler Boundary

Bootstrap builds `KeireShaderCompiler` from the exact recursive SDL_shadercross lock. Compiler libraries never link
into KeireCore or runtime applications. Import stages source files in a bounded ASCII-safe temporary directory, invokes
the host compiler with a timeout and bounded output, and produces DXIL, SPIR-V, and MSL. SPIR-V reflection validates the
fixed Kéire graphics ABI before deterministic canonical bytes enter the import cache.

Contextual importers receive a confined dependency reader and return dependency path/digest records plus structured
diagnostics. The legacy byte-only callback remains supported. Hot reload publishes only successful imports, so a
compile error leaves the last working shader active. Compiler errors are shown on the selected shader and in the editor
Console, and are written to `Logs/Core.log` and `Logs/Client.log` alongside their terminal output.

Automatic compiler discovery is anchored to the running executable and searches a bounded set of development and SDK
tool locations before consulting the working directory. This keeps Hub-launched editors reliable even though their
working directory is the opened project root. `KEIRE_SHADER_COMPILER` remains the explicit override.

Run bootstrap after cloning to build the cached compiler:

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
```

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
```

Set `KEIRE_SHADER_COMPILER` only when intentionally selecting another packaged copy of the pinned compiler.

Dropping a Material Graph onto a renderer resolves only a generated material that exists in the mounted catalog. If
the graph has not published that runtime subasset yet, the editor compiles and remounts it asynchronously, keeps the
renderer's previous material during the operation, and applies the graph only after publication succeeds.

Assigning an already open graph also replays its current last-good shader and parameter revision, including on built-in
Cube and other built-in meshes; the Inspector assignment and rendered surface therefore cannot diverge. Graph edits
remain dirty until **Save** atomically publishes both graph source and generated runtime variants. Closing the editor
chains the Material Graph into the unsaved-change workflow after any dirty-scene decision and requires **Save**,
**Discard**, or **Cancel**. Reopening always decodes the saved graph source instead of reconstructing a default draft.

Material Graph-generated shaders preserve the renderer's complete fixed vertex-to-pixel interpolator ABI even when a
simple graph does not consume every field. This avoids backend-specific sparse-interface optimization differences and
keeps DXIL pipeline-state creation deterministic on Direct3D 12.

## Cooking

Cooking keeps only the selected target's shader variant: DXIL for Windows, SPIR-V for Linux, and MSL for macOS. Host
preserves all variants. The asset tool exposes this explicitly:

```text
KeireAssetTool cook --project <path> --output <path> --profile Dist --target windows
```

Supported target values are `host`, `windows`, `linux`, and `macos`. Renderer caches track loaded and attempted asset
revisions, build complete replacements before swapping, and retain last-good GPU resources after failed reloads. SDK packages include the compiler, required runtime
libraries, licenses/notices, and locked recursive commits so packaged asset workflows remain reproducible.
