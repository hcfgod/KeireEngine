# Getting Started With C# Scripting

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

Kéire compiles project scripts into managed assemblies and publishes each successful build as an immutable generation.
A failed build never replaces the last working generation, so Play Mode can continue using the last-good scripts while
you correct diagnostics.

## On This Page

- [Prerequisites](#prerequisites)
- [Organize Scripts Freely](#organize-scripts-freely)
- [Assembly Definitions](#assembly-definitions)
- [Create A Behaviour](#create-a-behaviour)
- [Your First Play Mode Check](#your-first-play-mode-check)
- [Build And Attach](#build-and-attach)
- [IDE Projects](#ide-projects)
- [Build Output And Last-Good Behavior](#build-output-and-last-good-behavior)
- [First-Script Checklist](#first-script-checklist)

## Prerequisites

A scripting project needs:

- a project opened in Kéire Editor;
- a .NET 10 SDK available to the editor for project compilation;
- C# files anywhere under `Assets`.

Packaged games carry the runtime needed to execute already-cooked assemblies. Developing or changing scripts still
requires the SDK.

## Organize Scripts Freely

New projects do not create an assembly asset or require a `Runtime` folder. Unclaimed scripts compile into the built-in
`Assembly-CSharp` assembly. Unclaimed scripts below a folder named `Editor` compile into `Assembly-CSharp-Editor`
and can use the editor API. Folder names do not determine C# namespaces.

Create a **Managed Assembly** in the Project panel only when you need a custom boundary. Its folder and subfolders
belong to that assembly, except where another definition introduces a nested boundary. Creating an assembly does not
create a script. Inside a custom assembly, its explicit classification applies even to nested `Editor` folders.

For example, `Assets/Characters/Player.cs` needs no assembly asset. Adding `Assets/Characters/Characters.keireasm`
moves that folder's scripts into the custom assembly; adding a nested definition splits that subtree out again.

## Assembly Definitions

Schema version 4 is the current format; versions 1 through 3 remain readable:

```json
{
  "schemaVersion": 4,
  "name": "MyGame",
  "rootNamespace": "MyGame",
  "classification": "runtime",
  "sourceRoots": [],
  "references": [],
  "packages": [],
  "defineSymbols": [
    "MY_GAME"
  ],
  "autoReferenced": true,
  "allowUnsafe": false
}
```

| Property | Meaning |
| --- | --- |
| `schemaVersion` | `4` adds platform filters, constraints, version defines, and DLL reference controls |
| `name` | Unique C# assembly name |
| `rootNamespace` | Default namespace used by generated scripts and IDE projects |
| `classification` | `runtime`, `editor`, or `tests` |
| `sourceRoots` | Empty or omitted uses the definition folder; explicit project-relative roots remain supported |
| `autoReferenced` | Whether predefined assemblies reference this custom assembly (default true) |
| `references` | Asset IDs of other `.keireasm` definitions |
| `packages` | NuGet package name and exact, non-floating version pairs |
| `defineSymbols` | Unique valid C# preprocessor identifiers |
| `allowUnsafe` | Whether gameplay code in this assembly may compile unsafe blocks |

A custom assembly must explicitly reference another custom assembly before using its public classes, interfaces,
generic types, or methods. Predefined assemblies automatically reference eligible custom assemblies. Custom assemblies
cannot reference predefined assemblies; put shared types in a custom assembly when both need them. Tests are never
automatically referenced. Cycles and ambiguous folder ownership are errors.

Reference rules are deliberate:

- runtime assemblies may reference runtime assemblies;
- editor assemblies may reference runtime and editor assemblies;
- test assemblies may reference all classifications.

Duplicate names, missing references, invalid classification edges, and cycles fail graph validation before compilation.
Package versions must be exact; ranges, wildcards, and floating versions are rejected.

### Inspector And Assembly References

Select a `.keireasm` asset to edit its name, namespace, classification, references, platform filters, symbols, constraints,
version defines, and DLL controls in the Inspector. **Apply Assembly Settings** validates and saves the changes, then
requests a build; **Revert Assembly Settings** reloads the source. Apply rejects external edits made since the draft loaded.

Use **Create > Assembly Reference** to add an `.asmref` asset, then choose its target assembly in the Inspector. Its folder
and descendants join that assembly, with nested definitions/references establishing closer boundaries. A folder may hold
one definition or reference. References cannot target predefined assemblies or other references. JSON accepts an assembly
name or `"GUID:<asset-id>"`; the Inspector writes IDs so renames preserve the connection.

### Conditional Assemblies And DLL References

| Property | Meaning |
| --- | --- |
| `includePlatforms` / `excludePlatforms` | Mutually exclusive lists of `Windows`, `Linux`, `macOS`, or `Editor`; empty means all |
| `defineConstraints` | Every row must match; a row supports `SYMBOL`, `!SYMBOL`, and `A || B` alternatives |
| `versionDefines` | `{ "resource": "Example.Package", "expression": "[1.2,2.0)", "define": "HAS_PACKAGE" }` entries |
| `overrideReferences` | When false, reference managed DLLs found under Assets automatically |
| `precompiledReferences` | With overrides enabled, explicit project-relative DLL paths such as `Assets/Plugins/Utility.dll` |

Version expressions use semantic version ordering, including prereleases. A bare version is a minimum, `[1.2.3]` is exact,
`[1,2)` includes the lower bound and excludes the upper bound, and `(,2]` or `[1,)` permits an unbounded side. An empty
expression matches any installed version. Resources include `Keire`, locked project packages, and the assembly's exact
NuGet packages. A missing resource supplies no define. Version defines are local to the declaring assembly and participate
in its constraints. Define symbols also include `KEIRE_EDITOR` in editor builds and the selected `KEIRE_WINDOWS`,
`KEIRE_LINUX`, or `KEIRE_MACOS` platform symbol.

The native `ManagedAssemblyBuildContext` selects a target platform, editor/player mode, global symbols, and resource
versions for `ResolveProjectManagedAssemblies`. Its default selects the host platform in editor mode. The Editor and IDE use the active player profile target;
player cooking resolves again for that target in player mode. An assembly included
only on `Editor` is classified as editor code. Excluded assemblies retain ownership of their scripts, so excluded scripts
never fall into a predefined assembly. Active custom references to excluded assemblies are errors.

DLLs are identified by their CLR header; native DLLs are not automatic managed references. Explicit references must point
to managed DLLs inside Assets. Duplicate DLL names and names shadowing project or engine assemblies are rejected. Referenced
DLLs are copied to the published generation and included in input digests so edits invalidate build results. Generated IDE
projects use the same resolved symbols and DLL references as compilation.

These settings follow [Unity's assembly definition model](https://docs.unity3d.com/6000.0/Documentation/Manual/class-AssemblyDefinitionImporter.html).
Kéire retains its own managed API and `.keireasm` format; Unity scripts and `.asmdef` files are not directly interchangeable.
Unity's no-engine-references option and legacy first-pass special-folder assemblies are not implemented.

## Create A Behaviour

Create `LightSwitch.cs` in a runtime source root:

```csharp
using Keire;

namespace MyGame;

[StableComponentId("31f48d51-3502-4452-abfe-6e9af83fd83a")]
public sealed class LightSwitch : Behaviour
{
    [SerializeField, StableFieldId("0bce4b90-da78-4c6d-969c-03807d71504c")]
    private Entity? _light = null;

    protected override void Update()
    {
        if (Input.Keyboard.Current?.lKey.WasPressedThisFrame == true && _light is { IsValid: true })
            _light.Active = !_light.Active;
    }
}
```

Important conventions:

- The public `Behaviour` type and `.cs` filename must match.
- Use the ASCII namespace `Keire` in code. The accented name `Kéire` is for display text.
- Every attachable component needs a unique, durable `StableComponentId`.
- Prefer private Inspector fields marked `[SerializeField]`.
- Give persisted fields a durable `StableFieldId`; do not reuse an ID for a different meaning.

The editor-generated script command supplies IDs automatically. When writing a file manually, generate real UUIDs and
keep them stable after the script has been attached or serialized.

## Your First Play Mode Check

1. Open a project and save a scene containing a camera and a visible lit object.
2. Create `Assets/LightSwitch.cs` with the class above. The direct **L** key makes this first test independent of
   an authored action map. Use Input Actions once you need rebinding or gamepad support.
3. Wait for the managed build to succeed. Fix Console/build diagnostics before testing the new code.
4. Add a separate controller entity and attach `LightSwitch` through **Add Component > Scripts** or a script drag.
5. Drag the light's entity from the Hierarchy into the script's Light field. Keep the controller outside that light's
   hierarchy so disabling the target does not also disable the script receiving the next key press.
6. Enter Play Mode, focus the game input view, and press **L**. The assigned entity's local active state toggles.
7. Stop Play Mode. Runtime mutations are session state; save authored scene changes explicitly through the Editor
   workflow when you want them in the project.

If nothing happens, verify input focus, assignment, active hierarchy, enabled state, and the running generation.
An unassigned target is intentionally ignored. A target under an inactive parent remains inactive in hierarchy
even if its local `Active` flag becomes true. Continue with the [cookbook](Cookbook.md) for actions, physics, and UI.

## Build And Attach

The editor watches `.cs`, `.keireasm`, `.asmref`, and precompiled `.dll` files. After the newest change settles, it:

1. validates the assembly graph;
2. generates SDK-style projects targeting .NET 10 and C# 14;
3. compiles the engine API and affected gameplay assemblies;
4. validates the candidate type registry;
5. publishes a new immutable generation only when all steps succeed;
6. reloads active Play Mode instances transactionally.

Runtime script compilation keeps one warm Roslyn compiler owned by the script system, using a private connection
name. It never shares that connection with Visual Studio or another editor. Cancellation, SDK changes, and editor
shutdown stop the compiler; MSBuild node reuse remains disabled. The selected SDK resolves the compiler location,
and changes to ancestor SDK/build configuration restart the session. If discovery is unavailable, the ordinary
non-shared compiler path remains available. The first build includes compiler startup; later edits reuse it.
Build orchestration restores and builds the actual assemblies without compiling an empty wrapper assembly.
It starts with graph roots; SDK project references still restore and build their transitive dependencies. No restore
validation is bypassed. The Core log reports preparation, compiler setup, build, and publication timings separately.
API freshness scans retain file enumeration and metadata checks but avoid redundant filesystem path resolution.
For reproducible headless measurements, see [editor workflow performance](../EditorWorkflowPerformance.md).
Repeated C# change notifications compare actual contents before scheduling another build. When notified, same-size or
timestamp-preserving edits still rebuild, and unreadable/oversized sources fall back to normal build diagnostics. Explicit
Build Scripts bypasses notification deduplication. Assembly-definition changes always request a build.
Edits arriving during a build are combined into one follow-up build. The editor remains responsive instead of
waiting synchronously to cancel and restart compilation. Queued Play waits for the latest requested build and reload;
an intermediate successful generation is not reloaded while another build is pending.
An already loaded last-good runtime does not bypass an active replacement build when entering Play. The readiness
check also waits if the worker has published success but the editor has not requested that generation's reload yet.
If compilation fails, the existing last-good generation remains available. File-change detection is asynchronous;
Play requested before the editor detects a save may still enter the existing generation and subsequently hot-reload.
Unchanged scripts reuse MSBuild compiler outputs. Each successful build still publishes an independent runtime
generation; compiler caches live under `Library/ScriptAssemblies/Intermediate` and can be regenerated.

Attach a successfully compiled script with any of these editor workflows:

- choose **Add Component > Scripts** in the Inspector;
- drag the `.cs` asset onto the Inspector drop target;
- drag it directly onto a GameObject in the Hierarchy.

If the script is not in the active generation yet, the editor queues the attachment, builds and reloads, then completes
the attachment after the type becomes available.

## IDE Projects

Opening a C# source from the editor regenerates a project-root solution and one SDK-style project per predefined or custom assembly.
These files provide IntelliSense and navigation. The resolved assembly graph remains authoritative; editing only a generated
project does not change a runtime build. Once opened, the design-time workspace refreshes before each build and on every
script open, so new, renamed, moved, or failing sources remain part of their assembly for completion and navigation.

The editor's Visual Studio authoring façade may use a compatibility target for design-time support. Runtime gameplay
builds still use the engine's .NET 10 and C# 14 policy.

## Build Output And Last-Good Behavior

Successful builds are published below:

```text
Library/ScriptAssemblies/Generations/<generation>/
```

The active generation is recorded by the script system. Build intermediates remain under
`Library/ScriptAssemblies/Intermediate`. These are generated files; do not commit them.

When compilation, discovery, migration, or loading fails:

- the candidate generation is abandoned;
- the previous active generation remains intact;
- existing Play Mode instances resume when possible;
- diagnostics report the assembly, file, source location, and failure text.

This means the code visible in the editor may temporarily be newer than the code running in Play Mode. Check the
managed build diagnostics before assuming a save was loaded.

## First-Script Checklist

- The script is under `Assets`, or under a legacy custom assembly source root.
- The class derives from `Behaviour`.
- The class is public, non-abstract, and has the same name as the file.
- The namespace matches the project convention.
- `StableComponentId` is present and unique.
- Serialized fields use supported types and durable `StableFieldId` values.
- Input action names exist in the assigned Input Action asset.
- The managed build completed successfully before the component was attached.

Next, read [Behaviours And Lifecycle](BehavioursAndLifecycle.md).
