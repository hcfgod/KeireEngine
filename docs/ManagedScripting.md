# Managed scripting

Kéire managed gameplay targets .NET 10 and C# 14 with nullable annotations, deterministic output, portable PDBs, and
engine-owned compiler settings.

## Assembly definitions

`.keireasm` schema version 2 adds exact-version NuGet packages, project define symbols, unsafe-code policy, and strict
runtime/editor/test reference isolation. Schema version 1 remains readable. Runtime assemblies may reference runtime
assemblies only. Editor assemblies may reference runtime and editor assemblies. Test assemblies may reference every
classification.

## Automatic builds

The editor watches declared `.cs` and `.keireasm` sources and starts a generation-aware build about 100 ms after the
newest stable change. Standalone `.cs` changes bypass asset catalog import, and each generation copies the validated
engine API assembly from a source-fingerprinted cache. Engine API sources compile once when their project, size, or
timestamp fingerprint changes; gameplay-only saves reuse the validated result. A newer change cancels an obsolete
build. Successful output is published to an immutable `Library/ScriptAssemblies/Generations/<generation>` directory and
recorded in `active-generation.json`; failed builds never replace the last-good generation. MSBuild intermediates
remain under `Library/ScriptAssemblies/Intermediate`.

## Serialization and reload

Kéire serializes supported public instance fields and private fields marked `[SerializeField]`. `[HotReloadState]`
includes a field in Play Mode migration without writing it into scene or prefab state. `[StableFieldId]` is the durable
identity. Field names and `[FormerlySerializedAs]` aliases are compatibility fallbacks and produce migration warnings.
Inspector edits to these fields during Play Mode hydrate the active `Behaviour` immediately while remaining isolated
to the runtime scene until they are selected in **Play Mode Changes** and applied.

Play Mode reload is transactional. The runtime captures state, invokes `OnBeforeReload`, constructs and hydrates every
candidate instance, and swaps the Coral context only after the complete migration succeeds. A failed candidate is
unloaded and the last-good generation resumes. Migrated instances do not rerun `Awake` or `Start`.

## Managed data assets

Authorable `ScriptableObject` types require a public parameterless constructor and `[StableAssetTypeId]`.
`[CreateAssetMenu]` adds a deterministic **Create > Managed Data** path after the runtime assembly builds. Every
serialized member requires `[StableFieldId]`; supported members are primitives, enums, vectors/colors, nested
`[SerializableType]` values, arrays/lists, and `AssetReference<T>`. Range, header, tooltip, read-only, and hidden
metadata is reflected into the default Inspector. Dictionaries, cyclic or polymorphic inline graphs, and inline
ScriptableObjects are rejected; use typed asset references for cross-object relationships.

`.keiredata` edits are project-asset edits, not Play scene-clone edits. They persist through the asset document’s own
Save action and publish a new development revision immediately during Play. Loaded objects retain identity across
successful asset reloads. `Assets.Load`, `TryLoad`, and cancellation-aware `LoadAsync` use the application-owned asset
pipeline, while `ScriptableObject.Instantiate` deep-copies the supported serialized graph.

Script reload hydrates all active managed data into the candidate context before migrating Behaviours. Any migration
failure abandons the candidate context and leaves the last-good objects and scripts active. Strict cooking similarly
compiles and discovers runtime types before validating type IDs, field shapes, and typed dependency closure.

## Exceptions and async work

A managed lifecycle exception quarantines only the Behaviour that threw. Runtime diagnostics include the instance,
entity, callback, type, script generation, and managed exception text. The instance can be retried or explicitly
enabled/disabled through `ScriptSystem`.

Each Behaviour owns a synchronization context and lifetime token. Async continuations resume on the simulation thread.
Disable, destroy, reload, and Play Mode teardown cancel queued lifetime-bound work.

## Entity API

`Entity` remains a blittable-style value handle and exposes name, active state, parent/children, transform, component
queries, add/remove, clone, and deferred destroy operations. Generic component lookup resolves stable component IDs once
and caches the result.

## Attaching scripts

Create C# scripts inside a `.keireasm` source root. The editor generates a stable component ID, compiles the script,
and registers the Behaviour as a component. Attach it from the Inspector's searchable **Add Component > Scripts**
menu, drag the `.cs` asset onto the Inspector drop target, or drop it directly onto a GameObject in the Hierarchy.
Like Unity, the public Behaviour type name must match the script filename. If the script is not present in the active
managed generation yet, the editor queues the attachment, compiles and reloads scripts, then attaches it automatically.
## Inspector events and cursor ownership

Declare `KeireEvent` fields with `SerializeField` to expose Unity-style persistent listeners in the Inspector.
Generic events support up to four engine or project-defined argument types, while `AddListener` and `RemoveListener`
provide runtime-only subscriptions.

```csharp
[SerializeField] private KeireEvent opened = new();
[SerializeField] private KeireEvent<DamageInfo> damaged = new();
```

Each persistent listener stores an enabled flag, target entity, managed component stable ID, and callback method.
Renaming a component type is safe when its `StableComponentId` remains unchanged. Callback methods must return `void`
and accept the event's argument count and runtime-compatible types.

Use scoped cursor requests when multiple systems can control pointer capture:

```csharp
IDisposable gameplayCapture = Cursor.RequestCapture();
IDisposable menuCursor = Cursor.RequestVisible();
menuCursor.Dispose(); // Gameplay capture resumes automatically.
```

Visible requests take priority over capture requests. Dispose requests during disable, destruction, or managed reload;
direct `Cursor.Show`, `Hide`, `Lock`, and `Unlock` calls remain available for simple single-owner workflows.
