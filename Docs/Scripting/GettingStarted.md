# Getting Started With C# Scripting

Kéire compiles project scripts into managed assemblies and publishes each successful build as an immutable generation.
A failed build never replaces the last working generation, so Play Mode can continue using the last-good scripts while
you correct diagnostics.

## Prerequisites

A scripting project needs:

- a project opened in Kéire Editor;
- a .NET 10 SDK available to the editor for project compilation;
- at least one `.keireasm` managed assembly definition;
- C# files located under one of that assembly's `sourceRoots`.

Packaged games carry the runtime needed to execute already-cooked assemblies. Developing or changing scripts still
requires the SDK.

## Recommended Layout

Keep runtime, editor, and test code in separate source roots:

```text
Assets/
  Scripts/
    Runtime/
      Gameplay.keireasm
      PlayerController.cs
      PauseMenu.cs
    Editor/
      Gameplay.Editor.keireasm
    Tests/
      Gameplay.Tests.keireasm
```

An assembly definition may sit elsewhere, but every source root is project-relative and cannot escape the project.
The editor can create a starter assembly and script from the Project panel.

## Assembly Definitions

Schema version 2 is the current format:

```json
{
  "schemaVersion": 2,
  "name": "MyGame",
  "rootNamespace": "MyGame",
  "classification": "runtime",
  "sourceRoots": [
    "Assets/Scripts/Runtime"
  ],
  "references": [],
  "packages": [],
  "defineSymbols": [
    "MY_GAME"
  ],
  "allowUnsafe": false
}
```

| Property | Meaning |
| --- | --- |
| `schemaVersion` | `2` for packages, define symbols, and unsafe-code policy; schema 1 remains readable |
| `name` | Unique C# assembly name |
| `rootNamespace` | Default namespace used by generated scripts and IDE projects |
| `classification` | `runtime`, `editor`, or `tests` |
| `sourceRoots` | Unique project-relative directories containing the assembly's C# sources |
| `references` | Asset IDs of other `.keireasm` definitions |
| `packages` | NuGet package name and exact, non-floating version pairs |
| `defineSymbols` | Unique valid C# preprocessor identifiers |
| `allowUnsafe` | Whether gameplay code in this assembly may compile unsafe blocks |

Reference rules are deliberate:

- runtime assemblies may reference runtime assemblies;
- editor assemblies may reference runtime and editor assemblies;
- test assemblies may reference all classifications.

Duplicate names, missing references, invalid classification edges, and cycles fail graph validation before compilation.
Package versions must be exact; ranges, wildcards, and floating versions are rejected.

## Create A Behaviour

Create `LightSwitch.cs` in a runtime source root:

```csharp
using Keire;

namespace MyGame;

[StableComponentId("31f48d51-3502-4452-abfe-6e9af83fd83a")]
public sealed class LightSwitch : Behaviour
{
    [SerializeField, StableFieldId("0bce4b90-da78-4c6d-969c-03807d71504c")]
    private Entity _light;

    protected override void Update()
    {
        if (Input.Pressed("ToggleLight") && _light.Id.IsValid)
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

## Build And Attach

The editor watches `.cs` and `.keireasm` files. After the newest change settles, it:

1. validates the assembly graph;
2. generates SDK-style projects targeting .NET 10 and C# 14;
3. compiles the engine API and affected gameplay assemblies;
4. validates the candidate type registry;
5. publishes a new immutable generation only when all steps succeed;
6. reloads active Play Mode instances transactionally.

Attach a successfully compiled script with any of these editor workflows:

- choose **Add Component > Scripts** in the Inspector;
- drag the `.cs` asset onto the Inspector drop target;
- drag it directly onto a GameObject in the Hierarchy.

If the script is not in the active generation yet, the editor queues the attachment, builds and reloads, then completes
the attachment after the type becomes available.

## IDE Projects

Opening a C# source from the editor regenerates a project-root solution and one SDK-style project per `.keireasm`.
These files provide IntelliSense and navigation. The `.keireasm` graph remains authoritative; editing only a generated
project does not change a runtime build.

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

- The script is below a declared `sourceRoots` path.
- The class derives from `Behaviour`.
- The class is public, non-abstract, and has the same name as the file.
- The namespace matches the project convention.
- `StableComponentId` is present and unique.
- Serialized fields use supported types and durable `StableFieldId` values.
- Input action names exist in the assigned Input Action asset.
- The managed build completed successfully before the component was attached.

Next, read [Behaviours And Lifecycle](BehavioursAndLifecycle.md).
