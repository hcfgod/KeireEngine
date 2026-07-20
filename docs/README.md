# Documentation

Kéire documentation is organized by task. The root [README](../README.md) is the project overview and command
reference; these guides provide the details needed to develop, integrate, validate, and release the engine.

| Guide | Purpose | Primary audience |
| --- | --- | --- |
| [Getting Started](GettingStarted.md) | Clone, bootstrap, generate, build, run, clean, and diagnose a workstation | New contributors and integrators |
| [Architecture](Architecture.md) | System boundaries, ownership, implementation strategy, and release shape | Engine maintainers |
| [Runtime Lifecycle](RuntimeLifecycle.md) | Startup, frame order, threading, layer mutation, events, time, UI, and shutdown | Runtime and layer authors |
| [Undo And Redo](UndoRedo.md) | Contexts, commands, transactions, limits, merging, threading, and editor routing | Runtime, editor, and tooling authors |
| [UI Workspace](UiWorkspace.md) | Panel registration, factory docking, layouts, themes, persistence, and recovery | Editor and tooling authors |
| [Asset Runtime](AssetRuntime.md) | Handles, fallbacks, async loading, mounts, integrity, reloads, and threading | Runtime and subsystem authors |
| [Asset Pipeline](AssetPipeline.md) | Metadata, import cache, file operations, cooking, validation, and CLI | Editor, content, and release authors |
| [Rendering](Rendering.md) | Render ownership, frame order, viewports, cameras, navigation, and current scope | Rendering and editor authors |
| [Project Settings](ProjectSettings.md) | Tracked rendering environment settings, validation, defaults, and editor workflow | Technical artists and editor authors |
| [Shaders And Materials](ShadersAndMaterials.md) | Shader manifests, compiler boundary, reflection, materials, and target cooking | Technical artists and rendering authors |
| [Asset Browser](AssetBrowser.md) | Folder navigation, List/Grid modes, thumbnails, selection, and file operations | Designers and editor authors |
| [Project System](ProjectSystem.md) | Project identity, directory isolation, locks, templates, and recent registry | Editor and tooling authors |
| [Project Hub](ProjectHub.md) | Create/open/reveal workflows, launcher behavior, and smoke modes | Artists, designers, and editor authors |
| [Scene System](SceneSystem.md) | Scene assets, mutable instances, async loading, activation, and events | Runtime and subsystem authors |
| [Scene Authoring](SceneAuthoring.md) | Hierarchy/Inspector workflow, dirty prompts, undo, save, and recovery | Designers and editor authors |
| [ECS And Components](ECSAndComponents.md) | Entities, registration, component lifetime, lifecycle, transforms, and schema v2 | Runtime and component authors |
| [Editor Panels And Commands](EditorPanels.md) | Panel boundaries, global commands, play toolbar, Inspector, and UI images | Editor and tooling authors |
| [Input System](InputSystem.md) | Devices, users, actions, snapshots, rebinding, overrides, and cursor modes | Runtime and gameplay authors |
| [Input Actions Editor](InputActionsEditor.md) | Templates, authoring, validation, undo, live monitor, and Listen | Designers and editor authors |
| [Input Debugger](InputDebugger.md) | Live action test mode, capture bypass, device/user state, and Console logs | Designers, QA, and input authors |
| [Testing And Release](TestingAndRelease.md) | Test matrix, sanitizers, smoke tests, scripts, packages, and handoff checks | Maintainers and release engineers |

## Sources Of Truth

Documentation explains the repository but does not replace its authoritative inputs:

- `AGENTS.md` defines repository-wide engineering and validation rules.
- `Config/Project.conf` defines project identity and target names.
- `Config/Dependencies.lock` defines immutable dependency repositories and commits.
- `.clang-format` defines first-party C++ formatting.
- Premake files define Kéire targets and compiler policy.
- Platform launchers under `Scripts/` define supported developer workflows.

When documentation and an authoritative input disagree, correct the documentation in the same change that resolves the
discrepancy.

## Documentation Maintenance

Keep guides task-oriented and link to the supported public API rather than private implementation types. Public API,
ownership, packaging, or workflow changes should update the relevant guide, the root README when user-facing behavior
changes, and `CHANGELOG.md` when engine or SDK users are affected.

Code snippets are production examples. They must use the public `Keire/` headers, follow `.clang-format`, and remain
consistent with the supported lifecycle and thread-affinity contracts.
