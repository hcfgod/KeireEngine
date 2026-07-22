# Scene Authoring

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

Ctrl-click toggles entities in both the Scene viewport and Hierarchy. Dragging from anywhere in the viewport except an
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
duplication/deletion, and drag/drop reparenting. Entity creation is available through the top-level **Entity** menu and
blank-space or row context menus. `F2`, `Delete`, and `Ctrl+D` route to the same operations.

Inspector edits foldable Unity-style component cards that retain their bordered presentation when collapsed. Transform
position, Euler rotation (stored as a normalized quaternion),
scale, and parent hierarchy are component data. Camera exposes projection, primary selection, priority, clipping, and
linear clear color. Mesh Renderer exposes mesh/material IDs, tint, and visibility. Directional Light color, intensity,
temperature, and orientation feed the built-in Lambert pass; shadow settings remain authoring data for a later pass.
Scene uses its own bounded `UndoContext`, `Ctrl+S`, and explicit atomic Save. The global Edit menu and
`Ctrl/Cmd+Z`, `Ctrl/Cmd+R`, `Ctrl/Cmd+Shift+Z`, or `Ctrl+Y` route to the focused document history. Continuous Transform and tint
drags are one undo entry. `Ctrl+Shift+S` uses an asynchronous native dialog,
requires a new `.keirescene` inside project Assets, assigns a new asset identity, and switches to the copy.
`Ctrl/Cmd+S` is routed globally so a focused Inspector or text field cannot consume it. Saving an existing scene writes
its source atomically and returns immediately; runtime catalog rebuilding and handle reload happen in the background.

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
camera navigation active everywhere else. `F` frames imported renderer metadata and transformed child bounds with
aspect-aware padding while the Scene view is focused or hovered; a double-`F` locks the camera to the selected entity,
and `Shift+F` toggles that lock. Alt+left
orbits, middle drag pans in camera space, the wheel/Alt+right zooms,
and right drag plus `WASDQE` flies. Shift accelerates navigation; the wheel adjusts fly speed; arrow keys walk/strafe;
the orientation overlay snaps axes and toggles perspective/orthographic projection. Horizontal orbit/fly motion follows
pointer
motion while vertical motion retains Unity's pitch convention. Game view renders the deterministic active scene Camera
from the runtime clone during Play. Scene-camera state stays below `Library/Editor` and never dirties source content.
Closing the editor cancels queued catalog refreshes instead of forcing pending background import work to finish;
already-saved scene and material sources remain durable and refresh on the next launch. See
[Rendering](Rendering.md). Prefabs, scripting, and native module hot reload remain later subsystem milestones.
