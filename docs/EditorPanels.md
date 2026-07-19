# Editor Panels And Commands

KeireClient is a composition of dockable tools built exclusively on Kéire's public UI façade. Dear ImGui and SDL remain
inside KeireCore. Asset Browser, Console, and Diagnostics are independent panel classes, and thumbnail generation is a
separate bounded service. The workspace layer coordinates project services, shared selection/undo/console state, scene
runtime, and panel registration rather than reimplementing those panel views.

## Global Commands

Edit menu history is supplied by application-owned `UndoService`. Focus selects the scene, Input Actions, Project
assets, or theme context, so one tool never clears another tool's redo stack. Labels identify the pending command, such
as `Undo Change Tint` or `Redo Rename Asset`; docking geometry remains outside document history.

The File menu and keyboard routing share the same scene operations:

- `Ctrl+S` saves the authored scene atomically.
- `Ctrl+Shift+S` opens the asynchronous Save As dialog.
- Save As is constrained to the project's `Assets` directory, requires `.keirescene`, creates a new metadata identity,
  and switches the editor to the new asset.
- `Ctrl+D`, `F2`, and `Delete` duplicate, rename, and delete the selected entity.
- `Ctrl/Cmd+Z`, `Ctrl/Cmd+Shift+Z`, and `Ctrl+Y` undo or redo the active document.

Hierarchy creation lives in the top-level Entity menu and blank-space or row context menus. Create Empty, Create Child,
Directional Light, Duplicate, Rename, Delete, and drag/drop reparenting all operate on stable entities. There are no
permanent create/delete buttons consuming hierarchy space.

## Scene And Inspector

The Scene toolbar shows the authored scene and dirty state plus centered Play, Pause, Step, and Stop controls. Play
clones the current in-memory edit scene with the same entity IDs. Save always targets the authored scene. Runtime
changes are discarded on Stop.

Inspector uses component cards. Transform presents compact, responsive X/Y/Z drag fields for local position,
Euler-degree rotation backed by a normalized quaternion, and scale. The fields support direct numeric entry, stack
their labels in narrow inspectors, and retain uniform-scale locking. Directional Light exposes linear color, intensity,
temperature, shadow mode, strength, and bias. Add Component searches the application-owned component registry. Missing
Components remain visible through their preserved serialized records rather than being deleted silently.

## UI Resource Boundary

`UiImage` is an opaque reference-counted image owned by the UI service. Call `UiFrame::CreateImage` on the UI owner
thread with RGBA8 pixels; the private rendered backend uploads and releases the texture safely. Retained images become
inert after UI shutdown. Clients never receive a GPU texture, native handle, or ImGui identifier.
