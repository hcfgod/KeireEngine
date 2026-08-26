# Serialization And The Inspector

Kéire writes managed-state format v3 and retains readers for v1 and v2 state. State is attached to a behaviour,
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
- recursively nested one-dimensional arrays, exact `List<T>`, and exact `Dictionary<TKey, TValue>` values;
- `KeireEvent` values.

Dictionary keys must be strings, booleans, characters, integers, enums, or `Guid` values. Custom comparers,
multidimensional arrays, and custom collection implementations are rejected. Unsupported fields produce a
`ManagedSerializationException` whose structured `Code`, `Phase`, `Owner`, `RootField`, `FieldPath`, `DeclaredType`,
`RuntimeType`, `SerializedTypeId`, and `ObjectId` properties identify the exact failed contract. Loading and copying
prepare the complete candidate before changing the live object; a failure therefore preserves the previous valid
state.

```csharp
[Serializable]
public sealed class Wave
{
    public Prefab? Enemy;
    public List<Entity> SpawnPoints = [];
}

[SerializeField]
private Wave[] _waves = [];

[SerializeField]
private Dictionary<string, List<int[]>> _scoresByRegion = [];
```

## Reference Graphs

By-value fields retain value semantics. Add `[SerializeReference]` to a field when its subtree requires runtime
polymorphism, an abstract or interface declaration, shared object identity, or cycles. Every concrete class reachable
through such a field must be `[Serializable]`, closed and non-abstract, have a parameterless constructor (public or
non-public), and declare a unique `[StableSerializedTypeId]`:

```csharp
public interface IEncounterStep;

[Serializable]
[StableSerializedTypeId("6b76b5f7-22a1-43e3-b916-18f7e83424c4")]
public sealed class SpawnStep : IEncounterStep
{
    public string Enemy = string.Empty;
    public IEncounterStep? Next;
}

public sealed class Encounter : Behaviour
{
    [SerializeReference]
    private IEncounterStep? _first;
}
```

The v3 document contains one object table and a stable root map for all `[SerializeReference]` fields. It therefore
preserves aliases and cycles both within one root and across distinct root fields; no arbitrary assembly-qualified type
names are loaded from data. The accepted managed generation freezes one serialized-type registry from its locked
assembly-load context after the engine, locked packages, and project assemblies have loaded. State persistence,
ScriptableObject hydration, metadata, and both Inspectors share that immutable registry; assemblies loaded later cannot
change the generation's stable-ID mapping. A reload creates a new context and registry before it can replace the last
valid generation.

`[SerializeReference]` also opts a private field into serialization. The Behaviour and persistent ScriptableObject
Inspectors use the same graph editor: a reference slot can be cleared, linked to a compatible existing object, or
assigned a new instance of any registered concrete type. Shared links remain visible by object ID, and cycle links stop
recursive expansion while preserving the link. **Focus** opens a cycle target in an owner-local object pane without
changing serialized data or adding an undo record. Collection and dictionary edits validate the complete graph,
including duplicate dictionary keys, before one atomic undo record is committed; a rejected edit leaves the previous
graph unchanged.

Managed documents are limited to 16 MiB, and each string is limited to 1,048,576 bytes of valid UTF-8. The byte limit
is independent of C# UTF-16 code-unit count, so multibyte text and surrogate pairs consume their actual encoded size.
Values may contain at most 32 nested object/collection levels, and each serialized or registered concrete type may
publish at most 1,024 fields. An accepted managed generation may register at most 4,096 concrete graph types, and an
individual reference slot may offer at most 256 compatible type choices. A graph may contain at most 65,536 objects
and 131,072 nodes/edges; each array, list, or dictionary node may contain at most 16,384 entries. The managed and
native readers enforce the same limits before allocating or mutating a candidate state.

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
and scene Behaviour drawers support recursively nested one-dimensional arrays, exact `List<T>`, and exact
`Dictionary<TKey, TValue>` values with add/remove controls. Dictionary edits validate canonical keys before commit and
report the exact nested field path on duplicates. Assignments exposed by the Inspector participate in atomic undo/redo
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
v1/v2 readers recognize historical field aliases and old entity/asset-reference records; the next save writes
canonical v3. ScriptableObject fields without this attribute receive a deterministic ID derived from the asset type and field
path for Unity-style initial authoring. Add an explicit ID before renaming a field in persistent production data.

## Reference Records

Managed state explicitly tags engine-reference kind:

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
