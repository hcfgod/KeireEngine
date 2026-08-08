# Entities, Components, And Transforms

Managed scripts access scene state through stable value handles. An `Entity` identifies an object within one runtime
world; component and subsystem handles pair that identity with validated operations.

## Entity Validity

`Entity` contains a world identity and an `EntityId`:

```csharp
if (!target.IsValid)
    return;
```

Choose the validity check that matches the question:

- `entity.Id.IsValid` checks whether an identity was assigned.
- `entity.IsValid` also asks the active runtime whether that entity still exists.

Use `Id.IsValid` for cheap serialized-reference presence checks when the lifetime is already controlled. Use
`IsValid` before operating on a reference that may have been destroyed or may belong to an expired runtime scene.
`default(Entity)` is the invalid sentinel.

Handles do not own entities. Copying one does not keep the target alive.

## Names And Active State

```csharp
Entity target = _target;
if (!target.IsValid)
    return;

target.Name = "Activated Door";
target.Active = true;
Debug.Log($"{target.Name}: hierarchy active = {target.ActiveInHierarchy}");
```

`Active` is the object's local setting. `ActiveInHierarchy` is read-only and also reflects inactive parents.

`Layer` is a readable/writable unsigned index from 0 through 31. It is the same value shown in the Inspector entity
header and drives the owning entity's physics collision layer. Invalid values throw before native scene state changes:

```csharp
Entity.Layer = 8;
```

## Parent And Children

```csharp
Entity parent = Entity.Parent;
IReadOnlyList<Entity> children = Entity.Children;

child.SetParent(Entity, preserveWorldTransform: true);
```

Assigning `Parent` preserves the world transform. `SetParent` lets the caller choose. The children collection is a
runtime snapshot; do not assume it updates after later hierarchy changes.

Search by name or path:

```csharp
Entity muzzle = Entity.Find("Visuals/Weapon/Muzzle");
Entity nestedCamera = Entity.FindChild("Camera", recursive: true);
Entity sibling = Entity.Find("../Sibling");
```

`Find` supports `/`-separated segments plus `.` and `..`. A missing result is `default`, so check validity.
Name search is ordinal and case-sensitive.

For important gameplay relationships, prefer serialized `Entity` fields over repeated name lookup. Names are authoring
labels, not durable IDs.

## Transforms

`Entity.Transform` returns a `TransformHandle`:

```csharp
TransformHandle transform = Entity.Transform;
transform.LocalPosition = new Vector3(0.0f, 2.0f, 0.0f);
transform.LocalRotation = Quaternion.Euler(0.0f, 90.0f);
transform.LocalScale = new Vector3(2.0f, 2.0f, 2.0f);

Vector3 worldPosition = transform.Position;
Vector3 forward = transform.Forward;
```

Available properties:

| Property | Access | Space |
| --- | --- | --- |
| `LocalPosition` | Read/write | Parent-local |
| `LocalRotation` | Read/write | Parent-local |
| `LocalScale` | Read/write | Parent-local |
| `Position` | Read | World |
| `Forward`, `Right`, `Up` | Read | Derived from the handle's local rotation |

Use `Time.DeltaTime` or `Time.FixedDeltaTime` when applying rates:

```csharp
transform.LocalPosition += transform.Forward * (_speed * Time.DeltaTime);
```

## Component Queries

Built-in and managed component types declare `StableComponentId`, which supports generic queries:

```csharp
if (Entity.HasComponent<AudioSourceComponent>())
{
    ComponentHandle source = Entity.GetComponent<AudioSourceComponent>();
    source.Enabled = false;
}
```

Other forms:

```csharp
ComponentTypeId type = ComponentType.Of<ColliderComponent>();
ComponentHandle untyped = Entity.GetComponent(type);
ComponentHandle<ColliderComponent> typed = Entity.GetComponentHandle<ColliderComponent>();

if (Entity.TryGetComponent<ColliderComponent>(out ComponentHandle collider))
    collider.Enabled = true;
```

`GetComponentHandle<T>()` creates a typed identity view even when the component is absent; check its `IsValid` property
before using it.

## Adding And Removing Components

```csharp
if (!Entity.HasComponent<AudioSourceComponent>())
{
    ComponentHandle added = Entity.AddComponent<AudioSourceComponent>();
    if (!added.IsValid)
        Debug.Error("Could not add the Audio Source.");
}

Entity.RemoveComponent<AudioSourceComponent>();
```

`AddComponent` returns an invalid handle when the operation is rejected. `RemoveComponent` and `ComponentHandle.Remove`
return whether removal succeeded. Do not remove a component while another part of the current callback assumes it
exists.

`RequireComponent` declares a permanent type-level dependency. Adding the dependent component adds missing
dependencies first; removing a dependency is rejected while any attached component requires it. Duplicate,
self-referential, unresolved, and cyclic dependency graphs fail managed registration without replacing the last-good
registry.

## Accessing Managed Behaviours

Retrieve another managed component on an entity:

```csharp
if (target.TryGetBehaviour<DoorController>(out DoorController? door) && door is not null)
    door.Open();
```

The non-`Try` form returns `null` when the component is unavailable:

```csharp
DoorController? door = target.GetBehaviour<DoorController>();
```

The registry holds weak references. A returned object belongs to the current managed generation and runtime scene; do
not retain it across reload. Store an `Entity` relationship and resolve the `Behaviour` when needed.

## Cloning And Destruction

Clone an existing scene entity:

```csharp
Entity clone = template.Instantiate();
if (clone.IsValid)
{
    TransformHandle transform = clone.Transform;
    transform.LocalPosition = spawnPosition;
}
```

Request destruction:

```csharp
Entity.Destroy();
```

Destruction is deferred by the runtime. After requesting it, stop using the entity in gameplay logic even if a
subsequent validity check in the same callback has not observed teardown yet.

Prefab assets use the separate `Prefab.Instantiate` API described in [Gameplay Services](GameplayServices.md).

## Built-In Component Markers

Managed marker types support presence, enabled-state, add, and remove operations without exposing native component
layout. They include:

- transform, camera, mesh renderer, and animator;
- collider, rigid body, and character controller;
- audio source and listener;
- VFX emitter;
- directional, point, and spot lights;
- canvas, rectangle transform, UI text, UI image, UI button, and UI layout.

These markers are deliberately empty. System-specific state uses façades such as `Entity.Animator`,
`Entity.AudioSource`, `RuntimeUi`, and `Vfx`.

`Entity.CharacterController` is the typed movement façade. It queues collision-resolved displacement and reads the
last grounded, ground-normal, and velocity state without exposing native physics ownership.

## Safe Reference Pattern

```csharp
[SerializeField, StableFieldId("82fc175d-d1a3-421e-8b33-2d7a550345e5")]
private Entity _target;

private void ActivateTarget()
{
    Entity target = _target;
    if (!target.IsValid)
    {
        Debug.Warn("Target is missing or no longer exists.");
        return;
    }

    target.Active = true;
}
```

Copy the value locally when a method performs several operations. This makes it clear that all checks and mutations
refer to the same serialized handle.
