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

Scene view uses a nonserialized editor camera and draws a depth-tested grid plus visible mesh renderers from the edit
scene, or the runtime clone while playing. Its controls are:

- `F`: focus the selected entity.
- `Shift+F`: lock or unlock the view pivot to the selected entity.
- Alt+left drag: orbit.
- Middle drag: camera-local pan.
- Mouse wheel or Alt+right drag: perspective dolly or orthographic zoom; Shift accelerates it.
- Right drag and `WASDQE`: fly; Shift accelerates movement and the wheel adjusts fly speed without dollying.
- Arrow keys: walk and strafe while the Scene view is active.
- `X`, `Y`, and `Z`: snap orientation; `Persp/Ortho` toggles projection.

Relative cursor capture exists only during fly navigation and is released on focus loss. Camera state is stored below
the project's `Library/Editor` directory, so navigation never dirties a scene or enters source control.

Game view selects an enabled, hierarchy-active primary `CameraComponent` by highest priority and then stable entity ID.
It shows an explicit empty state when no camera qualifies. Perspective and orthographic cameras validate their FOV or
size, clipping planes, and linear clear color. `MeshRendererComponent` references Kéire-owned mesh/material asset IDs;
the built-in cube is available without exposing a graphics backend.

## Current Scope

The built-in cube uses a first-party HLSL shader compiled reproducibly to DXIL, SPIR-V, and MSL. Its per-draw constants
contain model-view-projection plus the Mesh Renderer linear tint, so Inspector tint edits update Scene and Game in the
same frame. Base vertex colors are white and tint alpha is preserved while the pipeline remains opaque.

This foundation renders an unlit, depth-tested cube and editor grid. Directional lights remain authorable but do not
affect pixels yet. Textures, transparency sorting, shadows, PBR, post-processing, compute, custom raw GPU passes, and a
dedicated render thread remain later milestones.
