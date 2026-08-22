# Entities, Prefabs, Assets, And Scenes

The managed world API combines Unity-shaped objects with explicit validity. `Entity`, `Component`, and `Asset` are
managed objects backed by stable value identities. Always recheck `IsValid` when an object can outlive a scene change,
destroy, reload, or `await`.

## Entities And Components

```csharp
if (Entity.TryGetComponent<AudioSource>(out AudioSource? source))
    source.Volume = 0.5f;

Entity? child = Entity.Find("Rig/Hand.R");
if (child is { IsValid: true })
    child.Active = true;
```

Use `GetComponent<T>`, `TryGetComponent<T>`, hierarchy queries, or scene queries to find existing components. Use
`AddComponent<T>` and `RemoveComponent<T>` for supported runtime structural changes. Requirements declared with
`[RequireComponent]` are added before their dependent component; removing a required component is rejected while a
dependent remains.

`Entity.Transform` is always available for a live entity. Local and world position, rotation, scale, direction, and
parenting operations are exposed by `Transform`. Prefer a serialized entity/component reference over a repeated
name-based search when the relationship is authored in the scene.

## Prefabs

Create a prefab by dragging a Hierarchy entity onto a folder in the Project panel. Assign the resulting `Prefab` asset
to a serialized field, then instantiate it:

```csharp
using Keire;

namespace MyGame;

[StableComponentId("38e0b2bf-d750-47d4-8505-b5a032589222")]
public sealed class SpawnPoint : Behaviour
{
    [SerializeField, StableFieldId("166228ba-f8e3-4680-85af-df3a7e910a86")]
    private Prefab? _prefab = null;

    public Entity? Spawn()
    {
        if (_prefab is not { IsValid: true })
            return null;

        return _prefab.Instantiate(Entity.Transform.Position, Entity.Transform.Rotation);
    }
}
```

Prefab creation and save validate the entire subtree before replacing an existing asset. Instances keep stable links
for overrides and update handling. Runtime instantiation returns an entity in the current runtime world; destroy it
with `entity.Destroy()` or `EngineObject.Destroy(entity)`.

## Native Assets And Residency

Asset fields such as `AudioClip`, `Texture`, `Material`, `VfxEffect`, `Prefab`, and `SceneAsset` are direct managed
asset objects. A valid object identifies content; it does not promise that every byte is resident at this instant.

Use `Assets.LoadRuntime<T>` when gameplay needs an explicit native residency lease and observable priority, readiness,
fallback, revision, and diagnostics. Dispose the returned `AssetLoadOperation<T>` when the lease ends. Managed data
assets use `Assets.LoadAsync<T>` instead.

## ScriptableObjects

Derive authorable managed data from `ScriptableObject` and add `[CreateAssetMenu]`:

```csharp
using Keire;

namespace MyGame;

[CreateAssetMenu("Gameplay/Movement Tuning", "MovementTuning")]
public sealed class MovementTuning : ScriptableObject
{
    [StableFieldId("4f1b9c0e-1622-45e3-90f9-a0ba891bfeab")]
    public float WalkSpeed = 4.0f;

    [StableFieldId("d60cc874-58d4-4184-9d9d-c6f0bca69657")]
    public float SprintSpeed = 7.5f;
}
```

Create the asset through the Project panel menu supplied by `CreateAssetMenu`, then assign it to a serialized field.
`ScriptableObject.CreateInstance<T>()` creates transient data and `ScriptableObject.Instantiate(source)` clones an
existing object. Persistent project data should be authored and referenced as an asset; destroying persistent assets
at runtime is rejected.

## Scene Queries And Loading

`SceneManager.FindByName`, `FindWithTag`, and `FindAllWithComponent<T>` query the active scene by default. Overloads
accept `SceneQuery.Active`, `Loaded`, `Persistent`, or `SceneQuery.In(scene)`. Results are bounded; use tags and
component queries rather than assuming names are unique.

Packaged games can load a cooked `SceneAsset` asynchronously:

```csharp
private IEnumerator ReplaceScene(SceneAsset scene)
{
    SceneLoadOperation operation = SceneManager.LoadSceneAsync(scene, SceneLoadMode.Single);
    yield return operation;

    if (operation.State == SceneLoadState.Failed)
        Debug.LogError(operation.Error);
}
```

Single-scene replacement validates and prepares the candidate before retiring the previous active scene. Additive
loads coexist until explicitly unloaded. `SceneManager.Preserve(entity)`/`EngineObject.DontDestroyOnLoad(entity)` moves
eligible root content into the persistent world. Scene assets must be included in the player build's ordered scene set.

See [Assets and ScriptableObjects](../Scripting/AssetsAndScriptableObjects.md) and
[Scenes and Render Settings](../Scripting/ScenesAndRenderSettings.md) for loading, cancellation, fallback, and render
environment details.
