# Entities, Components, And Transforms

`Entity` is a sealed reference object for one scene object in one runtime world. Repeated lookup of the same stable
identity returns the same wrapper during a runtime generation. An unassigned reference is `null`; after destruction,
the existing wrapper remains non-null and `IsValid` becomes false.

## Component Lookup

Every `Entity`, `Component`, and `Behaviour` exposes the same lookup family:

```csharp
AudioSource? source = GetComponent<AudioSource>();

if (Target is not null && Target.TryGetComponent(out Animator? animator))
    animator.SetBool("Alert", true);

Collider[] colliders = GetComponentsInChildren<Collider>(includeInactive: true);
RigidBody? body = GetComponentInParent<RigidBody>();

var results = new List<IReloadable>();
GetComponentsInChildren(results);
```

Available forms include `GetComponent`, `TryGetComponent`, and `GetComponents`, plus singular, array-returning, and
allocation-free `List<T>` child/parent variants. Each also has a `Type` overload. Behaviour queries match assignable
base classes and interfaces; built-in components use their stable exact type. Results follow stable component and
hierarchy order.

Same-entity lookup ignores active state. Hierarchy lookup includes self and excludes inactive ancestors or descendants
unless `includeInactive` is true.

## Adding And Removing

```csharp
AudioSource source = Entity.AddComponent<AudioSource>();
Gameplay gameplay = Entity.AddComponent<Gameplay>();

Destroy(source);       // delayed until the current update traversal ends
Destroy(Entity);       // destroys the hierarchy at the same safe boundary
```

Adding a behaviour to an active entity performs native binding, `Awake`, and (when enabled) `OnEnable` before
`AddComponent` returns. An inactive entity defers `Awake` until its first activation. Native binding failure rolls the
addition back; a user callback exception is reported and disables the offending behaviour.

Native component cardinality is one component per stable built-in type. Multiple matches remain possible when looking
up behaviours through a base class or interface.

## Hierarchy And Activation

```csharp
Entity? parent = Entity.Parent;
IReadOnlyList<Entity> children = Entity.Children;

Entity.SetParent(newParent, preserveWorldTransform: true);
Entity.SetActive(false);
Entity.Tag = "Enemy";

Entity? muzzle = Entity.Find("Weapon/Muzzle");
```

Parenting rejects cycles and cross-world relationships. Child enumeration and hierarchy searches are stable. Scene
queries and tags are provided by `SceneManager`; `DontDestroyOnLoad(Entity)` moves a root hierarchy to persistent
scene ownership.

## Transform

`Entity.Transform` and `Component.Transform` return the concrete `Transform` attached to the entity:

```csharp
Transform.Position = spawnPosition;
Transform.Rotation = Quaternion.Euler(0.0f, headingDegrees);
Transform.LocalScale = new Vector3(2.0f, 2.0f, 2.0f);

Transform.Translate(Vector3.Forward * speed * Time.DeltaTime);
Transform.Rotate(Quaternion.Euler(0.0f, turn * Time.DeltaTime));
```

`Position` and `Rotation` are world-space. `LocalPosition`, `LocalRotation`, and `LocalScale` are parent-relative.
`Forward`, `Right`, and `Up` derive from world rotation. Presentation interpolation is read-only unless explicitly
reset for teleportation.

## Cloning And Prefabs

```csharp
Entity duplicate = Instantiate(Entity);
Entity projectile = Instantiate(Projectile, Transform.Position, Transform.Rotation);
Entity child = Projectile.Instantiate(position, rotation, Entity, active: true);
```

The entire cloned graph is registered and hydrated before callbacks. Internal entity/component references remap to
the clone, same-scene external references stay external, and asset references remain shared. Active prefab graphs run
their required `Awake` and `OnEnable` callbacks before instantiation returns. A binding failure rolls back the complete
graph.

Prefab creation rejects references from the selected hierarchy to unrelated scene entities. Persisted 0.3.x entity
and asset-reference records are read by the legacy state reader and normalize to tagged v2 records when next saved.

## Built-In Components

Kéire exposes concrete managed classes for every native registry entry: `Transform`, `Camera`, directional/point/spot
lights, reflection/light probes, `MeshRenderer`, `Animator`, `Collider`, `RigidBody`, `CharacterController`, fixed,
hinge, distance, and spring joints, `AudioSource`, `AudioListener`, `AudioReverbZone`, `VfxEmitter`, and
`Keire.UI.UIDocument`. Use the component instance for entity-scoped operations. Retired Canvas/Rect Transform component
IDs are reserved for exact import diagnostics and are not an authoring API.
