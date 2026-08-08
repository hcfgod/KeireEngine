# Serialization And The Inspector

Kéire serializes authoring state by stable identity so scene, prefab, Play Mode, and script-reload workflows can survive
ordinary code changes. Treat serialized members as a data contract: choose supported types, assign stable IDs, and
migrate intentionally.

## Behaviour Fields

A `Behaviour` field participates in persistent scene or prefab state when it is:

- a public instance field; or
- a non-public instance field marked `[SerializeField]`.

Static, `readonly`, `[NonSerialized]`, and compiler-generated fields are not captured. Private fields without
`[SerializeField]` remain ordinary runtime state.

Prefer private serialized fields:

```csharp
[SerializeField, StableFieldId("8a802089-1236-4370-9c9c-133c251305f2")]
[InspectorGroup("Movement")]
[Range(0.0, 20.0)]
[Tooltip("Maximum horizontal speed in metres per second.")]
private float _maximumSpeed = 6.0f;
```

This keeps the public API small while preserving an explicit authoring surface.

## Persistent State Versus Reload State

The two state categories solve different problems:

| Declaration | Scene/prefab state | Successful reload migration |
| --- | --- | --- |
| Public field | Yes | Yes |
| `[SerializeField]` private field | Yes | Yes |
| `[HotReloadState]` private field | No | Yes |
| Plain private field | No | No |

Use `[HotReloadState]` for transient Play Mode values that should survive recompilation but must not become authored
defaults:

```csharp
[SerializeField, StableFieldId("8306ea72-47ed-48bf-95d3-0023575806c8")]
private float _maximumHealth = 100.0f;

[HotReloadState]
private float _currentHealth;
```

A field can be both persistent and reloadable through its persistent declaration; adding `[HotReloadState]` to a
serialized field is redundant.

## Stable Field Identity

`StableFieldId` is the durable identity of a serialized member:

```csharp
[SerializeField, StableFieldId("e22be096-7a90-4936-b474-b5f1a47b74af")]
private AssetReference<AudioClip> _interactionSound;
```

Keep the ID unchanged when renaming the field. Never copy an ID to a different field or change the field's meaning
without an explicit data migration.

For legacy fields without a stable ID, `FormerlySerializedAs` provides a compatibility fallback:

```csharp
[SerializeField]
[FormerlySerializedAs("movementSpeed")]
[FormerlySerializedAs("_walkSpeed")]
private float _speed = 5.0f;
```

Name-based restoration produces a migration warning. Add a stable ID so future renames no longer depend on names.

## Inspector Attributes

| Attribute | Purpose |
| --- | --- |
| `[SerializeField]` | Include a non-public field, or an eligible managed-data property, in serialized authoring state |
| `[HotReloadState]` | Migrate a non-persistent `Behaviour` field during Play Mode reload |
| `[StableFieldId("uuid")]` | Give a serialized member durable identity |
| `[FormerlySerializedAs("name")]` | Add a legacy name fallback |
| `[Header("text")]` | Add a heading before a managed-data member |
| `[InspectorGroup("name")]` | Group related members where the Inspector supports grouped presentation |
| `[Tooltip("text")]` | Describe a member in the Inspector |
| `[Range(min, max)]` | Constrain numeric Inspector editing to finite ordered bounds |
| `[ReadOnlyInInspector]` | Show a managed-data member without allowing Inspector edits |
| `[HideInInspector]` | Serialize a member without displaying it |
| `[SerializableType]` | Allow a nested inline type in managed data |
| `[StableComponentId("uuid")]` | Identify an attachable managed component |
| `[StableAssetTypeId("uuid")]` | Identify a concrete managed data or built-in asset marker type |
| `[CreateAssetMenu("path", "name")]` | Add a managed data type to **Create > Managed Data** |
| `[RequireComponent(typeof(T))]` | Enforce an automatically attached, non-removable-while-required dependency |
| `[ExecutionOrder(value)]` | Define relative managed callback order |

`Range` validates its bounds when the attribute is created. It does not replace runtime validation for values loaded
from another source or computed by code.

## Common Supported Values

Behaviour state supports the engine value types used throughout the managed API:

- Boolean, integral, floating-point, string, and enum values;
- `Vector2`, `Vector3`, `Vector4`, `Quaternion`, and `Color`;
- `Entity`, ID values, and asset references;
- `UiButton` scene bindings;
- serializable arrays, lists, and event data whose elements are supported.

Managed data assets apply stricter discovery rules described in
[Assets And ScriptableObjects](AssetsAndScriptableObjects.md). In particular, nested inline managed-data types require
`[SerializableType]`, dictionaries are rejected, and inline `ScriptableObject` graphs must use `AssetReference<T>`.

If the editor reports a serialization diagnostic, change the field shape rather than relying on implementation-specific
`System.Text.Json` behavior.

The current `Behaviour` Inspector directly edits scalar/string/enum values, engine math values, `Entity`, `UiButton`,
`KeireEvent`, and `AssetReference<T>` fields, plus supported nested `[SerializableType]` values. Arrays and lists may
participate in state migration without receiving the same direct component-Inspector editing surface. `Header` and
`ReadOnlyInInspector` currently apply to managed data; use `InspectorGroup`, `Tooltip`, `Range`, and
`HideInInspector` for `Behaviour` presentation.

## Entity And Asset References

Scene references use `Entity`:

```csharp
[SerializeField, StableFieldId("04e66bd4-3747-4340-af60-a1d77a3111a1")]
private Entity _target;
```

Asset references use `AssetReference<T>`, not the asset marker type itself:

```csharp
[SerializeField, StableFieldId("76d371a5-ffcb-4793-9fe0-90a95f1fd8e7")]
private AssetReference<AudioClip> _clip;
```

`AssetReference<T>` is a value type. Do not declare it nullable or compare it with `null`; use `_clip.IsValid`.

`UiButton?` is different: it is a managed wrapper class for a scene-authored UI Button and can be `null`:

```csharp
[SerializeField, StableFieldId("31375b29-c8a8-4943-a7df-07f3d515caa2")]
private UiButton? _confirmButton;
```

## Inspector Events

`KeireEvent` fields expose persistent listeners:

```csharp
[SerializeField, StableFieldId("76ff62e8-d557-4e4c-8c65-0933763e4162")]
private KeireEvent _opened = new();

[SerializeField, StableFieldId("85fc4fee-a36a-48dc-91f1-b5c1529026dd")]
private KeireEvent<float> _healthChanged = new();
```

Generic events support up to four arguments. Persistent callback methods must return `void` and accept runtime-compatible
arguments in the same order. Code listeners added with `AddListener` are runtime-only and must be removed during
disable/reload when the subscription should not survive.

See [UI And Events](UiAndEvents.md) for a complete binding pattern.

## Inspector Edits During Play Mode

Inspector edits to serialized `Behaviour` fields hydrate the active Play Mode instance immediately. They remain
isolated to the runtime scene clone unless selected and applied through **Play Mode Changes**.

Managed data assets are different: `.keiredata` documents are project assets. Saving one publishes a new development
revision even during Play Mode; it is not a scene-clone edit.

## Renaming And Changing Types

Safe changes:

- rename a field while preserving its `StableFieldId`;
- add a new field with a new ID and a useful default;
- hide or regroup a field without changing its identity;
- add a legacy alias while transitioning an old field.

Changes that require migration planning:

- changing a field to an incompatible type;
- reusing an ID for another meaning;
- deleting a field whose value must be preserved elsewhere;
- converting an inline object to an asset reference;
- moving a type to a different assembly while changing its stable component or asset ID.

Candidate reload hydration is transactional. An incompatible migration rejects the candidate generation rather than
partially replacing live instances.

## Recommended Declaration Style

```csharp
[StableComponentId("dce1db02-69f0-4101-9a9f-437464427f56")]
public sealed class Pickup : Behaviour
{
    [SerializeField, StableFieldId("81f7adfd-669c-4946-a143-52e0d4d06834")]
    [Tooltip("Amount awarded when the pickup is collected.")]
    private int _amount = 1;

    [SerializeField, StableFieldId("c47de4cb-1072-4036-ac6a-e85e51b36db1")]
    private AssetReference<AudioClip> _collectedSound;

    [SerializeField, StableFieldId("95055e77-ce88-4fa8-a636-577e29af2ed1")]
    private KeireEvent _collected = new();

    [HotReloadState]
    private bool _consumed;
}
```

This style makes persistence, editor presentation, dependencies, and migration intent visible at the declaration site.
