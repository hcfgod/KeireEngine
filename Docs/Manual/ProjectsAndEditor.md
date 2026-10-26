# Projects And The Editor

Use Kéire Hub as the normal entry point. Hub creates and registers projects, selects a compatible installed Editor,
and reports missing or damaged installations before launch. Direct Editor launch is useful for automation, but it does
not replace Hub's installation and compatibility view.

## Create A Project

1. Open Hub and choose **New project** on Home or **+ New Project** on Projects.
2. Choose an installed Editor version and one of its available templates.
3. Enter **Project name** and **Location**. Location is the parent folder: check the displayed **Destination** before
   continuing. Missing parent folders can be created, but the final project directory must not already exist, even empty.
4. Leave **Open in the selected editor after creation** enabled if you want to start immediately, then choose
   **Create project**. The project should appear in Projects and open with the selected Editor.

If no Editor or template is available, first complete [Hub installation and recovery](HubInstallationAndRecovery.md).
Templates belong to an Editor installation; switching versions can change the choices. A disabled Create action is a
validation result, not an instruction to create an empty destination yourself. Correct the field identified in the dialog.

A project has one `ProjectSettings/Project.keireproject` descriptor. Keep these boundaries intact:

```text
MyGame/
  Assets/             # Source assets and C# files you author
  Packages/           # Project package requirements and lock data, when used
  ProjectSettings/    # Project, player, input, and authoring settings
  Library/            # Generated imports, caches, script generations, and editor state
```

Do not copy another project's `Library/` directory into a new project. Stable asset metadata lives beside source
assets, while derived content is recreated locally.

## Open And Upgrade

Hub's **Open** action uses the exact or least-disruptive compatible Editor. **Open with another editor...** lets you select another
compatible installation explicitly. If a descriptor or content schema needs an upgrade, read the preview before
confirming: Kéire validates and stages upgrades transactionally, but a newer saved schema may no longer open in an
older Editor.

An Editor reports the project as locked while another live Editor owns its session. Do not bypass a valid lock. If a
previous process crashed, let the product's recovery flow prove the owner is gone before continuing.

If Hub reports that the project is already open, switch to its existing Editor window. Do not launch another copy or
delete its lock to get past the notice. Removing a project from Hub removes its registration, not its files. Use the
missing-project relocation workflow only when the moved descriptor still identifies the same project.

## Read The Default Workspace

| Surface | Use it for |
| --- | --- |
| **Hierarchy** | Create, rename, parent, order, duplicate, and select scene entities. |
| **Scene** | Edit transforms, select geometry and lights, and inspect the authored world. |
| **Game** | See the active camera and interact with Play Mode input and UI. |
| **Inspector** | Edit the primary selected entity, component, or asset. |
| **Project** | Browse, search, create, import, rename, duplicate, and move project assets. |
| **Console** | Read managed build, import, runtime, graph, and validation messages. |
| **Diagnostics/Profiler** | Inspect structured failures and frame/runtime health. |

The centered toolbar owns **Play**, **Pause**, **Step**, and **Stop**. The Scene, Game, Hierarchy, Inspector, and Project
panels are separate views of the same current authoring session; closing a panel does not delete its content.

In **Game**, choose **Preview > Free**, **16:9**, **16:10**, or **4:3**. Free fills the available view; fixed ratios
are centered with letterboxing. **Maximize On Play** expands Game for play testing. **FPS** toggles the workspace's
performance overlay, while **GPU Bounds** visualizes visibility decisions for the Game camera, independently of Scene.
These controls wrap onto more rows as you narrow the dock, and the camera caption wraps below them. Widen a dock that
cannot fit a single label; make it taller if the controls leave too little room for a drawable preview.
See [Debugging And Profiling](DebuggingAndProfiling.md)
for the scope and limitations of the performance and bounds displays.

## Create And Save A Scene

Create a scene with **File > New Scene** or the Project panel's **Create > Scene** menu. Double-click a
`.keirescene` asset to open it. Use the Hierarchy's **Create Empty**, light entries, and child commands to build the
entity tree. **Add Component** in the Inspector searches both built-in and successfully compiled managed components.

`Ctrl/Cmd+S` saves the active document even when a text field has focus. Dirty-scene close, project switch, and Editor
exit offer **Save**, **Discard**, and **Cancel**. A decode or validation failure leaves the current scene unchanged.

## Play Mode Is Isolated

Entering Play clones the authored scene. Hierarchy, Inspector, gizmo, and Scene-view edits then target the runtime
clone and use a separate undo history. When you stop, **Play Mode Changes** shows eligible differences. Applying a
change is explicit; stopping never silently overwrites the authored scene.

Use this loop when trying an Inspector value:

1. Save the source scene.
2. Enter Play and make the runtime adjustment.
3. Stop and inspect the grouped change list.
4. Apply the intended field or component changes.
5. Save the source scene again.

## Assets And Undo

When an entity has repeated components, edit the card for the intended instance. In updated source builds, its fields
and enabled checkbox target that instance rather than the first component of the same type. Multi-selection matches
component instances by their order within each type, not by their displayed name. Reorder consistently before applying
a common edit. Lock the Inspector when you want to pin and edit one entity while changing the Hierarchy selection;
unlock it to resume selection-wide editing.

The Project panel creates Folder, Scene, Material, C# Script, C# ScriptableObject Class, Managed Assembly, graph, VFX,
and other supported asset types. The ScriptableObject class template includes stable type identity and
`CreateAssetMenu`, so its slash-separated menu path appears under **Create** after managed compilation. Choosing that
entry creates a typed `.keiredata` asset in the current folder and selects it for Inspector editing. If compilation or
type discovery fails, the Create menu keeps a ScriptableObject diagnostic entry visible instead of silently hiding the
workflow. Dragging a Hierarchy entity onto a Project folder creates a prefab. Asset operations validate paths and use
recoverable transactions where the operation supports them.

Scene and graph undo contexts are document-local. Text editing retains local text undo. A Play Mode undo never mutates
Edit Mode history. When an operation is rejected, read the Console before repeating it; rejection is intended to leave
the document unchanged.

## Replace Template Content Safely

Deleting a template UI entity removes that scene instance, not its visual-tree asset or C# implementation. Save a
checkpoint first. Remove unwanted scene entities, then identify their assets in Project and check whether another
scene or prefab still uses them. Remove unneeded scripts separately; shared scripts may still be required by remaining
components. Wait for compilation and fix missing references before testing again.

Use Project's recoverable asset deletion rather than deleting source and metadata independently in Explorer. Keep
`.keiremeta` files alongside surviving source assets. Do not delete `ProjectSettings` or package lockfiles as cleanup.
Test the replacement UI in Game, then test a standalone player: an Editor-only preview is not the final acceptance test.

Continue with [C# Scripting Fundamentals](ScriptingFundamentals.md), or use [Scene Authoring](../SceneAuthoring.md) for
the complete selection, recovery, lighting, and Play Mode Changes contracts.
