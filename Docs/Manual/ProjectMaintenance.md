# Back Up, Share, and Recover a Project

Use this guide before moving a project, putting it in source control, cleaning generated files, or recovering from a
failed import. The goal is to preserve authored content and stable asset identities while keeping caches out of your
shared project history.

**You need:** a project that opens in a compatible Editor, enough space for a separate backup, and access to its project
folder. Close Play Mode before preparing a backup. Close the Editor before copying or moving the whole project.

## Know What Belongs to the Project

| Location | What it contains | What to do with it |
| --- | --- | --- |
| `Assets/` | Scenes, materials, shaders, scripts, assemblies, UI, models, textures, and other authored content. | Back up and version the complete tree. |
| Adjacent `.keiremeta` files | Stable asset identities and importer settings. | Keep each file with its source asset, including during moves and restores. |
| `ProjectSettings/` | Project identity, engine requirements, player settings, build profiles, and scenes in the player. | Back up and version the complete directory. |
| `.gitignore` | Rules that keep generated project files out of source control. | Keep it with the project; review changes deliberately. |
| `Library/` | Import caches, user-local Editor state, locks, and potentially recovery or transaction data. | Normally ignore in source control. Preserve a copy before attempting recovery or a cache reset. |
| `Logs/` | Project diagnostics. | Keep relevant logs for troubleshooting; avoid treating them as authored content. |
| `Temp/` | Temporary work. | Ignore in source control; do not clean while an Editor or build owns it. |
| `Build/` | Generated player outputs and staging work. | Archive a release separately when needed. It does not replace your source project. |
| Root `.csproj` and `.sln` files | Generated external-editor project files. | The project creator ignores these root files; keep your actual C# and assembly-definition sources under `Assets/`. |

The project marker is **`ProjectSettings/Project.keireproject`**, not a file at the project root. It contains the project
identity and compatibility requirements. A folder containing only an executable, a `.keirepak`, or `Library/` is not a
source project.

## Make a Recoverable Backup

1. Stop Play Mode. Review any pending Play Mode changes before deciding what should become authored scene data.
2. Save dirty scenes and authoring documents. An open window showing an edit is not proof that its source was saved.
   Materials commit through their Inspector edit boundary; graph, UI, scene, and settings documents have their own
   save behavior. Use the guide for the document you are editing.
3. Wait for imports and managed compilation to finish, then close the Editor. Do not copy a live project during
   package import, migration, or a player build.
4. Copy the entire project into a separate backup location. For a first recovery backup, include `Library/` and logs;
   you may need their recovery evidence even though they are excluded from ordinary source control.
5. Confirm that the copy includes `ProjectSettings/Project.keireproject`, the authored assets, their metadata, and the
   referenced scripts and assembly definitions.
6. Keep the backup untouched. Perform recovery experiments in another working copy.

**Expected result:** you have a complete source snapshot independent of the installed Editor, its download cache,
and any built player.

A complete backup retains the same project identity. Avoid opening the original and its restored copy simultaneously
as if they were unrelated projects. Use the restored copy deliberately and check the path Hub is opening.

## Put a Project in Source Control

1. Start with the `.gitignore` created for the project. It excludes `/Library/`, `/Logs/`, `/Temp/`, `/Build/`, and
   generated root solution/project files.
2. Add authored source and its adjacent metadata together. Include `ProjectSettings/` and any project-owned files
   required by your build or gameplay workflow.
3. Before committing, inspect the changed-file list. A missing `.keiremeta` file or a scene pointing at a missing asset
   is a content problem, not something a teammate's import cache can repair reliably.
4. On another working copy, open the project through Hub with a compatible Editor, allow imports and compilation to
   finish, then open the intended scene and test it in Play Mode.
5. For a release, also build and run a standalone player. A project that opens successfully has not yet proved that
   its player scene list and cooked dependency closure are complete.

Treat successful clean-checkout imports as a repeatable project check. Do not commit somebody else's `Library/` to
hide missing source assets or metadata.

## Move a Project to Another Folder or Computer

1. Save, stop work, and close the Editor as in the backup procedure.
2. Copy the project source and metadata together. Preserve the relative layout under `Assets/` and `ProjectSettings/`.
3. On the destination computer, install an Editor compatible with the project's requirements. Add the Build Support
   needed for your intended target separately; copying a project does not install Editor components.
4. In Hub, add/open the project at its new location. Verify the displayed path before launching it.
5. Allow local caches to rebuild. Check Console diagnostics, load the saved scene, and test the required scripts,
   materials, UI, and input.
6. Make a new standalone build before removing your original backup.

Removing an old project entry from Hub removes discovery state; it does not delete the source project. A moved
project's old recent entry may remain until it is removed or refreshed.

## Investigate an Import or Recovery Problem

Start with the failed asset or diagnostic, not a wholesale cache deletion:

| Symptom | First check | Continue with |
| --- | --- | --- |
| A scene opens with missing assets. | The source asset and its original `.keiremeta` both exist. | [Entities, Prefabs, Assets, and Scenes](WorldAndAssets.md). |
| Material or shader edits do not appear. | The current import/compile diagnostics and the material's selected shader. | [Materials](MaterialGraph.md) and [Shader Graph](ShaderGraph.md). |
| A script or component is missing. | Its source, assembly definition, and compilation errors. | [Scripting Fundamentals](ScriptingFundamentals.md). |
| The Editor layout is unexpected. | You are in the intended project and using the intended workspace layout. | [Projects and Editor](ProjectsAndEditor.md). |
| The project is reported as already open. | Another Editor process still owns the project. | [Hub Installation and Recovery](HubInstallationAndRecovery.md). |
| A conversion, upgrade, or import was interrupted. | Its retained journal, backups, and diagnostics. | The specific migration or package guide; preserve recovery files before changing them. |
| A player starts in the wrong scene. | The first enabled row in **Scenes In Build**. | [Player Builds and Packages](PlayerBuildsAndPackages.md). |

If you need to test fresh generated state, first preserve the complete project, including recovery data, close all
owners, and work in a disposable copy. Do not delete `Library/` while an import, save, migration, or build is running.
Do not remove transaction journals to make an error disappear. Record the original diagnostic and what you changed.

## Replace Template Content Carefully

Starter templates are ordinary project content, but their scenes, scripts, assembly definitions, and UI can reference
one another. Remove a template feature as a unit:

1. Back up the project and stop Play Mode.
2. Remove or replace the scene components and objects that use the template feature.
3. Remove its unused source assets through the Project workflow so metadata and references are handled together.
4. Remove unused template scripts only after their component references have been removed. Keep the assembly
   definitions that still own your own scripts.
5. Resolve compilation and missing-reference diagnostics, save, and reopen the project.
6. Test both Play Mode and a standalone player with your replacement feature.

For your own interface, follow [UI, Jobs, and Diagnostics](UiJobsAndDiagnostics.md). Removing a UI document alone does
not remove a template Behaviour that tries to load or query it.

## Before You Call a Restore Successful

- The restored project opens through Hub using the intended Editor and project path.
- Imports and managed compilation complete without unresolved errors.
- Saved scenes, assets, material assignments, and UI documents survive a close/reopen cycle.
- Gameplay runs in Play Mode and in a newly built standalone player.
- The original backup remains available until you have checked the restored work.

Implementation references: `KeireCore/Source/Project/Project.cpp` creates the project layout and ignore rules;
`KeireCore/Source/Project/Project.cpp` validates project identity; [Asset Pipeline](../AssetPipeline.md) describes
source/metadata ownership and recoverable Project operations.
