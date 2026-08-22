# Assets And ScriptableObjects

All scripting-visible native assets derive from `Asset`. Declare the asset class directly; there is no
`AssetReference<T>` authoring wrapper in 0.4.0.

```csharp
[SerializeField] private AudioClip? _sound;
[SerializeField] private AudioMixer? _mixer;
[SerializeField] private Material? _surface;
[SerializeField] private AnimationClip? _reload;
[SerializeField] private VfxEffect? _impact;
[SerializeField] private Prefab? _projectile;
[SerializeField] private SceneAsset? _destination;
```

Unassigned or unresolved assets are `null`. Persistent assets have stable `AssetId` identity, and repeated resolution
of the same type and ID returns the canonical managed wrapper for the runtime generation. `AssetId` remains available
for diagnostics and native interoperability, but ordinary gameplay and Inspector workflows use the asset object.

## Prefabs

`Prefab` is a direct asset object and owns instantiation convenience methods:

```csharp
Entity first = Instantiate(_projectile!);
Entity second = Instantiate(_projectile!, position, rotation);
Entity child = _projectile!.Instantiate(position, rotation, parent, active: false);
```

Instantiation uses the real runtime scene and asset services in Editor Play Mode and packaged players. The complete
clone graph is bound and hydrated before callbacks; active objects complete `Awake` and `OnEnable` before the call
returns. Internal scene references remap to the clone, asset references remain shared, and any binding failure rolls
the graph back.

## Explicit Runtime Residency

Presentation components accept direct assets and manage ordinary playback/render residency. Use
`Assets.LoadRuntime(asset)` only when code needs to observe readiness, fallback, revision, or diagnostic state, or to
retain an explicit residency lease:

```csharp
private AssetLoadOperation<Material>? _load;

protected override void OnEnable()
{
    if (_surface is not null)
        _load = Assets.LoadRuntime(_surface, AssetPriority.High);
}

protected override void OnDisable()
{
    _load?.Dispose();
    _load = null;
}
```

`AssetLoadOperation<T>` can be yielded or awaited. It is a generation-scoped lease, not a native resource pointer.
Disposal is idempotent, and retiring the managed generation releases outstanding operations.

## ScriptableObject

Use `ScriptableObject` for managed project data:

```csharp
[StableAssetTypeId("e4638cc5-58e2-4955-a576-c9c4edb995ca")]
public sealed class WeaponTuning : ScriptableObject
{
    public float Damage = 25.0f;
    public AudioClip? FireSound;
    public Prefab? Projectile;
}

public sealed class Weapon : Behaviour
{
    [SerializeField]
    private WeaponTuning? _tuning;
}
```

Persistent ScriptableObject assets receive stable identity and canonical instances. They may reference native assets,
prefabs, or other ScriptableObjects, but not scene entities/components. A scene behaviour may reference persistent
managed assets and objects in its own scene.

`ScriptableObject.CreateInstance<T>()` creates a transient, non-persistent object. Transient instances can be cloned
and destroyed at runtime but do not become project assets or acquire stable asset identity.

## Serialization And Dependencies

Direct asset and ScriptableObject references are tagged in state v2 with stable identity and declared/actual type.
They work inside supported `[Serializable]` value objects, one-dimensional arrays, and `List<T>`. Cooker dependency
extraction reads the same tagged records, so scene-to-managed-data-to-native-asset closure is deterministic.

The legacy reader accepts persisted 0.3.x `AssetReference<T>` records. Saving the owning scene, prefab, or data asset
normalizes them to v2; old C# source must still be migrated to direct asset fields.
