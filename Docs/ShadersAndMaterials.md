# Shaders And Materials

## Current Authoring Workflow

Use the Inspector for Material values and Shader Graph for shader logic. Start with
[Materials](Manual/MaterialGraph.md), [Material Examples](Manual/MaterialGraphExamples.md), and
[Shader Graph Examples](Manual/ShaderGraphExamples.md). Historical filenames and internal `MaterialGraph` types
remain for compatibility; they do not make a Material canvas the default workflow.

1. Create a Material with **Create > Material** and wait for its shader import.
2. Double-click the Material, edit its reflected values in the Inspector, and finish the edit boundary.
3. Assign it to the appropriate scene consumer and save the scene.
4. Reopen both the asset and scene to check persisted values and assignments independently of live preview.

To change the calculation, create a Shader Graph, save it, then choose **Create > Material from Shader**.
Material Inspector edits commit through their document workflow; the graph's **Save** button is for shader source,
not a missing step in ordinary Material editing.

**Create > Material** uses the shared **Kéire/Lit** surface shader. On first use, the project installs version 1.0.0
sources for Lit, Unlit, UI, Fullscreen, VFX, and Custom Graphics under `Assets/Keire/SharedShaders/1.0.0` and records
their identities and source hashes in `ProjectSettings/SharedShaders.lock`. Existing sources are verified rather
than regenerated from the current editor's templates. Opening an older project does not opt it into the library.
The library does not include compute programs. Target-specific consumer integrations remain separate acceptance gates.

UI and fullscreen Shader Graphs use a bounded 2D authoring preview: graph UVs cover the image, and opacity is shown
over a checker background. Mesh selection, mesh rotation, and environment lighting are hidden for these targets.
Exposure remains available. This CPU preview evaluates graph color and emission; it does not execute a UI document,
sample the scene framebuffer, or prove backend/player parity. Surface graphs retain their mesh previews.
Focus the Shader Graph panel to route keyboard undo/redo to that graph's history. Output nodes expose their named
input defaults directly; their unused generic node value is hidden to avoid edits that cannot affect shader output.

Shared sources can be inspected in Shader Graph. Use **Copy to Project** in the Inspector to create an editable copy
with a new shader asset ID. Asset operations reject changes to the shared source namespace. Missing or modified
pinned sources require restoring the recorded library content; automatic repair must not change a project's visuals.

The implementation history is tracked in [Material and shader replacement](MaterialShaderReplacement.md).
The graph-owned material section below describes compatibility assets, not the default Material creation workflow.
In the Material Inspector, missing shaders remain replaceable without losing saved values;
**Reset Shader Properties** restores shader defaults and participates in material save/undo.
Overrides rejected by a resolved shader are retained in the source's `inactiveProperties` array and listed under
**Inactive Overrides**. They do not become GPU bindings or import dependencies. Restoring a compatible declaration
reactivates its saved value. Multiple historical values may share a name; the most recently archived compatible value
wins. Resetting an active property removes compatible older overrides so they cannot reactivate; incompatible
type/range history remains saved. **Remove Inactive Overrides** deletes the archive through the same save/undo path.
When the shader declares stable property IDs, schema-5 sources store ordered identity/value records in
`propertyOverrides`. Renaming a symbol retains its value; a different property reusing that symbol does not inherit
it. Incompatible identity records remain inactive. Schema-3 material variants apply these identities through nested
parents and may override values inherited directly from shader defaults.
**Create > Material from Shader** writes a property-only `.keirematerial` source and opens the Material Inspector.
For graph shaders, **Shader Keywords** exposes boolean and named options. **Shader Default** clears an individual
selection, and **Reset Shader Keywords** clears all selections. Keyword edits select imported variants and preserve
property values; they use the material source save/undo workflow. Loading is asynchronous: a pending choice keeps the
last-good preview until its shader asset is ready. Cancel discards it, and selecting another shader or material prevents
the old request from applying. Missing/broken variants remain pending with repair/cancel guidance. This does not add
keyword reflection or equivalent authoring controls to handwritten shader assets.
The surface material shader picker hides generated code duplicates when their owning Shader Graph exists; it keeps
orphaned legacy shader sources selectable. Existing references to generated code are still loadable.
The material references the selected Shader Graph or code shader and inherits defaults until overridden. The
Inspector shows mesh surface controls only for surface shaders. UI, fullscreen, VFX, and custom graphics materials
show a target badge in their asset thumbnail instead of a misleading lit sphere; view a fullscreen effect through
a camera effect slot to see its actual output.
Double-clicking these sources reopens the Inspector. Surface controls, compatible-value preservation when switching
shaders, reset, save, and undo use the property-material document. Preview and undo publish to the stable generated
runtime material ID. Value imports resolve shader references without compiling shader code. Nested material instances
apply keyword overrides from oldest parent to nearest child before selecting a graph shader variant.
The standard **Material** command starts with shared Kéire/Lit; **Material from Shader** preselects the current shader.
Historical assets can still use the graph implementation described below. The reviewed conversion workflow is exposed
under **Assets > Review Legacy Material Conversion...**. `InspectShaderGraphMigration` reports direct binding conversion or
executable graph extraction; pass that report to `ApplyShaderGraphMigration` to reject changed review inputs.
The transaction retains verified backups and automatically rolls back interrupted publication on exclusive project
open. Unsupported graphs and collisions prevent publication. The editor validates staged sources, metadata, source index, and runtime catalog before publication. Test the converted
project in its target player; this document is not a claim of release acceptance for every project.
Unsupported Compute Shader Graph creation is disabled.

Material properties are grouped by their shader category. **Property Overrides** identifies explicit values and
offers individual reset. **Copy Property Values** and **Paste Compatible Property Values** use stable identities,
or names for legacy entries; paste skips unavailable or incompatible declarations and is one undo action.
Variants share these controls: copy includes resolved inherited values and paste makes compatible values explicit
overrides while preserving the parent, keywords, surface settings, and incompatible saved history.
For different shader interfaces, expand **Paste Across Shaders** and choose **Paste Matching Property Names**.
This explicitly matches code symbols rather than clipboard identities, retains the destination shader's property IDs,
and skips incompatible values. Ordinary paste continues to follow stable IDs across property renames.
Shader parameter descriptions appear as material-property tooltips. Color parameters can enable **HDR Color** in
the Shader Graph node Inspector; Save applies this metadata. Code shader property declarations accept optional
`description` text (up to 512 bytes) and `hdr: true` for Color properties. These fields survive generated manifests
and cooked reflection. HDR color controls accept finite values above one; ordinary UI display-color controls retain
their existing 0–1 validation. Legacy material colors outside that range also use the HDR control to remain editable.

Shader Graph node metadata is a draft until applied or saved. **Save** and **Ctrl+S** apply the draft, wait for
compilation, and then write the source; invalid drafts remain editable with diagnostics. Switching selection preserves
an unapplied draft until Save, Apply, or explicit Discard. Closing the editor also detects unapplied node properties.
Material and shader **Asset Details** can be expanded to inspect source IDs, importer versions, and content hashes.

Kéire separates material-value authoring from shader logic. A new Material owns its shader selection, property overrides,
and surface settings. Existing graph-owned Materials retain editable OpenPBR surface programs. A Shader Graph
owns a Material, UI, Fullscreen, VFX, Custom Graphics, or Compute program target, including legal stages, resources,
output contract, and render integration. Shader Graph and legacy graph compatibility share typed graph validation,
deterministic lowering, HLSL generation, reflected program artifacts, explicit material-pass contracts, and last-good
publication. Compute's internal contract does not enable Compute creation in the editor menu.

```text
Raw HLSL + .keireshader or Surface Shader Graph --> Property Material ----\
Existing Material OpenPBR surface program ------------------------------+--> MaterialAsset --> RenderSystem
                                                      Material Instance/

Shader Graph target program --> ShaderAsset --> UI / Fullscreen / VFX / Custom Graphics
```

The renderer never needs to understand either authoring graph format. It consumes immutable shader/material artifacts,
and public authoring contracts do not expose native GPU handles or backend compiler types.

Double-clicking a `.keireshadergraph` opens its target program in the Shader Graph panel. Double-clicking a
property-only `.keirematerial` opens the Inspector; an existing graph-owned source opens its surface graph,
parameter blackboard, and material preview. Material Instances open in the
Inspector. Shader Graphs are not assignable to Mesh Renderers: users assign a Material, Material Instance, or explicit
`.keiremateriallegacy` compatibility asset.

## Asset Roles

| Asset | Extension | Responsibility |
| --- | --- | --- |
| Raw shader | `.hlsl` + `.keireshader` | Expert-authored HLSL, entries, defines, render state, and exposed properties. |
| Shader Graph | `.keireshadergraph` | Surface, UI, Fullscreen, VFX, or Custom Graphics shader logic, resources, variants, and preview. Compute creation is disabled. |
| Material | `.keirematerial` | New schema-5 property-only material, or an existing schema-7 graph-owned OpenPBR surface program. |
| Material Instance | `.keirematerialinstance` | Lightweight property and surface overrides inherited from a Material or Material Instance. |
| Legacy Material | `.keiremateriallegacy` | Compatibility values against a raw shader or historical surface Shader Graph. |
| Shader Subgraph | `.keiresubgraph` | Reusable typed material/shader function, Material Attributes layer, or layer-blend body selected by its purpose. |
| Material Parameter Collection | `.keireparametercollection` | Project-global scalar, vector, and color defaults with stable parameter identities and runtime state. |
| Legacy Shader Graph instance | `.keireshadergraphinstance` | Readable compatibility format from 0.1.x; new assets use Material Instance. |

Materials and Material Instances are the primary assignment workflows. Legacy Materials keep raw/custom shader
projects readable without exposing generated code; every path cooks to the same immutable `MaterialAsset` consumed by
the renderer.

## Camera fullscreen effects

Create a **Fullscreen Effect** Shader Graph, then create a Material using that shader. In a Camera Inspector, assign
that material under **Fullscreen Effects** to **Before Tonemapping**, **After Tonemapping**, or **After UI**. Each slot
accepts one material and can be cleared independently. The camera slots determine execution order; the graph's
injection-point metadata does not override an explicit camera assignment. Camera assignments participate in scene
save/load, duplication, reset, and Inspector undo. The editor Scene view uses the effects of the camera chosen
by the scene's camera-selection rules, not simply the entity highlighted in the Hierarchy. Among enabled cameras
on active entities with a Transform, Primary cameras take precedence, then higher Priority, with entity ID breaking
ties. The Scene view keeps its own navigation camera while copying those effect assignments. Game view, camera
preview, and the runtime player execute effects through the same renderer path.

For a tint effect, connect **UV0 → Scene Color.UV**, multiply **Scene Color.Color** by an exposed Color parameter,
and connect the result to **Fullscreen Shader Output.Color**. A graph can also generate a replacement color or
procedural pattern without sampling Scene Color. Time and Delta Time use the scene material clock. Edit the material
parameter to update the image without recompiling the graph. Scene Color is a separate copy of the current image,
so sampling never reads the attachment being written. Before Tonemapping operates on linear HDR color; subsequent
slots operate on the tone-mapped image, with After UI including camera overlay panels. Temporal history is recorded
before the two post-tonemapping slots.

The graph thumbnail remains a CPU approximation and has no camera scene input. Verify effects in the Scene/Game
views or camera preview. Scene Color is fragment-only and only legal in Fullscreen graphs. Scene depth sampling and
arbitrary user-scheduled render passes are not provided by this path. Invalid or unavailable effect assets leave the
current image intact; invalid shader reloads retain the last executable shader. UI/fullscreen materials cannot be
executed as mesh shaders.

If an effect does not appear:

1. Check that the Material uses a Fullscreen shader and that it is assigned to the intended Camera slot.
2. Confirm the Camera and its entity are enabled. In scenes with multiple cameras, check Primary and Priority;
   highlighting a different camera in the Hierarchy does not switch the Scene view's effect source.
3. Inspect the camera image in Scene/Game view, rather than judging Scene Color from the graph thumbnail.
4. Check the selected slot: Before Tonemapping receives linear HDR values, so an effect tuned for display color
   can look different there. The graph's injection-point metadata does not move an explicitly assigned effect.
5. Check import/compiler diagnostics if the image stays unchanged. An unavailable effect preserves the existing
   image; unchanged output alone does not prove the effect executed successfully.

Material creation now accepts Surface, UI, Fullscreen, VFX, and Custom Graphics shaders. Replacement shader choices
stay within the resolved material's target. Retained UI uses UI materials. VFX materials execute authored graph logic
on CPU and GPU Sprite/Ribbon outputs and the existing mesh consumer. Volumetric VFX retains built-in density shading.
Custom Graphics has the existing mesh consumer; it does not expose arbitrary render-pass scheduling.

The current Shader Graph generator version is 39 and the Shader Graph importer version is 54. These versions describe
generated binaries and cache invalidation, not the serialized graph schema or the engine release number.

Generated volume materials declare ABI 13 for texture-backed cloud noise or ABI 14 for buffer-backed noise.
Both append planet-fixed cloud coordinates to the prior radial-fog layout. Legacy material generation also advances
through its generator-derived importer version; existing source assets require reimport before using this layout.
Source-less older cooked shaders do not acquire planetary support merely because the renderer accepts their old prefix.

C++ uses `CameraComponent::SetFullscreenEffect(CameraEffectStage::AfterTonemapping, materialId)`.
C# exposes nullable Material properties `Camera.EffectBeforeTonemapping`, `Camera.EffectAfterTonemapping`, and
`Camera.EffectAfterUi`; assigning null clears that stage. These are asset references, so edits persist when the scene
is saved outside Play Mode.

## Shader Graph Authoring

**Create > Shader Graph > Fullscreen Presets** supplies editable Blur, Chromatic Aberration, Distortion, and Vignette
graphs. These are ordinary graphs sampling the camera image, not fixed color overlays. Create a material from the
chosen graph and assign it to a camera stage. `RadiusPixels`, `OffsetPixels`, and `AmplitudePixels` control sampling
distances in pixels through the fragment-only **Scene Texel Size** node, which returns `(1 / width, 1 / height)` for
the current Scene Color copy. It remains correct when the surface resolution changes. Distortion also exposes Speed
and Frequency; Vignette exposes Radius, Softness, and Strength.

Blur uses nine equally weighted taps; RadiusPixels changes their spacing, not the number of taps. It is a compact
single-pass blur, not a full bloom pyramid or a large separable Gaussian filter. Extend the graph with additional
Scene Color samples, textures, math, and time nodes for other spatial effects. Scene depth, prior-frame history,
and arbitrary multi-pass intermediate targets are not provided by these presets.

Create **Shader Graph** in the Project panel, then choose a **Surface / ...** template, UI, Fullscreen Effect, VFX, or
Custom Graphics. **Compute (not supported)** is disabled. The
isolated worker restores the published source index instead of rescanning unrelated assets, validates the new source,
and opens the resulting `<entered-name>.keireshadergraph` directly in the dockable editor. Nodes,
pins, connections, and parameter properties use stable opaque identities. Validated cable replacement, bounded
undo/redo, deterministic schema upgrades, and last-good compilation make incomplete edits recoverable.
Creating a node brings that node into view, including nodes added from the blackboard or reusable-function picker.
The header deliberately uses **Shader Target**, **Shader Output**, and **Live Shader Preview** terminology so shader
program authoring remains visibly distinct from material-value authoring.

The shared search-first node palette is available from the toolbar and the canvas context menu. Typing filters names
and categories; Up/Down wraps through results; Enter creates at the requested canvas position. The searchable catalog is organized under Parameters, Constants, Inputs, Coordinates, Texture, Surface,
Attributes, BSDF, Color, Vector, Math, Procedural, Scene, Utility, Logic & Variants, and Advanced.
Right-clicking a node opens target-specific actions for inspection, cable removal, deletion, and adding a categorized
type-compatible node. Pin context menus provide the same compatible picker plus pin-level unlinking; cable context
menus select either endpoint or unlink that cable. Output nodes remain protected from deletion.
Double-click any Shader Graph cable (or a cable in a legacy Material compatibility graph) to insert a persistent routing knot without changing graph evaluation.
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
the Fullscreen target and other old graphs infer Material. The generated shader contract publishes target/stage metadata,
the validated displacement bound, and exact pass roles in a schema-3 shader manifest. Compute compiler/runtime progress
has separate [acceptance evidence](RevampComputeCompilerLane.md); it does not enable Compute creation in the editor menu.

Compilation reports active and total nodes, unused work, texture samples, estimated ALU instructions, and variant
count. The editor previews the last-good result on a sphere, plane, cube, or selected mesh and provides exposure and
environment controls. Parameter-only edits take a material fast path; shader-affecting edits compile on a
generation-checked background job, and stale completions are discarded.

Software previews sample texture mip zero using the imported magnification filter and per-axis repeat, clamp, or
mirror addressing. sRGB colors are decoded before bilinear filtering; alpha and linear/data textures remain linear.
These previews do not estimate screen-space derivatives for mip selection or anisotropic filtering.

Dependency depth follows the longest path through shared inputs. Node-preview budgets count the selected node and
its upstream inputs, including disconnected work; triplanar sampling counts three texture reads. Changing a shader's
displacement bound or a parameter's stable ID refreshes live shader metadata even when its HLSL is unchanged.

Saving stages generated HLSL and manifests outside the asset root, compiles through the normal shader importer, and
transactionally replaces `Assets/Generated/ShaderGraphs/<graph-id>/`. Each keyword variant is a stable generated
`ShaderAsset`. Legacy Surface graphs retain a private default `MaterialAsset` for preview and compatibility; program
targets publish only shader variants. DXIL, SPIR-V, and MSL outputs pass the same reflection and ABI validation as raw
shaders. Cooked shader asset schema 4 retains schema 3's identification of every binary lane by its exact material/program pass role and
backend format. Schema-1/2 assets migrate in memory to the `primary` role, while new multi-pass assets may carry
coexisting roles such as `primary` and `deferredGBufferStandard` without ambiguous first-format selection. The shader importer compiles each bounded role/format lane with its optional pass define, rejects duplicate roles or defines
and collisions with global defines, and keeps reflection selection independent of manifest order.

## Existing Graph-Owned Materials (Compatibility)

Historical sources may contain an executable surface graph rather than property overrides. The compatibility reader
retains their identities and supports the schema-7 `surfaceGraph` representation. Some older sources can therefore
still open a legacy graph panel. That is recovery support, not the recommended way to create a new Material.

Use the reviewed conversion described below to separate eligible legacy shader logic from Material values. Do not
manually remove `surfaceGraph`, rename extensions, or edit generated shaders to imitate conversion. Unsupported
sources remain on their compatibility path and should be diagnosed from the report.

## Functions, Layers, Collections, And Runtime Overrides

Create reusable graph assets from **Reusable Material Graphs** in the Project panel. Double-clicking a function or
layer opens the shared typed graph canvas in reusable-graph mode: it validates the function body and interface without
pretending the asset is a standalone shader or publishing preview-only materials. Parameter nodes form call inputs;
the function output node forms call outputs. Shader Graph lists project functions and layers under **Functions &
Layers**. Legacy material compatibility supports its historical surface composition; it is not an instruction to
create a new Material canvas. Saving reusable logic preserves asset metadata and reimports dependents.

For eligible shader expressions, **Extract Selection to Shader Function...** creates a reusable source beside the
graph and prepares a replacement call. Exclude output and parameter nodes, wait for the asset operation to finish,
then inspect the call and save. A collapsed comment does not perform extraction.

Function expansion is deterministic. Generated node, pin, and connection identities derive from the call site and
source identities, so identical source produces identical generated shader text. Expansion is recursive but bounded;
missing assets, wrong-purpose references, stale interfaces, cycles, and excessive depth fail with recoverable graph
diagnostics. The source graph is never mutated during expansion.

Function expansion and material composition remove comment references to nodes eliminated from their generated
graphs. Original source comments remain intact. When a material replaces a template's World Position Offset input,
its surface graph supplies the displacement bound; an unbounded replacement disables displaced occlusion.

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

1. Back up the project, leave Play Mode, and complete pending imports/builds.
2. Save or discard graph drafts, including unapplied shader node properties.
3. Choose **Assets > Review Legacy Material Conversion...** and inspect every reported source and proposed extraction.
4. Choose **Validate and Apply Reviewed Conversion** only after reviewing the report. If sources change, refresh the
   review instead of assuming the old report still applies.
5. Read the completion status, reopen converted Materials in the Inspector, and verify scene assignments and rendering.
6. Build a target player as a separate acceptance check.

The conversion stages source files, metadata, source index, and runtime catalog, and preserves reviewed identities.
Eligible graph-owned materials can extract their shader into a Shader Graph. Unsupported materials retain their legacy
editor. Backups remain in `Library/MaterialShaderUpgrade`; successful conversion clears affected asset history.
A failure before publication differs from “Conversion committed, but editor reload failed”: for the latter, reopen the
project as instructed rather than blindly applying again. Resolve reported recovery errors before another review.

The Core entry points `InspectShaderGraphMigration` and `ApplyShaderGraphMigration` underlie the reviewed operation.
Opening a historical source alone is not consent to convert it. Source-schema compatibility, project conversion,
shared-library upgrades, and a player build are separate operations.

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

### Custom Include Files

Use literal `#include "file.hlsl"` or `#include <file.hlsl>` directives inside project files. Resolution checks the
including file's folder first, then the manifest's declared `includeRoots` in order. Project-root-qualified includes
under a declared root remain supported. Absolute paths, drive-relative paths, and paths escaping the project are
rejected. Prefer simple relative paths that work on every supported platform.

Each import retains the first successfully read contents of a source/include file and uses those same bytes for
dependency traversal, SHA-256 identity, and compiler staging. A file edited later cannot silently replace the contents
already captured by that import; subsequent imports read fresh contents. Comments do not create dependencies, and
backslash-newline continuations are recognized. Snapshot capture is not a filesystem-wide atomic read.

The graphics importer supports at most 16 include roots and 256 captured files; literal header names are bounded to
1024 bytes. The aggregate source snapshot is bounded by
`AssetImportContext::MaximumDependencyBytes` (64 MiB by default), including the entry source. Split oversized generated
inputs rather than assuming every file receives a separate full budget. Macro-computed includes and cyclic include
graphs are unsupported. Discovery is conservative, not a conditional preprocessor: literal includes inside inactive
`#if` branches must still resolve, and include guards do not enable cyclic dependency graphs.

Snapshot capture supplies the retained inputs to the persistent import cache described below. Process cancellation
is a separate execution contract.
The code-shader importer version is 12. Updating importer versions invalidates previously derived outputs; the next
import may therefore take longer while those shaders are rebuilt. Cooked shader schemas are unchanged by this update.

### Cancelling Background Compilation

Superseding a background Shader Graph generation or closing its document requests cancellation of its native stage
and reflection jobs. Those jobs stop their owned compiler processes rather than waiting for the normal per-process
timeout. A cancelled generation cannot publish partially compiled variants or replace the last-good preview.

Editor tooling supplies the job token through an internal factory. The public `CreateShaderAssetImporter` API and
`ShaderImporterSpecification` layout remain unchanged; ordinary importer callers retain timeout-only behavior.
A synchronous Save has no new Cancel button, and cancellation does not forcibly interrupt arbitrary
source-reader callbacks or establish whole-process-tree termination. This is not the persistent compile-cache gate.

### Compilation And Binding Contracts

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
and interactive/background priority do not enter the key, so scheduling cannot change artifact identity. Remote-service
deployment and authentication are separate from the local shader-authoring workflow; this guide does not prescribe
historical provider credentials or claim that a remote compiler service is enabled.

The compile-unit helper accepts up to 129 unique defines, covering the graphics importer's 128 global defines plus
one optional pass selector. Ordered include roots and the source's project-relative path are not fields in the public
schema-2 manifest. An internal import identity therefore wraps the unit keys with those inputs and the authored shader
manifest digest; it must not sort include-root precedence away. Internal toolchain snapshots fingerprint the supplied
flat compiler/runtime-library bundle and stage the retained bytes, so changing only a runtime DLL changes its identity.
The graphics importer uses these identities to reuse complete, previously validated imports in
`Library/ShaderCompileCache/v1`. A hit checks the stored key, payload checksum, decoded shader contract, exact requested
pass/format inventory, dependency digests, and output limits before returning it without native stage or reflection
processes. A miss executes the retained compiler/runtime bytes against the retained source snapshot. Compiler source
and include arguments are stable scratch-relative paths, not UUID-bearing absolute paths.

The bundled host compiler accepts the source directory followed by up to 16 authored include roots in order, and
forwards all 128 global defines plus the optional pass selector. Bootstrap generates narrowly adapted build-tree
copies of the pinned CLI/library sources; it does not edit the Vendor checkout. The adaptation is fingerprinted and
fails configuration if its expected upstream source fragments change. Updating only this integration reuses the
existing DXC/SPIRV dependency objects instead of discarding the entire host-tool build cache.

The cache has 64 collision-checked slots, each at most 8 MiB including its header: at most 512 MiB of committed entries.
Collisions replace an older slot and cause a normal miss, never cross-key reuse. Larger valid imports still compile
but are not stored. Atomic writes prevent partial entries from becoming hits; simultaneous misses may compile twice.
Temporary atomic-write files can briefly consume additional space, so the committed-entry bound is not a whole-disk
quota. Storage errors, corrupt/truncated/oversized entries, unavailable bundled runtimes, loader overrides, and unknown
compiler names do not prevent ordinary compilation. Sources containing `__DATE__`, `__TIME__`, or `__TIMESTAMP__`
conservatively bypass reuse, including spliced spellings and token-pasting inputs that could synthesize them.
This is a local graphics-import cache, not remote compilation, a compute-shader cache,
or proof of packaged/native platform acceptance on every operating system.

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

Use the Inspector-first [Material Examples](Manual/MaterialGraphExamples.md) for paint variants, compatible property
copy, custom shader assignment, and temporary per-renderer overrides. Use [Shader Graph Examples](Manual/ShaderGraphExamples.md)
for texture tinting, emission, world-space blends, and camera effects.

The Sandbox's historical `Assets/Examples/MaterialLab/MaterialGraphs` folder name does not prescribe the current
authoring model. Inspect the actual asset: property-only Materials open in the Inspector; older executable sources
may require compatibility handling or reviewed conversion. Do not copy a legacy graph merely to change a color.

For a repeatable acceptance check, change one Material property, finish the edit, reopen it, reopen the assigned scene,
and build a target player. Then deliberately make an invalid shader edit: diagnostics should identify the failure
while the last-good result remains usable. Restore the valid graph before continuing.

Relevant source and test authorities:

| Contract | Source or focused tests |
| --- | --- |
| Inspector fields, resets, pending keywords, and commit boundaries | `KeireClient/Source/Editor/AssetInspectorPanel.cpp`, `MaterialInspectorPanel.cpp` |
| Default Lit shader and reflected symbols | `KeireCore/Source/Project/SharedShaderLibrary.cpp` |
| Current creation commands | `KeireClient/Include/KeireClientInternal/Editor/AssetBrowserPanelInternal.h` |
| Legacy/property-source routing | `KeireClient/Source/Editor/EditorWorkspaceMaterialGraph.cpp` |
| Reviewed conversion UI | `KeireClient/Source/Editor/EditorWorkspaceMaterialUpgrade.cpp` |
| Material identity, inactive values, and variants | `KeireEditorTests/Source/MaterialIdentityTests.cpp`, `MaterialInactiveOverrideTests.cpp`, `MaterialVariantEditingTests.cpp` |
| Migration publication/recovery | `KeireTests/Source/Project/MaterialMigrationPublicationTests.cpp`, `MaterialMigrationRecoveryTests.cpp` |

UI shader materials can be assigned to the **UI Material** field of a scene UI Document. The reference is saved with
the scene and exposed as `UIDocument.Material` in C#. Clearing it restores standard UI shading.
