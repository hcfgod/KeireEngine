# C# Scripting Fundamentals

Kéire's managed API is intentionally familiar to Unity users: gameplay types derive from `Behaviour`, entities own
components, fields appear in the Inspector, and callbacks cover frame, fixed-step, collision, animation, enable, and
teardown phases. The implementation uses validated value handles rather than exposing native pointers.

## Create A Managed Assembly

Use the Project panel to create a **Managed Assembly** and a **C# Script** below one of its source roots. A minimal
runtime assembly definition is:

```json
{
  "schemaVersion": 2,
  "name": "MyGame",
  "rootNamespace": "MyGame",
  "classification": "runtime",
  "sourceRoots": ["Assets/Scripts/Runtime"],
  "references": [],
  "packages": [],
  "defineSymbols": [],
  "allowUnsafe": false
}
```

Kéire targets .NET 10 and C# 14. The Editor watches `.cs` and `.keireasm`, validates the assembly graph, and publishes
only complete immutable generations. A failed candidate never replaces the last working generation.

## A Complete Behaviour

Save this as `Mover.cs` below the assembly's runtime source root. The stable IDs are persistence identities: generate
new UUIDs for your own types and fields, then keep them unchanged.

```csharp
using Keire;

namespace MyGame;

[StableComponentId("7b5ac27e-4531-4a42-97b8-9a643661660e")]
public sealed class Mover : Behaviour
{
    [SerializeField, StableFieldId("4cf59f74-236e-43c2-bb37-863a0ee5500c")]
    private float _speed = 4.0f;

    protected override void Update()
    {
        Vector2 input = Input.Axis2D("Move");
        Vector3 movement = new(input.X, 0.0f, input.Y);
        Entity.Transform.Position += movement * (_speed * Time.DeltaTime);
    }
}
```

After the build succeeds, use **Add Component > Scripts** in the Inspector or drag the script from Project to an
entity. Input returns neutral values when the named action is absent, but the intended workflow is to create `Move` in
the Input Actions editor and assign that action asset in Project Settings.

## Lifecycle Order

| Callback | Use it for |
| --- | --- |
| `Awake` | One-time local initialization after the entity is assigned. |
| `OnEnable` | Subscriptions, cursor requests, and work scoped to active execution. |
| `Start` | One-time initialization that depends on other awakened objects. |
| `FixedUpdate` | Physics-facing fixed simulation. |
| `Update` | Input and variable-frame gameplay. |
| `LateUpdate` | Camera follow and final-frame consumers. |
| `OnDisable` | Release active-only subscriptions, requests, and work. |
| `OnDestroy` | Final permanent teardown. |
| `OnBeforeReload` / `OnAfterReload` | Release and reacquire old managed-context relationships. |

Collision, trigger, animation-event, animator-IK, and procedural-motion callbacks are also available. `Awake` and
`Start` do not rerun after successful hot reload.

## Pair Acquisition And Cleanup

```csharp
private IDisposable? _cursorCapture;

protected override void OnEnable()
{
    _cursorCapture ??= Cursor.RequestCapture();
}

protected override void OnDisable() => ReleaseCapture();
protected override void OnBeforeReload() => ReleaseCapture();
protected override void OnAfterReload() => _cursorCapture ??= Cursor.RequestCapture();

private void ReleaseCapture()
{
    _cursorCapture?.Dispose();
    _cursorCapture = null;
}
```

Subscribe in `OnEnable` and unsubscribe in `OnDisable`; release old-context delegates in `OnBeforeReload`. Use
`LifetimeToken` for async work associated with a Behaviour. Continuations captured from lifecycle callbacks return to
the simulation thread, but referenced entities can still become invalid while awaiting.

## Inspector Fields

Private fields require `[SerializeField]`; public instance fields are serialized unless excluded. Supported metadata
includes stable field IDs, ranges, minimum values, step sizes, headers, labels, tooltips, multiline text, read-only
presentation, and former names. Entity, component, prefab, native asset, managed data-asset, enum, string, numeric,
vector, color, event, array, and list values are supported where the serializer documents them.

Do not change a stable field ID to rename a field. Use the migration attributes described in
[Serialization and the Inspector](../Scripting/SerializationAndInspector.md). A callback exception quarantines the
failing instance and reports it; it does not authorize the script to continue mutating through a stale handle.

Next: [Entities, Prefabs, Assets, and Scenes](WorldAndAssets.md).
