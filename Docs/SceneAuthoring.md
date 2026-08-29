# Scene Authoring

Inspector and Hierarchy authoring is routed through `SceneDocument` commands. Commands resolve the active Edit or
Play scene, validate entity/component membership and registry property types, update selection after structural
changes, and preserve the existing undo transaction created by the invoking editor control. Mesh Renderer authoring
supports named indexed material slots plus cast/receive-shadow flags; the slot-zero Material control remains
compatible with existing scenes. Point Light and Spot Light components can be created from the Hierarchy or generic
component registry and edited by the component-driven Inspector. Scene icons are pickable for both types; selecting a
point light shows its three-axis range sphere, while selecting a spot light shows its direction and outer cone.
Reflection Probe and Light Probe Volume entities are available from the same menus; their oriented bounds are visible
and pickable in Scene view.

## Editing during Play mode

The Hierarchy, Inspector, transform gizmos, and Scene viewport asset drops target the isolated runtime scene while
Play mode is active. These edits affect the running scene immediately and use a separate Play-mode undo history; the
authored scene remains unchanged until Play stops.

Stopping with runtime differences opens **Play Mode Changes**. Changes are grouped by entity and component and show
their previous and runtime values. Deliberate editor changes are selected by default, while simulation-only changes
remain available but unchecked. If simulation changes a path after an editor edit, it is marked **Mixed** and retains
the runtime-produced final value. Required created parents/components are selected and locked; a created child may be
explicitly kept at the scene root before its created parent is omitted. Deletions supersede conflicting descendant or
property edits, and unavailable registered-component payloads remain round-trippable. **Apply Selected and Stop**
applies the validated selection as one authored-scene undo
step and marks the scene dirty. **Discard and Stop** restores the exact authored scene, and **Cancel** resumes the prior
Playing or Paused state. Scene close, project changes, and application exit pass through this review before the normal
unsaved-scene prompt.

Ctrl-click toggles entities in both the Scene viewport and Hierarchy. In Hierarchy, Shift-click selects the inclusive
display-order range from the previous anchor and Ctrl/Cmd+Shift-click adds that range. Dragging from anywhere in the viewport except an
active gizmo handle draws a marquee and selects every active entity whose projected bounds intersect it; Ctrl-drag adds
to the existing set. Gizmo handles consume their press before picking begins, so starting a transform never clears the
selection. The last selected entity is the primary selection shown in Inspector and supplies the gizmo pivot. A gizmo
transform applies to every selected root; selected descendants follow their selected parent once instead of receiving a
second transform. Duplicate and Delete apply to the complete selection as one scene undo operation. Editor Ctrl/Cmd-Z
and redo shortcuts are globally routed from Scene and Hierarchy focus while active text controls retain local undo.

The Scene, Hierarchy, Inspector, Project, and Console panels form the first scene-authoring workflow.

## Workflow

Create a scene from **File > New Scene**, **Assets > Create/Scene**, or the Project panel. Double-click a `.keirescene`
asset to open it. The Project panel owns source selection; the scene is decoded and validated before the existing scene
is replaced.

Hierarchy supports recursive entities, multi-level selection stability, context-menu creation, subtree
duplication/deletion, and ordered drag/drop reparenting. Drop on the upper or lower quarter of a row to insert before or
after that sibling, drop on its center to parent beneath it, or drop on blank Hierarchy space to unparent and append at
the scene root. World transforms are preserved and a rejected cycle leaves the hierarchy unchanged. Entity creation is
available through the top-level **Entity** menu and blank-space or row context menus. `F2`, `Delete`, and `Ctrl/Cmd+D`
route to the same commands; multi-selection duplication copies only selected roots so selected descendants are not
duplicated twice.

Inspector edits foldable Unity-style component cards that retain their bordered presentation when collapsed. Transform
position, Euler rotation (stored as a normalized quaternion),
scale, and parent hierarchy are component data. Camera exposes projection, primary selection, priority, clipping, and
Skybox/Solid Color background, and linear clear color. Mesh Renderer exposes mesh/material IDs, tint, visibility,
cast/receive-shadow flags, and static lightmap/GI participation. Directional, point, and spot lights expose realtime,
baked, or mixed operation, shadow quality and resolution, contact shadows, cookies, and indirect contribution. Use
**Window > Lighting** to configure bake quality, lightmap density, padding, samples, bounces, AO, and denoising, then
queue a cached bake or force a rebuild. The resulting lighting-set reference is saved with scene schema v6.
The entity header exposes a Unity-style **Layer** dropdown beside the common name/active controls. It reads the 32 names
from Project Settings, applies one validated undoable edit, and edits every selected entity when the Inspector is not
locked. A mixed selection displays **Mixed** until a layer is chosen. Layer edits persist through scene saves,
duplication, prefabs, and selective Play Mode Changes.
Scene uses its own bounded `UndoContext`, `Ctrl+S`, and explicit atomic Save. The global Edit menu and
`Ctrl/Cmd+Z`, `Ctrl/Cmd+R`, `Ctrl/Cmd+Shift+Z`, or `Ctrl+Y` route to the focused document history. Continuous Transform and tint
drags are one undo entry. `Ctrl+Shift+S` uses an asynchronous native dialog,
requires a new `.keirescene` inside project Assets, assigns a new asset identity, and switches to the copy.
`Ctrl/Cmd+S` is routed globally so a focused Inspector or text field cannot consume it. Saving an existing scene writes
its source atomically and returns immediately; runtime catalog rebuilding and handle reload happen in the background.
After a scene opens successfully, the editor records its stable asset ID beneath
`Library/UserSettings/Workspace/EditorSession.state`. The next launch reopens that scene after catalog validation; if
it was deleted, changed to another asset type, or the session record is malformed, startup falls back to the project
descriptor's default scene without rewriting project content. Save As adopts and records the new scene identity.

The centered main editor bar controls Play, Pause, Step, and Stop from either the Scene or Game tab. Play clones the
current authored scene while retaining entity IDs. Pause freezes lifecycle updates, Step performs one fixed tick, and
Stop opens the selective change review when runtime state differs. A callback failure pauses the runtime clone and
writes one actionable Console diagnostic without changing authored state.

## Dirty And Recovery Policy

Opening, creating, closing, exiting, or accepting the OS close request while a scene is dirty presents Save, Discard, and
Cancel. Cancel preserves the exact scene and selection. Save validates, durably replaces the source, reimports it, and
clears recovery state. Discard never changes the source asset.

Dirty scenes write a bounded periodic recovery snapshot beneath `Library/SceneRecovery`; detachment also makes a final
best-effort snapshot during abnormal shutdown. On the next open, Scene offers Restore or Discard. Restore loads the
snapshot as dirty authoring state and requires an explicit Save before it becomes project content. Recovery files, logs,
caches, workspace state, and cooked builds are ignored project-local data.

Scene view renders the edit scene or active runtime clone with a depth-tested grid. A compact top-left overlay owns
View/Move/Rotate/Scale, Local/Global, Snap, and settings; the top-right overlay owns projection and axis snaps. These
overlays consume pointer input only inside their visible rectangles, leaving selection, marquee, gizmos, drops, and
camera navigation active everywhere else. The camera button in the top-right overlay toggles a live 16:9 preview of
the active main game camera in the lower-right corner; the preview itself blocks scene picking, while the rest of the
viewport remains interactive. `F` frames imported renderer metadata and transformed child bounds with
aspect-aware padding while the Scene view is focused or hovered; a double-`F` locks the camera to the selected entity,
and `Shift+F` toggles that lock. Alt+left
orbits, middle drag pans in camera space, the wheel/Alt+right zooms,
and right drag plus `WASDQE` flies. Shift accelerates navigation; the wheel adjusts fly speed; arrow keys walk/strafe;
the orientation overlay snaps axes and toggles perspective/orthographic projection. Horizontal orbit/fly motion follows
pointer
motion while vertical motion retains Unity's pitch convention. Game view renders the deterministic active scene Camera
from the runtime clone during Play. Scene-camera state stays below `Library/Editor` and never dirties source content.

Canvas defaults to **Screen Space Overlay** for compatibility. **Screen Space Camera** follows its selected Camera (or
the active presentation Camera when none is assigned), remains fitted to the viewport, and is hidden when its authored
plane distance lies outside that Camera's clip range. **World Space** uses the Canvas entity Transform,
reference resolution, and world-units-per-pixel scale to define a plane. Scene view projects that plane through the
editor camera, picks it with the matching ray-to-plane transform, and shows direct corner/center handles for undoable
Canvas and Rect Transform edits. World canvases behind the current camera are neither drawn nor hit-tested. This first
authoring slice does not test runtime UI against scene depth, so geometry cannot yet occlude a world Canvas; the Canvas
Inspector reports that limitation rather than exposing an inactive depth option.

Closing the editor cancels queued catalog refreshes instead of forcing pending background import work to finish;
already-saved scene and material sources remain durable and refresh on the next launch. See
[Rendering](Rendering.md). Prefab composition and managed scripting are supported authoring workflows; see
[Scene System](SceneSystem.md) and [C# Scripting](Scripting/README.md). Native C++ module hot reload is not part of the
supported editor contract.
