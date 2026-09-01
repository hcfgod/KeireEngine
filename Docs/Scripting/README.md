# C# Scripting

Kéire 0.4.0 gameplay scripting targets .NET 10 and C# 14. Scripts use canonical managed objects rather than public
native handles: `Entity` represents a scene object, concrete `Component` subclasses represent attached functionality,
and `Asset` subclasses represent project content. Unassigned references are `null`; destroyed scene wrappers remain
non-null and report `IsValid == false`.

## Minimal Behaviour

```csharp
using Keire;

namespace MyGame;

[StableComponentId("b94ebaa8-4a30-46b8-938a-0761f4589a22")]
public sealed class Mover : Behaviour
{
    [SerializeField, StableFieldId("fa1cbb4f-a81e-4c0a-af58-315401454e04")]
    [Range(0.0, 20.0)]
    private float _speed = 5.0f;

    [SerializeField, StableFieldId("122572ac-2a47-42e2-9bc6-c4309a517ca6")]
    private AudioSource? _audioSource;

    protected override void Awake()
    {
        _audioSource ??= GetComponent<AudioSource>();
    }

    protected override void Update()
    {
        Keyboard? keyboard = Input.Keyboard.Current;
        Vector2 input = new(keyboard?.dKey.IsPressed == true ? 1.0f : keyboard?.aKey.IsPressed == true ? -1.0f : 0.0f,
                            keyboard?.wKey.IsPressed == true ? 1.0f : keyboard?.sKey.IsPressed == true ? -1.0f : 0.0f);
        Transform.LocalPosition += new Vector3(input.X, 0.0f, input.Y) * (_speed * Time.DeltaTime);
    }
}
```

The class name must match the filename. `StableComponentId` preserves the script component's identity in scenes and
prefabs. Keep `StableFieldId` unchanged when a serialized field is renamed.

## Direct Inspector References

Declare normal fields. Public fields serialize automatically; private and protected fields serialize only with
`[SerializeField]`.

```csharp
public Entity? Target;
public Prefab? Projectile;
public AudioSource? Source;

[SerializeField]
private Gameplay? _gameplay;
```

The Inspector accepts Hierarchy drags for entities, entity or component-header drags for components/behaviours, and
Project/Asset Browser drags for assets, prefabs, scenes, and persistent `ScriptableObject` assets. The same reference
drawers work in supported `[Serializable]` objects. One-dimensional arrays and `List<T>` values participate in managed
state serialization; collection authoring is currently available for persistent ScriptableObject data assets.

## Core Rules

- Use `GetComponent<T>`, `TryGetComponent<T>`, and the child/parent lookup families on either `Entity` or `Component`.
- `AddComponent<T>()` returns the actual component or behaviour. `Destroy(component)` and `Destroy(entity)` commit
  after the current update traversal.
- Use instance operations such as `AudioSource.Play()`, `Animator.SetFloat(...)`, and `VfxEmitter.SendEvent(...)`.
  Global facilities such as `Physics`, `Audio`, `SceneManager`, and `RenderSettings` remain static.
- `Instantiate(Prefab, ...)` is available from a behaviour, and `prefab.Instantiate(...)` is available directly.
- Use `TryGetComponent` when absence is expected. Generated gameplay projects suppress only the nullable warnings
  required for Inspector injection and assignment from nullable lookup results.
- `[HotReloadState]` migrates Play Mode state during a successful script reload; it does not author scene or prefab
  state.

## Guide Map

| Guide | Use it for |
| --- | --- |
| [Getting Started](GettingStarted.md) | Assemblies, IDE generation, compilation, and attachment |
| [Behaviours And Lifecycle](BehavioursAndLifecycle.md) | Callback timing, reentrancy, cleanup, reload, and exceptions |
| [Serialization And The Inspector](SerializationAndInspector.md) | Field eligibility, direct references, stable IDs, and migration |
| [Managed Extensibility](ManagedExtensibility.md) | Custom values, runtime services, native contracts, Editor SDK, importers, tools, and build hooks |
| [Entities, Components, And Transforms](EntitiesComponentsAndTransforms.md) | Lookup, hierarchy, cloning, activation, and destruction |
| [Assets And ScriptableObjects](AssetsAndScriptableObjects.md) | Direct assets, prefabs, persistent data, and residency operations |
| [Gameplay Services](GameplayServices.md) | Input, physics, navigation, prefabs, VFX, logging, and profiling |
| [Audio](Audio.md) | Audio components, clips, mixers, buses, and one-shot playback |
| [Animation](Animation.md) | Animator states, parameters, layers, events, and IK |
| [Rendering And Materials](RenderingAndMaterials.md) | Cameras, renderers, lights, dynamic materials, and parameters |
| [Scenes And Render Settings](ScenesAndRenderSettings.md) | Scene objects, loading, activation, queries, and environments |
| [UI And Events](UiAndEvents.md) | UI Builder, retained documents/styles/panels, managed controls, events, and target modes |
| [Async, Reload, And Diagnostics](AsyncReloadAndDiagnostics.md) | `Job`, cancellation, hot reload, failures, and diagnostics |
| [Managed API Index](ApiIndex.md) | Compact type and method lookup |

## Sources Of Truth

- [`Handles.cs`](../../KeireManaged/Handles.cs) defines the engine-object, entity, component, transform, and lookup API.
- [`Behaviour.cs`](../../KeireManaged/Behaviour.cs) defines lifecycle callbacks.
- [`NativeAssets.cs`](../../KeireManaged/NativeAssets.cs) defines direct native asset objects.
- [`RuntimeApi.cs`](../../KeireManaged/RuntimeApi.cs) defines gameplay components and global services.
- [`Rendering.cs`](../../KeireManaged/Rendering.cs) defines cameras, renderers, lights, and dynamic materials.
- [`RuntimeWorld.cs`](../../KeireManaged/RuntimeWorld.cs) defines `Scene`, `SceneManager`, and render settings.
- [`SerializationAttributes.cs`](../../KeireManaged/SerializationAttributes.cs) defines Inspector metadata.
- [`ManagedCustomSerialization.cs`](../../KeireManaged/ManagedCustomSerialization.cs) defines bounded custom values,
  converters, migrations, and serialization callbacks.
- [`RuntimeServices.cs`](../../KeireManaged/RuntimeServices.cs) defines application-owned managed services.
- [`KeireEditorManaged`](../../KeireEditorManaged) defines the Editor-only retained extension SDK.
