# Scene Authoring

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

The Scene toolbar controls Play, Pause, Step, and Stop. Play clones the current authored scene while retaining entity
IDs. Pause freezes lifecycle updates, Step performs one fixed tick, and Stop discards all runtime mutations. A callback
failure pauses the runtime clone and writes one actionable Console diagnostic without changing authored state.

## Dirty And Recovery Policy

Opening, creating, closing, exiting, or accepting the OS close request while a scene is dirty presents Save, Discard, and
Cancel. Cancel preserves the exact scene and selection. Save validates, durably replaces the source, reimports it, and
clears recovery state. Discard never changes the source asset.

Dirty scenes write a bounded periodic recovery snapshot beneath `Library/SceneRecovery`; detachment also makes a final
best-effort snapshot during abnormal shutdown. On the next open, Scene offers Restore or Discard. Restore loads the
snapshot as dirty authoring state and requires an explicit Save before it becomes project content. Recovery files, logs,
caches, workspace state, and cooked builds are ignored project-local data.

Scene view renders the edit scene or active runtime clone with a depth-tested grid. `F` frames transformed renderer and
child bounds, `Shift+F` locks the pivot, Alt+left orbits, middle drag pans in camera space, the wheel/Alt+right zooms,
and right drag plus `WASDQE` flies. Shift accelerates navigation; the wheel adjusts fly speed; arrow keys walk/strafe;
the toolbar snaps axes and toggles perspective/orthographic projection. Horizontal orbit/fly motion follows pointer
motion while vertical motion retains Unity's pitch convention. Game view renders the deterministic active scene Camera
from the runtime clone during Play. Scene-camera state stays below `Library/Editor` and never dirties source content. See
[Rendering](Rendering.md). Gizmos, prefabs, scripting, native module hot reload, and applying Play changes remain later
subsystem milestones.
