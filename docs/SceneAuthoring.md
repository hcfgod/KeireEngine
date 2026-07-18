# Scene Authoring

The Scene, Hierarchy, Inspector, Project, and Console panels form the first scene-authoring workflow.

## Workflow

Create a scene from **File > New Scene**, **Assets > Create/Scene**, or the Project panel. Double-click a `.keirescene`
asset to open it. The Project panel owns source selection; the scene is decoded and validated before the existing scene
is replaced.

Hierarchy supports selection, empty-object creation, subtree duplication/deletion, and drag/drop reparenting. Inspector
edits object name, active state, position, Euler rotation (stored as a normalized quaternion), and scale. Scene provides
bounded undo/redo and explicit atomic Save. Runtime single-scene reload happens only after a successful source import.

## Dirty And Recovery Policy

Opening, creating, closing, exiting, or accepting the OS close request while a scene is dirty presents Save, Discard, and
Cancel. Cancel preserves the exact scene and selection. Save validates, durably replaces the source, reimports it, and
clears recovery state. Discard never changes the source asset.

Dirty scenes write a bounded periodic recovery snapshot beneath `Library/SceneRecovery`; detachment also makes a final
best-effort snapshot during abnormal shutdown. On the next open, Scene offers Restore or Discard. Restore loads the
snapshot as dirty authoring state and requires an explicit Save before it becomes project content. Recovery files, logs,
caches, workspace state, and cooked builds are ignored project-local data.

Rendering previews, gizmos, components, prefabs, play mode, and runtime serialization remain later subsystem milestones.

