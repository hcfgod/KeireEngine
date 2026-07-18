# Scene System

Scenes are typed `.keirescene` assets. `SceneAsset` owns an immutable validated definition; `Scene` is the owner-thread
mutable authoring/runtime instance; `SceneSystem` coordinates asynchronous loading and frame-boundary activation.

## Source Format

Schema version 1 stores a scene name and an ordered object list. Every object has a stable UUID, optional parent UUID,
name, active flag, and finite position/quaternion/scale transform. Parents must precede children, quaternions must be
normalized, and validation enforces bounded document size, object count, name size, and hierarchy depth. Encoding is
canonical and deterministic for import caching and source control.

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

`Unload` and `SetActive` are deferred to the same boundary. Events are `SceneLoadedEvent`, `SceneUnloadedEvent`,
`SceneLoadFailedEvent`, and `ActiveSceneChangedEvent`. The service closes before Assets and invalidates mutable scenes
without exposing JSON or backend types.

## Mutable Scene Contract

`Scene` supports stable-ID lookup, create, subtree duplicate, subtree delete, rename, active state, transform, and safe
reparenting. Reparent rejects cycles and restores parent-before-child order. Mutations validate a candidate definition
before commit, preserve state on failure, and mark the scene dirty. `SceneObjectHandle` contains a weak reference; it
becomes inert after object deletion or scene close and cannot extend the scene lifetime.

All mutations are owner-thread-affine. The editor records snapshots for its bounded undo/redo stack, while the runtime
observes immutable state at frame boundaries.

