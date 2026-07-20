# Rendering

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

- `F`: focus the selected entity.
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
textures; missing non-null textures retain the checkerboard diagnostic.

Every begun render frame is completed when exit is requested and cancelled without throwing during exceptional
unwinding. Render tests release scene/view references before GPU shutdown and can be repeated in isolated processes
with Scripts/Tests/repeat-render-tests.ps1 or repeat-render-tests.sh.
The PBR suite measures neutral fallbacks, normal perturbation, metallic/roughness response, ambient occlusion, and
emissive output with tolerant regions. Windows shader catalogs retain DXIL and SPIR-V, and Vulkan pipelines bind the
shader manifest's declared SPIR-V entrypoint instead of assuming `main`.

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
