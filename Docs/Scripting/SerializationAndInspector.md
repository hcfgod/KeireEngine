# Serialization And The Inspector

Kéire 0.4.0 uses managed-state format v2 and Unity-style field eligibility. State is attached to a behaviour,
prefab, scene, or persistent managed-data asset; the Inspector edits the same stable-field representation used by
save/load, duplication, prefab instantiation, hot reload, undo/redo, and Play Mode Changes.

## Field Eligibility

The following fields serialize:

- public instance fields;
- private or protected instance fields marked `[SerializeField]`.

The following do not serialize:

- plain non-public fields;
- static, const, or readonly fields;
- properties;
- fields marked `[NonSerialized]`.

`[SerializedField]` is not an alias and is intentionally unsupported.

```csharp
public int Lives = 3;

[SerializeField]
private float _speed = 6.0f;

private float _runtimeAccumulator; // not serialized
public string DisplayName => $"Player ({Lives})"; // not serialized
```

Generated gameplay projects suppress only the nullable warnings required for Inspector-injected references and direct
assignment from nullable lookups. The engine assembly remains nullable-strict; use `TryGetComponent` when absence is
part of normal control flow.

## Supported Values

- primitives, strings, enums, and Kéire math/value types;
- `[Serializable]` classes and structs with supported fields;
- `Entity`, concrete `Component`, `Behaviour`, `Asset`, `Prefab`, `SceneAsset`, and persistent `ScriptableObject`
  references;
- one-dimensional arrays and `List<T>` of supported element values;
- `KeireEvent` values.

This milestone does not support `[SerializeReference]`, dictionaries, multidimensional arrays, jagged arrays, or a
collection nested directly inside another collection.

```csharp
[Serializable]
public sealed class Wave
{
    public Prefab? Enemy;
    public List<Entity> SpawnPoints = [];
}

[SerializeField]
private Wave[] _waves = [];
```

## Direct References

Declare the actual type you want to use:

```csharp
public Entity? Target;
public Prefab? Projectile;
public AudioSource? AudioSource;

[SerializeField]
private Gameplay? _gameplay;

[SerializeField]
private AudioClip? _impactSound;
```

Entity fields accept Hierarchy drags and an entity picker. Component or behaviour fields accept a component-header
drag or an entity drag; a unique compatible component is selected automatically, while ambiguous matches open a
filtered chooser. Asset, prefab, scene, and ScriptableObject fields accept filtered Project/Asset Browser drags.

`None` clears a reference. A missing object retains its serialized identity and is displayed as missing rather than
silently erased. Reference drawers work on behaviour fields and supported nested objects. Persistent ScriptableObject
collection drawers support add/remove/reorder; behaviour arrays and lists are serialized at runtime but do not yet have
collection authoring controls in the scene Inspector. Assignments exposed by the Inspector participate in undo/redo
and Play Mode Changes.

Scene behaviours may reference scene objects in their own scene and any project asset. Persistent ScriptableObject
assets may reference assets, prefabs, or other ScriptableObjects, but may not capture scene entities or components.

## Stable IDs

`StableComponentId` identifies a behaviour type. `StableFieldId` identifies authored field meaning:

```csharp
[StableComponentId("668c2cee-3c8b-443f-a183-2f3f06141d77")]
public sealed class Door : Behaviour
{
    [SerializeField]
    [StableFieldId("3cc5bb2c-8c77-47a8-bf2d-768468e6fb07")]
    private AudioClip? _openSound;
}
```

Preserve the field ID when renaming a field without changing its meaning. Give a replacement meaning a new ID. The
v1 reader recognizes historical field aliases and old entity/asset-reference records; the next save writes canonical
v2. ScriptableObject fields without this attribute receive a deterministic ID derived from the asset type and field
path for Unity-style initial authoring. Add an explicit ID before renaming a field in persistent production data.

## Reference Records

State v2 explicitly tags reference kind:

- entity: stable entity identity, rebound to the destination runtime world;
- component/behaviour: entity identity plus stable concrete component type;
- native asset, prefab, or scene asset: stable asset identity plus declared/actual managed type;
- persistent ScriptableObject: stable managed asset identity and type.

This makes cyclic and cross-behaviour relationships available before `Awake` and lets clone/prefab transactions remap
only references internal to the cloned graph.

## Inspector Attributes

Use `[Range]`, `[Min]`, `[Max]`, `[InspectorStep]`, `[Multiline]`, `[InspectorName]`, `[Header]`, `[Tooltip]`,
`[Group]`, `[ReadOnly]`, and `[HideInInspector]` to control presentation. These attributes do not replace stable field
identity and do not change runtime ownership.

`[HotReloadState]` is separate from authored serialization. It migrates transient state across a successful reload and
is discarded with the Play session.
