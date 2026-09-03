# Rendering

## Real-time shadows

Opaque scene geometry with `Cast Shadows` enabled is submitted to stabilized directional cascade maps and a bounded
spot/point shadow atlas. Materials whose shader manifest opts into `receivesShadows` sample those maps only when the
renderer component also has `Receive Shadows` enabled. Directional lights support up to four cascades from the project
rendering settings. Before applying the local-shadow limits, the renderer ranks every eligible light by intensity with
stable entity-identity ties, then selects at most eight shadowed spot lights and two shadowed point lights. Their tile
requests are allocated deterministically in a 4,096-pixel atlas from each light's resolution hint, stable identity, and
point-light face; requests that do not fit remain fully lit. Every tile reserves a two-texel cleared gutter, and receiver
sampling is clamped to that tile's texel-center bounds so soft PCF cannot read a neighboring light or point face. Point
and spot components expose Disabled, Hard, and Soft authoring modes plus strength, bias, and resolution.
Directional cascade centers snap in the light-space basis to whole shadow texels, and each projection reserves a
two-texel filter guard band. A Directional Light's resolution hint scales the project shadow-resolution base by 0.25x,
0.5x, 1x, or 2x for Low through Very High, clamped to the validated 256-8192 range. PCF taps outside a map are treated
as lit instead of clamping an edge depth, preventing camera or light rotation from exposing rectangular cascade borders.
The engine-owned default material uses the same receiver contract, so primitives without an assigned material receive
directional, point, and spot shadows instead of falling through an unshadowed compatibility path.
Shadow layer, quality, strength, and receiver bias occupy a dedicated shadow uniform block; enabling shadows never
changes the light's color or intensity. A receiving surface does not darken merely because it is also submitted to the
shadow pass. Visible shadowing requires another surface of the caster, or separate `Cast Shadows` geometry, to be
closer to the light along that shadow-map sample.

Realtime shadows do not require **Bake Lighting**. Baking produces static indirect lightmaps, probe data, reflection
cubemaps, and Mixed-light shadow masks. Starter scenes angle their Directional Light downward; rotating it close to the
horizon intentionally produces very long cast shadows.

Soft directional and local shadows use a weighted 3x3 tent PCF footprint. Local atlas guard texels keep that footprint
inside the selected spot tile or point-light face, including animated/skinned silhouettes.

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

LightingSet schema 2 also declares whether the bake owns static diffuse indirect, static specular indirect, and
stationary direct channels. Hybrid and Irradyn plans must replace or exclude an owned channel rather than adding a
second estimate. Schema-1 lighting sets migrate in memory to the historical all-baked ownership contract; publication
always writes schema 2.

Reflection Probe components author oriented boxes, blend distance, importance, intensity, resolution, and box
projection. The renderer deterministically selects the two strongest containing probes for each draw, blends their
prefiltered specular response, and intersects the reflection ray with each oriented local box when box projection is
enabled. Light Probe Volume components author oriented bounds, spacing, priority, and normal/view bias; the runtime
trilinearly samples their baked SH9 lattice. Use `LightProbeVolumeComponent::ConfigureGrid` when changing bounds and
spacing together so the final grid is validated and published as one update.

`LightingBaker` is an offline, backend-neutral boundary used by the editor worker and `KeireAssetTool bake-lighting`.
It fingerprints the scene, transitive asset digests, settings, platform-independent baker version, and output schema.
Successful artifacts and a digest-checked manifest are published atomically under `Library/LightingCache`; an exact hit
reuses them without recomputation, while missing or corrupt artifacts rebuild the complete entry. The deterministic CPU
path is always available and is the explicit fallback when the requested GPU bake backend is unavailable. Baked and
mixed direct light, approximate indirect bounces, emissive-to-GI sources, reflection probes, and light-probe volumes are
published together so a scene never observes a partially updated lighting set.

## Static-scene submission contracts

Project rendering settings schema 5 stores Automatic/Forward+/Deferred-Hybrid path intent, None/FXAA/TAA/MSAA 2x/4x
anti-aliasing intent, a static render scale, bounded automatic dynamic-resolution settings, and requested GI/Irradyn
quality. Capability resolution returns requested and effective values plus specific path, anti-aliasing,
dynamic-resolution, and GI fallback reasons. Older settings migrate without silently enabling temporal or
dynamic-resolution behavior.

The renderer publishes its live FXAA, exact 2x/4x MSAA, temporal-AA, deferred-multisample, and dynamic-resolution
capabilities after backend probing. Scene, Game, and runtime view owners resolve the requested settings and switch the
surface sample contract before UI, presentation, or readback work captures the surface epoch. None and FXAA use a
single-sample surface; supported MSAA requests create the matching multisample epoch for either render path. Deferred
Hybrid keeps single-sample depth, velocity, GBuffer, DBuffer, and Irradyn inputs while a multisampled forward coverage
subpass shades opaque geometry, decals, transparency, and VFX into HDR and resolves once before Irradyn. FXAA executes
as part of the tone-map pass. TAA on both Forward+ and Deferred Hybrid jitters each accepted camera frame, reprojects
the published per-surface history with object/deformation velocity, clamps history to the current 3x3 neighborhood,
and rejects history after discontinuities, resizes, path changes, or device recreation. Motion excludes the temporal
jitter itself, and the frame graph keeps velocity live through tone mapping; the resolved image therefore does not
follow the sample sequence. Directional cascade fitting uses the unjittered projection so temporal samples cannot move
shadow texel bounds. Reduced render scale is presented with spatial upscaling; it is not advertised as TAAU.

Irradyn is a staged Deferred-Hybrid GI path. After opaque, forward-only, transparent, and VFX rendering, a reduced-
resolution trace pass gathers the fully lit HDR scene with depth-tested screen-space visibility rays. This means local
and directional lights, their shadows and cookies, DBuffer decals, baked/environment lighting, and visible emissive
radiance feed the same indirect-light estimate instead of being approximated again in the deferred-lighting shader.
The trace reprojects a separately owned Irradyn history through the velocity buffer, rejects depth discontinuities,
clamps stale energy, and retains a small bounded feedback term for stable multi-bounce response. A second pass performs
depth/normal-aware bilateral upsampling and additively composites diffuse indirect before tone mapping.

Screen-space data is supplemented by a render-thread-owned, incrementally updated scene-card cache. Opaque and masked
meshes contribute conservative world bounds; authored world-position displacement expands those bounds. Forward-only
opaque surfaces use the hair participation path, transparent surfaces inject reduced-density radiance, and CPU/GPU VFX
are aggregated by renderer class so ribbons behave like low-density hair while volumetric particles inject
low-frequency volume energy. The cache is hard-capped at 2,048 entries with deterministic least-recently-updated
eviction, while a frame samples only its nearest 8, 12, or 16 cards. These paths intentionally approximate
translucency, hair, fog, and VFX rather than pretending that every fragment is an opaque surface. Performance,
Balanced, and Quality bound rays, visibility steps, and card updates to target 1.5 ms, 2.5 ms, and 4.0 ms p95
respectively at 1080p on the project baseline GPUs; `render-benchmark` remains the authority for measured hardware
results.

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

The private frame graph executes resource upload, directional shadows, VFX simulation and dynamic bounds, safe
occluder depth, HZB construction, unified visibility classification, Forward+ light-list compaction, spatial-lighting
selection, VFX expansion, opaque/mask, sky, transparency, ACES tone-map, overlay, readback, and presentation passes.
Its compiler validates transient reads, derives deterministic hazard order, records resource lifetimes, aliases
compatible non-overlapping transients, and emits every resource transition before invoking a backend pass. Transient
textures declare an exact portable format, usage set, sample count, and relative extent. Alias keys are derived from
those compatibility fields rather than authored as magic numbers, while the physical allocation unions usage flags
from every non-overlapping logical occupant. SDL_GPU allocation fails closed when the active backend cannot support the
compiled format/usage contract. Inspection JSON schema 2 exposes the same typed metadata.

The separately compiled Deferred Hybrid graph adds depth/velocity, standard and extended GBuffer, DBuffer decal,
deferred-lighting, and forward-only opaque-tail passes before the common transparent/post-processing tail. Its standard
layout uses sRGB base-color/metallic, floating-point normal/roughness, packed material, a floating-point baked/spatial
lighting payload, floating-point velocity, and a sampleable depth attachment. Cooked shader asset schema 3 and renderer
pipeline lookup address binaries by the exact
pass-role/backend pair, so forward, depth/velocity, and deferred GBuffer lanes coexist without format-order ambiguity.
Generated Surface and Unlit Shader Graphs publish those three lanes; Hair, Eye, transparent, and unsupported opaque
materials retain their exact forward lanes. Recording submits deferred-capable materials to the GBuffer and only the
remaining opaque work to the forward tail, so fallback work is never duplicated.

Portable built-in standard-GBuffer and fullscreen deferred-lighting shaders are generated as DXIL, SPIR-V, and MSL.
Their SDL_GPU pipeline set is created transactionally only when every MRT format and the sampleable depth contract are
supported. Successful probing publishes the public `DeferredHybrid` capability, and requested or Automatic selection
uses the live deferred graph for single-sample and supported multisample surfaces. Multisample mode retains the exact
single-sample deferred data attachments and uses the forward coverage subpass for the final shaded geometry. Probe
failure or missing per-surface resources retains Forward+ without leaking partially created handles; shutdown and
device recovery invalidate the complete set before a replacement device can advertise it.

The lighting resolve reconstructs world position from sampled depth, composites the three DBuffer channels, evaluates
metallic/roughness/specular direct lighting, and consumes the same bounded Forward+ tile/light/index buffers as the
forward path. Its fourth GBuffer target carries transformed lightmap UVs, one-based lightmap and mixed-shadow-mask
layers, a frame-owned spatial-selection record, and additive-scene identity. The resolve uses that payload for baked
directional lightmaps, SH9 light probes, two box-projected reflection probes, environment IBL, eight-channel mixed-light
masks, packed directional/local cookies, and bounded screen-space contact-shadow refinement. Local lights are filtered
to the owning additive contribution. Point and spot attenuation, cones, directional cascades, local shadow-atlas
layers, ambient occlusion, exposure, and Unlit bypass therefore remain coherent across both paths. Empty GBuffer pixels
are discarded so the sky remains intact. Decal-domain Mesh Renderer submissions are removed from the
opaque/transparent lists on an active
deferred surface and recorded through their exact `decalDBuffer` lane with depth testing and alpha-composited DBuffer
targets. During Deferred Hybrid MSAA, their explicit forward-transparent lane supplies multisampled visible coverage
while the DBuffer lane remains available to Irradyn's single-sample scene data.

Shader Graph generator version 10 publishes camera, object, skin-deformation, and world-position-offset motion in the
depth/velocity lane. Capture keeps bounded
per-surface-epoch history keyed by scene/entity and a separate camera history; first frames, skipped frames, recreated
surfaces, and entities without stable identity use the current transform as the previous transform, producing zero
instead of stale velocity. Skinned draws retain a consecutive-frame palette and produce paired current/previous vertex
streams through both compute and CPU fallback skinning. Generated velocity vertex shaders evaluate Time-driven offset
at `time - deltaTime` over the previous deformed position before applying previous object and camera transforms. CPU
mesh VFX uses its authored previous position. Depth/velocity recording expands an
instanced batch into deterministic single-instance draws so every instance receives its own previous model while the
regular GBuffer and forward passes retain batching.

Point and spot lights are serializable registry components. Scene packets cap visible local lights at 4,096 and build
deterministic 16x16 tile lists with 128 lights per tile and explicit overflow statistics. The renderer uploads the full
light array, compact tile records, and packed light indices to graphics storage buffers. PBR and default-material
fragments consume only the current tile's list. When GPU occlusion is active, Forward+ may compact those lists from the
same frame's local-light visibility mask; any frame, slot, surface-epoch, device-generation, count, buffer, or dispatch
mismatch preserves the CPU-built all-eligible-light lists. The first 62 lights retain the portable shadow-uniform ABI
while later lights remain unshadowed rather than disappearing. Directional lights remain outside the local-light mask.

The VFX and spatial-volume mask consumers are capability-gated and are advertised only while rendered GPU occlusion is
active and the device is running. Supported GPU sprites and meshes can compact visible instances, while ribbon renderers
are masked only as whole ordered groups. CPU VFX, volumetric or unbounded effects, invalid/stale bounds, and unsupported
deformation remain visible. Spatial-lighting ABI v3 can build a frame-owned per-draw record from visible reflection
probes and light-probe volumes; ABI v2 and invalid v3 ownership keep the embedded CPU selection. Recovery, resize, and
frame retirement invalidate these derived buffers by device generation and surface epoch before reuse.

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
display scale, then retain owner-only pending requests for an opaque `RenderView`. `EndFrame` first reserves one of the
configured 1–3 total accepted frame slots (default 2), then snapshots scene, VFX, runtime UI, and normalized ImGui data
into an immutable packet and enqueues it without drops. Capture does not begin while admission is blocked. `Flush()` is
an explicit owner-thread synchronous boundary; ordinary `EndFrame` returns after admission, capture, and enqueue.

The render thread exclusively creates, uses, and releases GPU objects and mutable renderer caches. For depth `N`, each
surface epoch owns `N` worksets and `N+1` final outputs: one published image plus one writer per accepted slot. Resize
and device epochs remain leased until their packets and fences retire. A fence retires replaced textures, buffers,
instance data, light lists, and pipelines without a device-idle wait during normal resize churn. Frame-ID timelines
publish owner update, capture, admission, queue, render CPU, GPU retirement, and submit-to-present latency alongside
outstanding/high-water statistics.

Device loss pauses packet acceptance and simulation/managed/UI work at the Application owner safe boundary while
window and exit events continue. Recovery preserves the selected backend, abandons the lost generation without idle,
release, or cancel calls, recreates mandatory device/surface/UI resources on the render thread, and retries the
interrupted immutable packet once. The default policy permits an immediate attempt and one attempt after 250 ms;
attempt limits are configurable from 0–3 and reset only after 60 stable seconds plus 120 retired frames. Exhaustion
latches the first terminal failure. Healthy close drains accepted work; loss/failure close cancels unstarted work and
is idempotent. Test-only fault hooks are compiled only into test-capable configurations and are absent from Release and
Dist binaries.

The device lifecycle is explicit: `Running` transitions to `RecoveryPending` when a loss is classified,
`RecoveryPending` waits for the owner safe-boundary pause, and `Recovering` owns recreation. A successful candidate
returns to `Running`; an exhausted or non-recoverable attempt enters `Failed`. Shutdown transitions any live state
through `Closing` to `Closed`. A loss first observed in `Closing` or `Closed` never starts recovery. `Close()` is
`noexcept` and repeatable: healthy shutdown drains every accepted frame, while failed/lost-device shutdown cancels
unstarted packets, abandons unusable handles without a GPU-idle wait, and preserves the first terminal diagnostic.
Successful recovery resumes the same runtime or Play session without advancing simulation during the pause; only the
interrupted immutable packet is retried, and only once. Exhausted recovery terminates Play safely before final close.

One request may carry a primary scene plus up to 63 ordered `SceneRenderContribution` values. The primary scene owns
camera, environment, global material parameters, clear color, and directional-light selection; every contribution owns
its baked-lighting associations and adds geometry, local lights, probes, and independent CPU/GPU VFX snapshots.
Opaque and transparent preparation is global, with contribution order then entity ID providing deterministic ties.
Submitting a second independent request to the same surface remains an error, preserving one clear/tone-map/present
operation per surface. Runtime UI submissions append in call order until the frame ends, so later additive-scene
presentations draw above earlier ones; a null tree contributes nothing and does not clear prior commands. Runtime UI
vertex uploads use a high-water pool partitioned by accepted frame slot, so steady screen, camera, world, and
render-texture targets reuse GPU buffers and staging transfers after warm-up instead of allocating every frame.

Runtime contribution order is session load order. Persistent sessions retain their positions, while an unloaded and
later reloaded session appends. The active session supplies the primary camera, clear mode, environment, and the
lowest-entity-ID qualifying directional light. If it has no camera, the first loaded session with a camera becomes the
primary; if no loaded session has one, the surface renders only the clear result and UI. If the active session has no
qualifying directional light, selection falls back to the first light in session/entity order. Unloaded sessions are
absent from every packet captured after the lifecycle commit, while already accepted packets retain generation leases
until retirement. Pointer input visits presentation trees from newest to oldest until handled; keyboard and text input
go only to the focused presentation of the active session. A session with no presentation still contributes render
content.

Material dependencies are validated once per material, frame, and surface sample configuration; all draws in that
configuration reuse the immutable resolved binding. CPU-simulated VFX billboard and ribbon vertices are prepared before
the surface render passes and uploaded through the surface command buffer, so steady-state effects do not introduce a
separate GPU submission.

Scene color is linear RGBA16F. Multisampling resolves in HDR before a fullscreen fitted-ACES pass writes the sRGB
display surface. The renderer selects D32, D24, then D16 depth and falls back through 4x, 2x, then 1x sampling when a
requested format is unsupported. A hidden, minimized, zero-sized, or unavailable swapchain skips presentation safely.
Viewport surfaces keep their last valid image through transient zero-sized layout changes.

## Resolution Scaling

`Render Scale` controls the internal 3D surface size from 50% through 100%; the final image is spatially scaled to the
full Scene, Game, or player presentation rectangle. Camera aspect ratio, UI layout, pointer routing, and presentation
viewport remain based on the full logical size. This contract is independent of None, FXAA, TAA, and supported MSAA
modes.

`Dynamic Resolution: Automatic` starts at the lesser of Render Scale and Maximum Scale. Every eight completed renderer
frames it compares the configured target against GPU timestamps when the backend provides them, otherwise against
completed frame latency divided by the outstanding-frame count. It lowers scale quickly above the target, raises it
conservatively below the target, quantizes changes to 1/32 increments, and clamps every result to the authored minimum
and maximum. A dead band and completed-frame cadence prevent resize churn. Disabling dynamic resolution immediately
returns to the authored static Render Scale.

The same settings drive Scene view, Game view, camera preview, and cooked players. Runtime manifests preserve the full
render environment rather than reconstructing defaults. The Scene camera preview is an optional second complete render
surface and starts hidden; its toolbar camera button enables it when a live authored-camera comparison is useful.

## Views And Cameras

Scene view uses a nonserialized editor camera for navigation and draws a depth-tested grid plus visible mesh renderers
from the edit scene, or the runtime clone while playing. An authored Camera chooses **Skybox** or **Solid Color** for
Game view, its Scene camera preview, and standalone runtime. Solid Color uses that Camera's linear clear color; Skybox
uses the project environment. Scene view retains its independent editor viewpoint but follows the active authored
Camera's background choice so authoring and Game previews agree. This remains true during Play Mode: hovering Scene
drives only the editor camera, while the focused and hovered Game view owns managed gameplay input and its authored
primary camera. GPU occlusion is likewise camera-local: something hidden from Game may remain visible from an oblique
Scene camera. The two viewport overlays report their own surface rather than implying global entity visibility. Scene's
bug button and Game's **GPU Bounds** control expose red/green culling bounds for their respective viewport cameras. Their
FPS and timing rows remain frame aggregates across every rendered surface. Scene's controls are:

Raw `RenderView` clients must provide finite camera matrices and color plus clip planes satisfying
`0 < NearPlane < FarPlane <= 10,000,000`; a rejected update leaves the previous camera intact.

- `F`: frame the selected entity's full imported bounds with visible padding; double-`F` locks the view to it.
- `Shift+F`: lock or unlock the view pivot to the selected entity.
- Alt+left drag: orbit.
- Middle drag: camera-local pan.
- Mouse wheel or Alt+right drag: perspective dolly or orthographic zoom; Shift accelerates it.
- Right drag and `WASDQE`: fly; Shift accelerates movement and the wheel adjusts fly speed without dollying.
- Arrow keys: walk and strafe while the Scene view is active.
- `Q`, `W`, `E`, and `R`: select View, Move, Rotate, and Scale tools while the Scene viewport is active.
- `X`, `Y`, and `Z`: snap orientation; `Persp/Ortho` toggles projection.

The Scene toolbar camera button toggles an on-demand live 16:9 preview of the selected primary/highest-priority game
Camera. It uses a separate renderer-owned view and therefore matches the Game camera without changing the editor
camera; keeping it hidden avoids submitting the scene twice while it is not needed.

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
Raw scene submission applies the same complete schema, environment-lighting, directional-shadow, and GPU-occlusion
validation before accepting a request.

When Sky Visible is enabled, the renderer draws a deterministic built-in equirectangular studio sky before scene
geometry. Assigning a Texture2D environment replaces it using the same project rotation, intensity, and exposure. The
custom path accepts LDR or RGBE Radiance HDR equirectangular maps plus horizontal/vertical cubemap cross and six-face
strip atlases. Layout and HDR encoding are versioned texture import data, so reimport and cooking preserve the same
sampling behavior across supported GPU backends.

This foundation renders asset-backed textured PBR meshes, global diffuse/specular image-based lighting, deterministic
directional and local-light shadow maps, GPU-consumed Forward+ light lists, instanced compatible geometry, sky
backgrounds, an RGBA16F/ACES pipeline, and the editor grid through a dedicated submission thread. Runtime
reflection-probe capture and custom raw GPU passes remain later milestones; imported reflection probes already
participate in same-frame spatial visibility and per-draw selection.
