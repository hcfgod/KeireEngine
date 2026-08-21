# Rendering

## Real-time shadows

Opaque scene geometry with `Cast Shadows` enabled is submitted to stabilized directional cascade maps and a bounded
spot/point shadow atlas. Materials whose shader manifest opts into `receivesShadows` sample those maps only when the
renderer component also has `Receive Shadows` enabled. Directional lights support up to four cascades from the project
rendering settings. Local-light tile requests are allocated deterministically in a 4,096-pixel atlas from each light's
resolution hint, stable entity identity, and point-light face. To keep local-light cost bounded, the renderer selects at
most eight shadowed spot lights and two shadowed point lights; requests that do not fit remain fully lit. Point and spot
components expose Disabled, Hard, and Soft authoring modes plus strength, bias, and resolution.
Directional cascade centers snap in the light-space basis to whole shadow texels, and each projection reserves a
two-texel filter guard band. PCF taps outside a map are treated as lit instead of clamping an edge depth, preventing
camera or light rotation from exposing rectangular cascade borders.
The engine-owned default material uses the same receiver contract, so primitives without an assigned material receive
directional, point, and spot shadows instead of falling through an unshadowed compatibility path.
Shadow layer, quality, strength, and receiver bias occupy a dedicated shadow uniform block; enabling shadows never
changes the light's color or intensity. A receiving surface does not darken merely because it is also submitted to the
shadow pass. Visible shadowing requires another surface of the caster, or separate `Cast Shadows` geometry, to be
closer to the light along that shadow-map sample.

Realtime shadows do not require **Bake Lighting**. Baking produces static indirect lightmaps, probe data, reflection
cubemaps, and Mixed-light shadow masks. Starter scenes angle their Directional Light downward; rotating it close to the
horizon intentionally produces very long cast shadows.

Directional, point, and spot lights can opt into short-range contact refinement and texture cookies. The renderer packs
up to eight active cookies into one deterministic 4x2 atlas, keeping the spatial PBR shader within SDL's portable
16-sampler limit. Directional and spot lights also expose cookie scale, offset, and rotation. `Realtime` lights remain
entirely dynamic, `Baked` lights are excluded from runtime direct-light lists, and `Mixed` lights combine realtime direct
lighting with one of eight packed baked shadow-mask channels.

## Image-based lighting

The sandbox PBR shader opts into the renderer's image-based-lighting ABI. Environment publication bakes nine
second-order spherical-harmonic coefficients from the source panorama or cubemap atlas and caches them with the GPU
texture revision. The fragment path evaluates those coefficients as diffuse irradiance, samples the environment's
radiance-preserving mip chain by material roughness, and combines the result with an engine-owned deterministic
split-sum BRDF integration LUT. Ambient occlusion attenuates the combined indirect response; emissive and direct
directional/Forward+ light contributions remain independent.

Radiance HDR imports generate RGBE-aware mip chains instead of averaging encoded bytes. LDR and HDR equirectangular,
horizontal/vertical cross, and horizontal/vertical strip layouts use the same direction convention for sky rendering,
irradiance baking, and specular lookup. Project rotation rotates all three responses together. `Environment Diffuse`
and `Sky / Specular Intensity` scale indirect diffuse and specular/visible-sky radiance independently, while exposure
is applied once after indirect, direct, and emissive lighting are combined.

The global environment bake is deterministic and revision-cached in the renderer; a failed or still-loading custom
environment falls back to the built-in studio environment as one complete lighting resource.

## Spatial lighting

Scene schema v6 can reference one baked `LightingSet` containing RGBE lightmap and directionality arrays, packed mixed
shadow masks, a prefiltered reflection cubemap array, renderer UV transforms, reflection-probe bindings, and SH9
light-probe volumes. Mesh schema v5 carries UV1 plus explicit primitive topology, and material schema v3 marks emissive
radiance that contributes to offline GI. Static renderers use UV1 (with deterministic UV0 fallback); dynamic renderers
interpolate nearby probe-volume coefficients.

Reflection Probe components author oriented boxes, blend distance, importance, intensity, resolution, and box
projection. The renderer deterministically selects the two strongest containing probes for each draw, blends their
prefiltered specular response, and intersects the reflection ray with each oriented local box when box projection is
enabled. Light Probe Volume components author oriented bounds, spacing, priority, and normal/view bias; the runtime
trilinearly samples their baked SH9 lattice.

`LightingBaker` is an offline, backend-neutral boundary used by the editor worker and `KeireAssetTool bake-lighting`.
It fingerprints the scene, transitive asset digests, settings, platform-independent baker version, and output schema.
Successful artifacts and a digest-checked manifest are published atomically under `Library/LightingCache`; an exact hit
reuses them without recomputation, while missing or corrupt artifacts rebuild the complete entry. The deterministic CPU
path is always available and is the explicit fallback when the requested GPU bake backend is unavailable. Baked and
mixed direct light, approximate indirect bounces, emissive-to-GI sources, reflection probes, and light-probe volumes are
published together so a scene never observes a partially updated lighting set.

## Static-scene submission contracts

The renderer consumes mesh schema v5 as ordered LOD, submesh, material-slot, UV1, and primitive-topology records.
Schema v1 and v2 meshes decode as one LOD with one submesh and one slot; schema v3 carries the production LOD structure
without UV1, and schema v4 adds UV1 while implying triangle-list topology. Submission
selects a projected-height LOD, frustum-tests its submesh bounds, resolves indexed material overrides or imported
defaults, and creates a deterministic draw order. Opaque and masked work is state sorted; transparent work remains
depth-tested, disables depth writes, and sorts back-to-front with stable entity/submesh tie breaking.

Material schema v3 owns `Opaque`, `Mask`, `Blend`, `Additive`, `Modulate`, `Alpha Composite`, and `Alpha Holdout`
alpha modes, alpha cutoff, and double-sided state. Mask surfaces
write depth and reject fragments below their cutoff. Blend uses straight source alpha, Additive accumulates source color
weighted by source alpha, Modulate multiplies destination color, Alpha Composite expects premultiplied source color,
and Alpha Holdout removes destination coverage without contributing source color. Every transparent mode remains
depth-tested, disables depth writes, bypasses opaque instancing, and participates in stable back-to-front submission. A
shader schema-v1 blend flag remains a compatibility default. Failed material or shader revisions retain the complete
last-good binding.

The private frame graph executes resource upload, directional-shadow, Forward+ culling, opaque/mask, sky,
transparency, ACES tone-map, overlay, readback, and presentation passes. Its compiler validates transient reads,
derives deterministic hazard order, records resource lifetimes, aliases compatible non-overlapping transients, and
emits every resource transition before invoking a backend pass. SDL_GPU consumes those transitions at copy, render,
and presentation encoder boundaries and performs the native D3D12/Vulkan/Metal barriers. It is an internal backend
contract, not a public render-graph API. Each surface materializes the compiled physical texture slots in a
graph-owned transient heap; HDR scene color is resolved through that heap rather than through an independently
allocated attachment.

Point and spot lights are serializable registry components. Scene packets cap visible local lights at 4,096 and build
deterministic 16x16 tile lists with 128 lights per tile and explicit overflow statistics. The renderer uploads the full
light array, compact tile records, and packed light indices to graphics storage buffers. PBR and default-material
fragments consume only the current tile's list; the first 62 lights retain the portable shadow-uniform ABI while later
lights remain unshadowed rather than disappearing. The CPU builder is the deterministic fallback and reference for a
future compute builder, not the fragment-lighting path.

`RenderSystem.cpp` is the stable PImpl facade. Private compiled implementation units separate backend data types,
device/frame and fence lifecycle, surface/pipeline management, renderer-owned asset caches, and scene recording behind
`RenderBackendInternal.h`. The split does not change resource ownership: replacement resources remain transactional,
last-good bindings survive failed reloads, and retired GPU objects remain fence-delayed.

## Ownership

`Application` owns one `RenderSystem` after Windowing and before UI. In rendered mode the system owns the SDL_GPU
device, primary-window claim, swapchain, command buffers, fences, viewport textures, depth targets, and pipeline cache.
`UiSystem` owns only UI lifecycle and records its final pass through the renderer's private bridge. SDL_GPU handles,
ImGui texture IDs, command buffers, fences, and platform window handles never cross a public header.

`ApplicationSpecification::Render` selects `Automatic`, `Rendered`, `Headless`, or `Disabled`. Automatic follows the UI
mode. The existing UI clear color, present mode, and validation fields remain source-compatible and are applied by the
renderer. UI-only applications therefore require no migration.

## Frame Contract

Viewport layout happens before GPU recording. Scene and Game panels request pixel extents from their logical size and
display scale, then submit a `SceneRenderRequest` to an opaque `RenderView`. The renderer applies pending resizes at a
safe boundary, then a bounded dedicated submission thread records scene/grid passes, resolves multisampling,
composites UI, acquires the swapchain, and submits one coordinated command buffer. Each surface exchanges two display
textures, so UI consumes a stable front image while ACES writes the back image. A fence retires replaced textures,
buffers, instance data, light lists, and pipelines without a device-idle wait during normal resize churn.
Backend-only test hooks deterministically exercise device-loss propagation, bounded queue saturation, and
resize/minimize/restore transitions without exposing fault injection through the supported renderer API.

Scene color is linear RGBA16F. Multisampling resolves in HDR before a fullscreen fitted-ACES pass writes the sRGB
display surface. The renderer selects D32, D24, then D16 depth and falls back through 4x, 2x, then 1x sampling when a
requested format is unsupported. A hidden, minimized, zero-sized, or unavailable swapchain skips presentation safely.
Viewport surfaces keep their last valid image through transient zero-sized layout changes.

## Views And Cameras

Scene view uses a nonserialized editor camera for navigation and draws a depth-tested grid plus visible mesh renderers
from the edit scene, or the runtime clone while playing. An authored Camera chooses **Skybox** or **Solid Color** for
Game view, its Scene camera preview, and standalone runtime. Solid Color uses that Camera's linear clear color; Skybox
uses the project environment. Scene view retains its independent editor viewpoint but follows the active authored
Camera's background choice so authoring and Game previews agree. This remains true during Play Mode: hovering Scene
drives only the editor camera, while the focused and hovered Game view owns managed gameplay input and its authored
primary camera. Its controls are:

- `F`: frame the selected entity's full imported bounds with visible padding; double-`F` locks the view to it.
- `Shift+F`: lock or unlock the view pivot to the selected entity.
- Alt+left drag: orbit.
- Middle drag: camera-local pan.
- Mouse wheel or Alt+right drag: perspective dolly or orthographic zoom; Shift accelerates it.
- Right drag and `WASDQE`: fly; Shift accelerates movement and the wheel adjusts fly speed without dollying.
- Arrow keys: walk and strafe while the Scene view is active.
- `Q`, `W`, `E`, and `R`: select View, Move, Rotate, and Scale tools while the Scene viewport is active.
- `X`, `Y`, and `Z`: snap orientation; `Persp/Ortho` toggles projection.

The Scene toolbar camera button toggles a live 16:9 preview of the selected primary/highest-priority game Camera. It
uses a separate renderer-owned view and therefore matches the Game camera without changing the editor camera.

The Scene toolbar exposes Global/Local space, snapping, position/rotation/scale increments, and a Gizmos toggle.
Move, Rotate, and Scale handles operate directly on the selected Transform and record one scene undo step per drag.
Camera and Directional Light icons remain visible independently of geometry; camera frustums and light directions can
be toggled in the same settings popup. Scene-tool preferences live under `Library/Editor` and never dirty assets.
Viewport clicks ray-test every active entity Transform. Rendered entities use cataloged imported mesh bounds and
transform-only entities use a smaller origin proxy; the nearest hit wins, with rendered geometry winning exact ties.
Camera and Directional Light overlays retain their larger icon hit regions. The picker is engine-handle-only and does
not expose SDL or GPU resources, perform source I/O, or invoke Assimp on click.

Shader ABI v2 appends tangent and handedness at vertex location 4 while ABI v1 remains supported. The sandbox surface
uses Cook-Torrance GGX directional lighting with base color, normal, metallic-roughness (G/B), occlusion (R), emissive,
ambient, and exposure inputs. Null semantic slots bind renderer-owned white, flat-normal, neutral-ORM, or black
textures; missing non-null textures retain the checkerboard diagnostic. Tangent normals, light direction, and view
direction are evaluated in world space. Opaque pipelines write opaque alpha even when a source base-color image carries
transparent texels; authored transparency uses one of the explicit transparent surface modes described above rather
than accidental compositing.
The PBR vertex path applies the normal matrix to normals and the model matrix to tangents, orthogonalizes the tangent,
multiplies stored tangent handedness by the model determinant sign, and reconstructs a normalized bitangent. Degenerate
or nonfinite inputs use a deterministic axis fallback. D3D12 and Vulkan pixel cases cover +Y normal perturbation under
identity, nonuniform scale, rotation, and mirrored scale while mesh ABI v1 tangent generation remains compatible with
stored ABI v2 tangents.
GPU material cache entries retain an immutable, complete binding: the pipeline key, packed numeric slots,
texture/sampler bindings, tint slot, and all material/shader/texture revision stamps. The renderer resolves this once
per unique dependency stamp and shares it across entities; only the component tint is copied into per-draw constants.
Rebuilds are transactional, so a failed material, shader, texture, sampler, or attachment transition leaves the entire
last-good binding active rather than mixing old and new resources.
The left-handed camera/projection path produces clockwise exterior winding in render-target space, which is the shared
front-face convention used by built-in and asset pipelines when applying front/back culling.

Every begun render frame is completed when exit is requested and cancelled without throwing during exceptional
unwinding. Render tests release scene/view references before GPU shutdown and can be repeated in isolated processes
with Scripts/Tests/repeat-render-tests.ps1 or repeat-render-tests.sh.
The PBR suite measures neutral fallbacks, normal perturbation, metallic/roughness response, ambient occlusion, and
emissive output with tolerant regions, including opaque output from a transparent source texel. Windows shader
catalogs retain DXIL and SPIR-V, and Vulkan pipelines bind the
shader manifest's declared SPIR-V entrypoint instead of assuming `main`.

The sandbox monster is the cooked production-content calibration sample. It binds diffuse as sRGB Base Color and
normal, standalone metallic, and standalone roughness as linear semantic data; the packed metallic-roughness slot stays
on the neutral ORM fallback. Its neutral studio baseline uses ambient `[0.18, 0.20, 0.24]` at intensity/exposure `1.0`
and a warm-white directional light `[1.0, 0.95, 0.90]` at intensity `2.0`, making skin, leather, and metal response
visible without saturating the reference regions.

Relative cursor capture exists only during fly navigation and is released on focus loss. Camera state is stored below
the project's `Library/Editor` directory, so navigation never dirties a scene or enters source control.

Game view selects an enabled, hierarchy-active primary `CameraComponent` by highest priority and then stable entity ID,
falling back to the highest-priority enabled scene Camera when no Camera is marked Primary. During Play it performs this
selection against the runtime clone, never the nonserialized editor camera. Perspective and orthographic cameras validate their FOV or
size, clipping planes, background mode, and linear clear color. Skyboxes provide visible environment response but do
not synthesize shadow-casting geometry: an enabled Directional Light owns shadow direction, quality, strength, and bias.
The built-in studio sky's sun is aligned with the identity-rotation directional-light convention; custom environments
should be paired with an authored light rotated to their sun. `MeshRendererComponent` references Kéire-owned mesh/material asset IDs;
the built-in cube is available without exposing a graphics backend.

## Current Scope

The built-in cube uses a first-party HLSL shader compiled reproducibly to DXIL, SPIR-V, and MSL. One per-draw vertex
constant block carries model-view-projection, normal transform, Mesh Renderer linear tint, the selected active
Directional Light, and the project ambient environment. Keeping this foundation path in one uniform block provides
identical binding behavior across SDL_GPU backends. White base vertices preserve tint accuracy; transformed face normals
receive ambient plus flat Lambert diffuse lighting, including light color, intensity, and optional color temperature.
The editor grid explicitly bypasses scene lighting.

Rendering environment values are stored in `ProjectSettings/Rendering.keiresettings`. Use **Edit > Project
Settings...** to edit them live. Values update rendering immediately and persist once the active edit finishes; writes
are schema-validated, atomic, and resilient to transient Windows file sharing. A missing file receives conservative
defaults, while a malformed file is reported and isolated without preventing the project's asset database from opening.

When Sky Visible is enabled, the renderer draws a deterministic built-in equirectangular studio sky before scene
geometry. Assigning a Texture2D environment replaces it using the same project rotation, intensity, and exposure. The
custom path accepts LDR or RGBE Radiance HDR equirectangular maps plus horizontal/vertical cubemap cross and six-face
strip atlases. Layout and HDR encoding are versioned texture import data, so reimport and cooking preserve the same
sampling behavior across supported GPU backends.

This foundation renders asset-backed textured PBR meshes, global diffuse/specular image-based lighting, deterministic
directional and local-light shadow maps, GPU-consumed Forward+ light lists, instanced compatible geometry, sky
backgrounds, an RGBA16F/ACES pipeline, and the editor grid through a dedicated submission thread. Spatial reflection
probes and custom raw GPU passes remain later milestones.
