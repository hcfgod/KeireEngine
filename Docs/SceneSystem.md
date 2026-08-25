# Scene System

Scenes are typed `.keirescene` assets. `SceneAsset` owns an immutable validated definition; `Scene` is the owner-thread
mutable authoring/runtime instance; `SceneSystem` coordinates asynchronous loading and frame-boundary activation.

## Source Format

The current source format is schema version 6. It stores stable entities and versioned component records, prefab
instance mappings and typed overrides, a validated entity layer, bounded entity tags, scene lighting-bake settings,
and an optional baked `LightingSet` identity. Schema 1 inline transforms, schema 2 component records, schema 3 prefab
state, schema 4 entity layers, and schema 5 lighting state remain readable and migrate in memory; every save emits
canonical schema 6.

Every entity has a stable UUID, optional parent UUID, name, active flag, finite transform, layer, bounded tag set, and
bounded component list. Tags are unique, case-sensitive ASCII identifiers containing letters, digits, `_`, `-`, or
`.`; each tag begins with a letter, uses at most 64 bytes, and an entity carries at most 16. Parents must precede
children, quaternions must be normalized, prefab targets and overrides must resolve, and lighting settings must stay
inside their published limits. Validation also bounds document size, entity count, name size, and hierarchy depth.
Encoding is canonical and deterministic for import caching and source control.

## Runtime Loading

Enable Assets and set `ApplicationSpecification::Scenes.Mode` to `SceneMode::Enabled`. `Application::Scenes()` then
returns the application-owned service.

```cpp
auto load = application.Scenes()->Load(sceneId, Keire::SceneLoadMode::Single);
```

`Load` immediately returns a reference-counted `SceneLoadOperation`. Worker asset loading never mutates live scene state.
At the next application safe boundary, a successful operation creates the scene, emits load/unload events, and changes
the active scene. Single mode replaces all loaded scenes transactionally; additive mode preserves them up to the bounded
capacity. A failed/cancelled load leaves the active and already-loaded scenes unchanged.

`SceneLoadOperation::State`, `Diagnostic`, `Result`, and `Cancel` are synchronized and may be called from any thread;
`Asset` and `Mode` are immutable. Cancellation wins while an operation is queued or loading. Once owner-thread scene
activation has claimed the completed asset, that transaction finishes normally and publishes `Ready`.
The loaded/active set and operation state commit before lifecycle notifications are dispatched. If an event listener
throws, `AdvanceFrame` preserves and propagates that exception, but the already-consistent scene transaction is not
rolled back or exposed as cancellable. Loads and unloads requested by a lifecycle listener join the next safe-boundary
batch; they do not invalidate the batch being traversed, and unprocessed requests remain queued if a listener throws.

`Unload` and `SetActive` are deferred to the same boundary. Events are `SceneLoadedEvent`, `SceneUnloadedEvent`,
`SceneLoadFailedEvent`, and `ActiveSceneChangedEvent`. The service closes before Assets and invalidates mutable scenes
without exposing JSON or backend types.

Except for the thread-safe `IsOpen` query and the load-operation methods above, every `SceneSystem` operation is affine
to the thread that constructed the service. This includes `Load`, `Unload`, `SetActive`, `Active`, `Find`,
`LoadedScenes`, `Components`, and `Close`; an off-thread call throws before changing service state.

## Playable Runtime Worlds

`SceneRuntimeWorld` is the C++20 owner used by the packaged player and Editor Play Mode. It turns loaded scene assets
into independent `SceneRuntimeSession` instances and assigns a `SceneHandle` whose value is never reused during that
world's lifetime. A handle is an opaque identity, not an asset ID: a retired handle cannot accidentally address a
later load of the same asset.

Single and additive activation share one safe-boundary pipeline. Additive activation leaves the active handle
unchanged; `SetActive` changes it at the next boundary. `Unload` retires exactly the addressed handle. Single activation
starts and validates the candidate runtime before publishing it, so asset, Play-lifecycle, subsystem, or player
validation failure leaves all committed runtime sessions running. Unloading the only regular active scene is rejected;
load its replacement with Single mode instead. Every loaded session receives fixed/update ticks and
owns its own physics, audio, VFX, UI, and managed-behaviour state; all sessions participate in rendering.

Queries require an explicit `SceneQueryScope`: `Active`, all `Loaded` scenes, a `Specific` stable handle, or
`Persistent` carriers. This prevents a lookup from silently changing meaning when additive content is present.

`MakePersistent` marks an entity's hierarchy root. When its loaded scene is unloaded or replaced, non-persistent roots
are destroyed normally while the marked root and its original runtime session remain alive in an unloaded persistent
carrier. Entity/world IDs, component instances, managed behaviours, and lifecycle state are therefore preserved rather
than serialized and recreated. Unloading the last regular scene does not destroy persistent carriers; closing the
runtime world stops every remaining session deterministically. All mutation, processing, and scoped-query operations
are owner-thread-affine.

## Mutable Scene Contract

`Scene` supports stable-ID lookup, deterministic indexed name/tag queries, create, subtree duplicate, subtree delete,
rename, active state, tags, transform, and safe reparenting. Reparent rejects cycles and restores parent-before-child
order. Mutations validate a candidate definition before commit, preserve state on failure, update indexes as one
transaction, and mark the scene dirty. `SceneObjectHandle` contains a weak reference; it becomes inert after object
deletion or scene close and cannot extend the scene lifetime.

All mutations are owner-thread-affine. The editor records snapshots for its bounded undo/redo stack, while the runtime
observes immutable state at frame boundaries.

When a Collider or Character Controller is added to an entity that renders a built-in prototype mesh, editor
authoring initializes the new physics shape from that mesh. Cube, sphere, and capsule primitives use their exact local
dimensions; cylinder and cone colliders use the built-in convex mesh, while plane, quad, and torus colliders use the
static triangle mesh. The built-in capsule and its controller are both 0.25 m in radius and 1.0 m in total height.
Existing components and imported meshes retain their authored settings.
