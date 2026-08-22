# Projects And The Editor

Use Kéire Hub as the normal entry point. Hub creates and registers projects, selects a compatible installed Editor,
and reports missing or damaged installations before launch. Direct Editor launch is useful for automation, but it does
not replace Hub's installation and compatibility view.

## Create A Project

1. Open Hub and choose **Create Project**.
2. Choose an installed Editor version and one of its available templates.
3. Enter the project name and destination. The final project directory must not already contain unrelated files.
4. Create the project and allow Hub to register it before opening the Editor.

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

Hub's **Open** action uses the exact or least-disruptive compatible Editor. **Open with** lets you select another
compatible installation explicitly. If a descriptor or content schema needs an upgrade, read the preview before
confirming: Kéire validates and stages upgrades transactionally, but a newer saved schema may no longer open in an
older Editor.

An Editor reports the project as locked while another live Editor owns its session. Do not bypass a valid lock. If a
previous process crashed, let the product's recovery flow prove the owner is gone before continuing.

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

## Create And Save A Scene

Create a scene with **File > New Scene**, **Assets > Create/Scene**, or the Project panel's Create menu. Double-click a
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

The Project panel creates Folder, Scene, Material, C# Script, C# ScriptableObject Class, Managed Assembly, graph, VFX,
and other supported asset types. The ScriptableObject class template includes stable type identity and
`CreateAssetMenu`, so it appears as an authorable data-asset type after managed compilation. Dragging a Hierarchy
entity onto a Project folder creates a prefab. Asset operations validate paths and use recoverable transactions where
the operation supports them.

Scene and graph undo contexts are document-local. Text editing retains local text undo. A Play Mode undo never mutates
Edit Mode history. When an operation is rejected, read the Console before repeating it; rejection is intended to leave
the document unchanged.

Continue with [C# Scripting Fundamentals](ScriptingFundamentals.md), or use [Scene Authoring](../SceneAuthoring.md) for
the complete selection, recovery, lighting, and Play Mode Changes contracts.
