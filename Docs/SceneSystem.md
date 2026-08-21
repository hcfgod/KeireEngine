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

`Unload` and `SetActive` are deferred to the same boundary. Events are `SceneLoadedEvent`, `SceneUnloadedEvent`,
`SceneLoadFailedEvent`, and `ActiveSceneChangedEvent`. The service closes before Assets and invalidates mutable scenes
without exposing JSON or backend types.

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
