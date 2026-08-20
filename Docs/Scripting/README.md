# C# Scripting

Kéire gameplay scripting targets .NET 10 and C# 14. Scripts use the `Keire` namespace, compile into project-defined
managed assemblies, and interact with scenes through validated value handles rather than native pointers.

This section is the starting point for writing, attaching, debugging, and shipping C# gameplay code. It documents the
public managed API in `KeireManaged` and links to the editor guides that cover asset authoring.

## Start Here

If this is your first Kéire script, follow these guides in order:

1. [Getting Started](GettingStarted.md) — create an assembly, write a `Behaviour`, attach it, and understand builds.
2. [Behaviours And Lifecycle](BehavioursAndLifecycle.md) — choose the correct callback and clean up safely.
3. [Serialization And The Inspector](SerializationAndInspector.md) — expose fields without breaking saved data.
4. [Entities, Components, And Transforms](EntitiesComponentsAndTransforms.md) — work with scene objects safely.
5. Continue with the system-specific guide you need.

Experienced C# developers can use the [Managed API Index](ApiIndex.md) as a compact map of the available types.
The [Managed API Capability Matrix](ManagedApiMatrix.md) records production support and the remaining parity roadmap.

## Minimal Behaviour

Create `Mover.cs` inside a source root declared by a `.keireasm` file:

```csharp
using Keire;

namespace MyGame;

[StableComponentId("b94ebaa8-4a30-46b8-938a-0761f4589a22")]
public sealed class Mover : Behaviour
{
    [SerializeField, StableFieldId("fa1cbb4f-a81e-4c0a-af58-315401454e04")]
    [Range(0.0, 20.0), Tooltip("Movement speed in metres per second.")]
    private float _speed = 5.0f;

    protected override void Update()
    {
        Vector2 input = Input.Axis2D("Move");
        Vector3 direction = new(input.X, 0.0f, input.Y);
        TransformHandle transform = Entity.Transform;
        transform.LocalPosition += direction * (_speed * Time.DeltaTime);
    }
}
```

The class name must match the filename. `StableComponentId` identifies the component in scenes and prefabs;
`StableFieldId` keeps serialized values associated with the field through ordinary renames.

After the editor publishes a successful script generation, attach the component through **Add Component > Scripts**,
drag the script onto the Inspector, or drop it onto a GameObject in the Hierarchy.

## Guide Map

| Guide | Use it for |
| --- | --- |
| [Getting Started](GettingStarted.md) | Project layout, `.keireasm`, IDE generation, compilation, and attachment |
| [Behaviours And Lifecycle](BehavioursAndLifecycle.md) | Callbacks, execution order, cleanup, reload, exceptions, and async lifetime |
| [Serialization And The Inspector](SerializationAndInspector.md) | Serialized fields, attributes, stable IDs, events, and migration |
| [Entities, Components, And Transforms](EntitiesComponentsAndTransforms.md) | Entity validity, hierarchy, transforms, components, cloning, and destruction |
| [Assets And ScriptableObjects](AssetsAndScriptableObjects.md) | `AssetReference<T>`, managed data assets, loading, cloning, and validation |
| [Gameplay Services](GameplayServices.md) | Time, input, physics, navigation, prefabs, VFX, cursor, logging, and profiling |
| [Audio](Audio.md) | Clip references, Audio Sources, one-call playback, mixers, buses, and status |
| [Animation](Animation.md) | Animator states, parameters, layers, events, playback, and IK |
| [UI And Events](UiAndEvents.md) | Scene UI, buttons, `KeireEvent`, text, and cursor ownership |
| [Async, Reload, And Diagnostics](AsyncReloadAndDiagnostics.md) | Cancellation, hot reload, failure isolation, logging, and troubleshooting |
| [Managed API Index](ApiIndex.md) | Quick type, callback, component, and attribute lookup |
| [Managed API Capability Matrix](ManagedApiMatrix.md) | Production status and named parity gaps by engine area |

## Core Mental Model

The managed API becomes easier to reason about when four rules stay explicit:

- A `Behaviour` belongs to one entity in one runtime scene. Its `Entity` handle is non-owning and must be treated as
  invalid after the scene object is destroyed.
- Handles such as `Entity`, `ComponentHandle`, `AssetReference<T>`, `AnimatorHandle`, and `AudioSourceHandle` are small
  values. They contain identity, not native ownership.
- Inspector state and hot-reload-only state are different. `[SerializeField]` persists authoring state;
  `[HotReloadState]` migrates transient Play Mode state without writing it into scenes or prefabs.
- Runtime subscriptions, cursor requests, cancellation sources, and other external registrations must be released on
  disable and before reload, then reacquired after reload when appropriate.

## Authoring Guides

These C# guides focus on runtime code. Use the existing authoring documentation for the other half of the workflow:

- [Input System](../InputSystem.md) and [Input Actions Editor](../InputActionsEditor.md)
- [Animation And Rigging](../AnimationRigging.md)
- [VFX Authoring And Runtime](../Vfx.md)
- [Scene Authoring](../SceneAuthoring.md)
- [Asset Pipeline](../AssetPipeline.md)
- [Weapon Authoring](../WeaponAuthoring.md)
- [Profiling](../Profiling.md)

## Sources Of Truth

The managed API source is the final authority:

- [`Behaviour.cs`](../../KeireManaged/Behaviour.cs) defines lifecycle callbacks.
- [`Handles.cs`](../../KeireManaged/Handles.cs) defines entity, component, and asset handles.
- [`RuntimeApi.cs`](../../KeireManaged/RuntimeApi.cs) defines gameplay service façades.
- [`RuntimeFoundation.cs`](../../KeireManaged/RuntimeFoundation.cs) defines application, time, and screen services.
- [`PlayerPreferences.cs`](../../KeireManaged/PlayerPreferences.cs) defines per-application persistent preferences.
- [`RuntimeUi.cs`](../../KeireManaged/RuntimeUi.cs) and [`Events.cs`](../../KeireManaged/Events.cs) define UI and events.
- [`SerializationAttributes.cs`](../../KeireManaged/SerializationAttributes.cs) defines Inspector metadata.

When a guide and the source disagree, update the guide with the API change.
