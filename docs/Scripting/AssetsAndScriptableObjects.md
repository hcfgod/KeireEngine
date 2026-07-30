# Assets And ScriptableObjects

Kéire uses stable asset IDs at the managed boundary. `AssetReference<T>` adds a compile-time expected type without
exposing native asset ownership.

## Asset References

Declare engine asset fields with their marker type:

```csharp
[SerializeField, StableFieldId("0e23b81c-1a6d-44c5-bf14-5bdfca5a3618")]
private AssetReference<AudioClip> _sound;

[SerializeField, StableFieldId("d77f598b-a745-4c0f-b076-9f2164b7cad2")]
private AssetReference<AudioMixer> _mixer;

[SerializeField, StableFieldId("9d488a47-e3ea-4b65-a027-ddd88506194e")]
private AssetReference<AnimatorController> _controller;

[SerializeField, StableFieldId("5db8bbca-0d17-45af-85ae-5d42e35d5646")]
private AssetReference<VfxEffect> _impactEffect;
```

`AssetReference<T>` is a `readonly record struct`, so its default value is an invalid reference. Test it with
`IsValid`:

```csharp
if (_sound.IsValid)
    Audio.Play(Entity, _sound);
```

Use `.Id` when an API takes an untyped `AssetId`:

```csharp
if (_prefab.IsValid)
    Prefab.Instantiate(_prefab.Id, spawnPosition, spawnRotation);
```

## Asset Markers Are Not Loaded Objects

Types such as `AudioClip`, `AudioMixer`, `AnimationClip`, `AnimatorController`, and `VfxEffect` are asset type markers.
They provide typed selection and API overloads; they are not clip or graph objects that scripts modify.

This declaration is incorrect for an asset field:

```csharp
[SerializeField] private AudioClip? _clip;
```

Use:

```csharp
[SerializeField] private AssetReference<AudioClip> _clip;
```

The common compiler error
`cannot convert from 'Keire.AudioClip' to 'Keire.AssetId'` means an asset marker object was passed where stable asset
identity was required.

## Managed Data Assets

Derive authorable data from `ScriptableObject`:

```csharp
using Keire;

namespace MyGame;

[StableAssetTypeId("abfca69a-e5c8-4462-8238-92747733f15e")]
[CreateAssetMenu("Gameplay/Weapon Tuning", "WeaponTuning")]
public sealed class WeaponTuning : ScriptableObject
{
    [StableFieldId("c64ec4af-5a89-45d4-9878-b522470f455c")]
    [Range(0.0, 500.0)]
    public float Damage = 25.0f;

    [StableFieldId("05307831-9dd3-4bc2-a55a-d3b64350a06c")]
    [Range(1.0, 2000.0)]
    public float RoundsPerMinute = 600.0f;

    [StableFieldId("af0a8803-7abe-4694-8b4a-b5384a734265")]
    public AssetReference<AudioClip> FireSound;

    protected override void OnValidate()
    {
        Damage = MathF.Max(0.0f, Damage);
        RoundsPerMinute = MathF.Max(1.0f, RoundsPerMinute);
    }
}
```

Concrete authorable types require:

- a public parameterless constructor;
- a unique `[StableAssetTypeId]`;
- a `[StableFieldId]` on every serialized member;
- `[CreateAssetMenu]` if designers should create the type from the Project panel.

After a successful runtime assembly build, the type appears under **Create > Managed Data** at its declared menu path.
The saved source is a `.keiredata` asset.

## Managed Data Members

Managed data discovery includes:

- public instance fields;
- non-public fields marked `[SerializeField]`;
- public read/write properties;
- properties marked `[SerializeField]` that have both a getter and setter.

Static, `readonly`, indexed, compiler-generated, `[NonSerialized]`, and accessor-incomplete members are excluded.

Supported shapes include:

- primitives, strings, and enums;
- `Vector2`, `Vector3`, `Vector4`, `Quaternion`, and `Color`;
- nested `[SerializableType]` classes or structs;
- single-dimensional, zero-based arrays;
- `List<T>`;
- `AssetReference<T>`.

Unsupported shapes include dictionaries, cyclic inline graphs, abstract or interface inline values, multidimensional
arrays, and inline `ScriptableObject` objects. Use `AssetReference<T>` for relationships between managed data assets.

## Nested Serializable Values

```csharp
[SerializableType]
public sealed class DamageFalloff
{
    [StableFieldId("2919fa5b-c36d-4651-b3a2-3421e73a0043")]
    public float Distance;

    [StableFieldId("8c126135-70eb-4294-81bd-f3342ef52d1f")]
    [Range(0.0, 1.0)]
    public float Multiplier = 1.0f;
}

[StableAssetTypeId("5bab349c-18d8-4f65-afcc-aef3e0b91a2d")]
[CreateAssetMenu("Gameplay/Damage Profile")]
public sealed class DamageProfile : ScriptableObject
{
    [StableFieldId("b9c75331-a5ed-4f90-9dc3-95cf05bbde06")]
    public List<DamageFalloff> Falloff = [];
}
```

Stable field IDs must be unique across the complete discovered asset type, including nested members.

## Loading Managed Data

Reference a managed data asset from a `Behaviour`:

```csharp
[SerializeField, StableFieldId("d87d038f-c561-413e-85fc-dcfcba02ce59")]
private AssetReference<WeaponTuning> _tuning;
```

Synchronous access is appropriate only when the object is already registered:

```csharp
if (Assets.TryLoad(_tuning, out WeaponTuning? tuning) && tuning is not null)
    Apply(tuning);
```

`Assets.Load` requires an already loaded managed asset and throws when it is unavailable or has the wrong type:

```csharp
WeaponTuning tuning = Assets.Load(_tuning);
```

Use asynchronous loading when the asset may need admission through the application-owned pipeline:

```csharp
private async Task ApplyTuningAsync(CancellationToken cancellation)
{
    try
    {
        WeaponTuning tuning = await Assets.LoadAsync(_tuning, cancellation);
        if (!cancellation.IsCancellationRequested)
            Apply(tuning);
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
    }
}
```

Within a `Behaviour`, normally pass `LifetimeToken`.

For managed data, `AssetReference<T>.Value` is a convenience over `Assets.TryLoad`. It may return `null`; it does not
synchronously force an unloaded asset through the pipeline. Built-in marker references such as
`AssetReference<AudioClip>` should be passed to their system façade rather than dereferenced with `.Value`.

## Transient Instances And Clones

Create runtime-only data:

```csharp
WeaponTuning tuning = ScriptableObject.CreateInstance<WeaponTuning>();
tuning.Name = "Runtime tuning";
```

`CreateInstance<T>` constructs the object, invokes `OnEnable`, and then `OnValidate`. If activation fails, Kéire rolls
back the lifecycle before rethrowing.

Deep-clone a supported serialized graph:

```csharp
WeaponTuning runtimeCopy = ScriptableObject.Instantiate(authoredTuning);
```

The clone receives a new `RuntimeInstanceId` and runs `OnEnable`. Supported inline arrays, lists, and values are copied;
asset references remain references to the same stable assets.

## ScriptableObject Lifecycle

Override:

```csharp
protected override void OnEnable()
{
}

protected override void OnDisable()
{
}

protected override void OnValidate()
{
}
```

Lifecycle transitions are guarded against re-entry. `OnValidate` is the place to normalize data and reject invalid
combinations. Keep it deterministic and free of scene-specific side effects.

Loaded managed objects retain identity across successful asset reloads. A failed reload leaves the last-good object
active. Script reload hydrates managed data in the candidate context before migrating `Behaviour` instances, so a
failure can reject the entire candidate without mixing generations.

## Editing And Cooking

`.keiredata` edits are project-asset edits, not Play Mode scene changes. Save them through their asset document. Saving
publishes a new development revision that active gameplay can observe.

Strict cooking discovers runtime managed types, validates stable type and field IDs, validates supported member shapes,
and closes typed dependency references. Missing or incompatible managed types fail cooking rather than producing a
partially valid asset graph.

See [Asset Pipeline](../AssetPipeline.md) for import and cooking workflows.
