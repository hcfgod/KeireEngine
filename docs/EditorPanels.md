# Editor Panels And Commands

KeireClient is a composition of dockable tools built exclusively on Kéire's public UI façade. Dear ImGui and SDL remain
inside KeireCore. `SceneDocument` and `InputActionsDocument` own their respective authoring state and undo contexts;
closing a document deterministically clears its selections, operations, and dirty/recovery state. Scene View,
Hierarchy, Inspector, Input Actions, and Project Settings are panel objects that depend on narrow controller
interfaces. Asset Browser, Console, and Diagnostics remain independent panels, and thumbnail generation is a separate
bounded service. The workspace layer owns services, composes those panels, coordinates modal transitions, and
orchestrates each frame.

`EditorCommandRouter` is the single dispatch point shared by menu items and shortcuts. A command supplies its action
and an availability predicate, so disabled UI and keyboard behavior cannot diverge. The headless `KeireEditorTests`
target exercises document lifecycle and command dispatch without creating the complete editor application.

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
- `Ctrl/Cmd+Z` undoes; `Ctrl/Cmd+R`, `Ctrl/Cmd+Shift+Z`, and `Ctrl+Y` redo the active document.

Hierarchy creation lives in the top-level Entity menu and blank-space or row context menus. Create Empty, Create Child,
Directional Light, Duplicate, Rename, Delete, and drag/drop reparenting all operate on stable entities. There are no
permanent create/delete buttons consuming hierarchy space.

## Scene And Inspector

The Scene toolbar shows the authored scene and dirty state plus centered Play, Pause, Step, and Stop controls. It also
contains View/Move/Rotate/Scale tools (`Q/W/E/R`), Global/Local orientation, a snapping toggle, independent position,
rotation, and scale increments, and Gizmos visibility settings. Camera icons/frustums and Directional Light icons/rays
remain clickable overlays, while transform handle drags participate in scene undo. Play
clones the current in-memory edit scene with the same entity IDs. Save always targets the authored scene. Runtime
changes are discarded on Stop.

The Project Settings panel is opened from the Edit or Window menu. Its Rendering section owns ambient color, ambient
intensity, and exposure for both Scene and Game views; settings are saved atomically under the project's tracked
`ProjectSettings` directory.

Inspector uses bordered, foldable component cards. Transform presents compact, responsive X/Y/Z drag fields for local position,
Euler-degree rotation backed by a normalized quaternion, and scale. The fields support direct numeric entry, stack
their labels in narrow inspectors, and retain uniform-scale locking. Directional Light exposes linear color, intensity,
temperature, shadow mode, strength, and bias. Add Component searches the application-owned component registry. Missing
Components remain visible through their preserved serialized records rather than being deleted silently.

Components outside the built-in ergonomic cards are rendered from `ComponentRegistration::Properties`; no
Inspector-specific code is required. `PropertyDrawerRegistry` supplies Boolean, Integer, Scalar, Text, Vector2,
Vector3, Vector4, Quaternion, Color, Asset, and Entity drawers. A component type plus property key may override a generic
drawer. Mesh and Material metadata carries its expected asset type so those fields show filtered pickers, while Entity
fields select from the open scene. An edit is validated against a fresh component first, records undo only after that
validation succeeds, and restores the original property bag if applying the validated change unexpectedly fails.

## UI Resource Boundary

`UiImage` is an opaque reference-counted image owned by the UI service. Call `UiFrame::CreateImage` on the UI owner
thread with RGBA8 pixels; the private rendered backend uploads and releases the texture safely. Retained images become
inert after UI shutdown. Clients never receive a GPU texture, native handle, or ImGui identifier.
