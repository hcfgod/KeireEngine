# Rendering

## Static-scene submission contracts

The renderer consumes mesh schema v3 as ordered LOD, submesh, and material-slot records. Schema v1 and v2 meshes
decode as one LOD with one submesh and one slot. Submission selects a projected-height LOD, frustum-tests its submesh
bounds, resolves indexed material overrides or imported defaults, and creates a deterministic draw order. Opaque and
masked work is state sorted; premultiplied blend work remains depth-tested, disables depth writes, and sorts
back-to-front with stable entity/submesh tie breaking.

Material schema v2 owns `Opaque`, `Mask`, and `Blend` alpha mode, alpha cutoff, and double-sided state. Mask surfaces
write depth and reject fragments below their cutoff. A shader schema-v1 blend flag remains a compatibility default.
Failed material or shader revisions retain the complete last-good binding.

The private frame graph declares resource upload, directional-shadow, Forward+ culling, opaque/mask, sky,
transparency, ACES tone-map, overlay, readback, and presentation passes. Its compiler validates transient reads,
derives deterministic hazard order, and records resource lifetimes. It is an internal backend contract, not a public
render-graph API.

Point and spot lights are serializable registry components. Scene packets cap visible local lights at 4,096 and build
deterministic 16x16 CPU tile lists with 128 lights per tile and explicit overflow statistics. This is the bounded
reference fallback used when the GPU compute route is unavailable. PBR draws consume the first deterministic 128
visible local lights through the optional fragment `b2/space3` block, using smooth range attenuation and inner/outer
spot-cone falloff on D3D12 and Vulkan. GPU tile-list consumption remains the next Forward+ step.

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
safe boundary, records scene/grid passes, resolves multisampling, composites UI, acquires the swapchain, and submits one
coordinated command buffer. A fence retires replaced textures, buffers, and pipelines without a device-idle wait during
normal resize churn.

Color rendering is linear into an sRGB target. The renderer selects D32, D24, then D16 depth and falls back through 4x,
2x, then 1x sampling when a requested format is unsupported. A hidden, minimized, zero-sized, or unavailable swapchain
skips presentation safely. Viewport surfaces keep their last valid image through transient zero-sized layout changes.

## Views And Cameras

Scene view uses a nonserialized editor camera for navigation and draws a depth-tested grid plus visible mesh renderers
from the edit scene, or the runtime clone while playing. The active authored Camera supplies the clear color to both
Scene and Game, so changing Camera clear color gives an immediate, consistent preview without replacing the Scene
viewpoint. Its controls are:

- `F`: frame the selected entity's full imported bounds with visible padding; double-`F` locks the view to it.
- `Shift+F`: lock or unlock the view pivot to the selected entity.
- Alt+left drag: orbit.
- Middle drag: camera-local pan.
- Mouse wheel or Alt+right drag: perspective dolly or orthographic zoom; Shift accelerates it.
- Right drag and `WASDQE`: fly; Shift accelerates movement and the wheel adjusts fly speed without dollying.
- Arrow keys: walk and strafe while the Scene view is active.
- `Q`, `W`, `E`, and `R`: select View, Move, Rotate, and Scale tools while the Scene viewport is active.
- `X`, `Y`, and `Z`: snap orientation; `Persp/Ortho` toggles projection.

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
transparent texels; authored transparency requires a future explicit surface mode instead of accidental compositing.
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
size, clipping planes, and linear clear color. `MeshRendererComponent` references Kéire-owned mesh/material asset IDs;
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

This foundation renders asset-backed textured PBR meshes plus the editor grid. Transparency sorting, shadows, IBL,
post-processing, multiple-light accumulation, compute, custom raw GPU passes, and a dedicated render thread remain
later milestones.
