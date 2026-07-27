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

Play Mode reload is transactional. The runtime captures state, invokes `OnBeforeReload`, constructs and hydrates every
candidate instance, and swaps the Coral context only after the complete migration succeeds. A failed candidate is
unloaded and the last-good generation resumes. Migrated instances do not rerun `Awake` or `Start`.

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
