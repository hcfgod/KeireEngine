# Gameplay Services

The managed runtime exposes focused static façades for frame time, input, physics, navigation, prefabs, VFX, cursor
state, diagnostics, and profiling. These APIs validate identity and arguments before crossing the native boundary.

## Time

```csharp
float frameDelta = Time.DeltaTime;
float fixedDelta = Time.FixedDeltaTime;
float unscaledDelta = Time.UnscaledDeltaTime;
double elapsed = Time.Elapsed;
```

| Property | Use |
| --- | --- |
| `DeltaTime` | Scaled variable-frame movement and timers |
| `FixedDeltaTime` | Fixed simulation work in `FixedUpdate` |
| `UnscaledDeltaTime` | Frame work that should ignore simulation time scaling |
| `Elapsed` | Runtime elapsed time in seconds |

Multiply rates by the matching delta. Do not multiply one-time impulses or already time-integrated values.

## Input

Managed scripts poll named actions from the active Input Action asset:

```csharp
Vector2 move = Input.Axis2D("Move");
float throttle = Input.Axis("Throttle");

if (Input.Pressed("Jump"))
    Jump();
if (Input.Held("Fire"))
    ContinueFiring();
if (Input.Released("Aim"))
    StopAiming();
```

| Method | Meaning |
| --- | --- |
| `Axis2D(action)` | Current two-dimensional action value |
| `Axis(action)` | X component of `Axis2D` |
| `Held(action)` | Action is currently held |
| `Pressed(action)` | Action became pressed in the current snapshot |
| `Released(action)` | Action became released in the current snapshot |
| `Button(action)` | Alias for `Held` |

All fixed ticks and the variable update in one outer frame observe the same immutable action snapshot. Use edge methods
for one-shot commands and `Held` for continuous behavior.

Action names are strings and must match authoring exactly. Configure actions, maps, bindings, and UI-capture policy in
the [Input Actions Editor](../InputActionsEditor.md).

## Physics Raycasts

Use the calling entity as the query context:

```csharp
Vector3 origin = Entity.Transform.Position;
Vector3 direction = Entity.Transform.Forward;

if (Physics.TryRaycast(
        Entity,
        origin,
        direction,
        out RaycastHit hit,
        maximumDistance: 100.0f,
        mask: uint.MaxValue,
        ignoredEntity: Entity))
{
    Debug.DrawLine(origin, hit.Point, Color.RedColor, 0.25f);
    Debug.Log($"Hit {hit.Entity.Name} at {hit.Distance:0.00} m.");
}
```

The direction is normalized by the API. A zero direction or non-positive maximum distance throws an argument exception.
The returned hit contains the hit entity, point, normal, and distance.

`Physics.Raycast` currently returns either an empty collection or the same nearest hit as `TryRaycast`; use
`TryRaycast` when only one result is needed.

Layer masks use the project's 32-layer collision configuration.

## Collision And Trigger Callbacks

Override the matching callbacks:

```csharp
protected override void OnCollisionEnter(CollisionContact contact)
{
    Debug.Log($"Collided with {contact.Other.Name}; impulse {contact.Impulse:0.00}.");
}

protected override void OnTriggerEnter(CollisionContact contact)
{
    if (contact.Other.TryGetBehaviour<PlayerController>(out PlayerController? player) && player is not null)
        ActivateFor(player);
}
```

`CollisionContact` contains:

- `Other` — the other entity;
- `Point` and `Normal` — contact geometry;
- `Impulse` — reported contact impulse;
- `Trigger` — whether the contact came from a trigger.

Kéire dispatches enter, stay, and exit contacts after fixed simulation in deterministic engine order. Choose collision
or trigger callbacks rather than branching on `Trigger` unless a shared helper consumes both.

## Navigation

Path queries are asynchronous and cancellation-aware:

```csharp
[SerializeField, StableFieldId("b9b127fc-254d-4fee-8cd6-fc9243641370")]
private Vector3 _destination;

protected override void OnEnable()
{
    _ = FindPathAsync(LifetimeToken);
}

private async Task FindPathAsync(CancellationToken cancellation)
{
    try
    {
        NavigationPath path = await Navigation.FindPathAsync(
            Entity.Transform.Position,
            _destination,
            areaMask: uint.MaxValue,
            cancellation: cancellation);

        for (int index = 1; index < path.Points.Count; ++index)
        {
            Debug.DrawLine(
                path.Points[index - 1],
                path.Points[index],
                new Color(0.1f, 0.9f, 0.4f),
                2.0f);
        }
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
    }
}
```

`NavigationPath` exposes `Points` and the navigation-mesh revision used by the result. A newer mesh can make previously
computed paths stale at the native service boundary. Re-query after relevant navigation changes.

## Prefab Instantiation

`Prefab.Instantiate` accepts a prefab asset ID:

```csharp
[SerializeField, StableFieldId("92968407-4ebd-4092-90fb-7bb7f660a4e2")]
private AssetReference<object> _projectilePrefab;

private Entity SpawnProjectile(Vector3 position, Quaternion rotation)
{
    if (!_projectilePrefab.IsValid)
        return default;

    PrefabInstance instance = Prefab.Instantiate(_projectilePrefab.Id, position, rotation);
    return instance.Root;
}
```

`PrefabInstance` contains the root plus the complete instantiated entity list. Passing `default(Quaternion)` uses the
identity rotation.

Use the typed asset marker supplied by the API when one exists. The current managed surface does not define a dedicated
prefab marker, so prefab calls use `AssetId`; the general serialized reference shown above retains stable identity.

## VFX

Declare a typed effect reference:

```csharp
[SerializeField, StableFieldId("d34681f7-8124-4abc-a09d-f9ccba57afae")]
private AssetReference<VfxEffect> _impact;
```

Play it on an entity with a VFX Emitter:

```csharp
if (_impact.IsValid)
{
    VfxEmitterHandle emitter = Vfx.Play(Entity, _impact);
    if (!emitter.IsValid)
        Debug.Warn("VFX playback was rejected.");
}
```

Control playback:

```csharp
Vfx.Pause(Entity);
Vfx.Resume(Entity);
bool alive = Vfx.IsAlive(Entity);
Vfx.Stop(Entity);

VfxEmitterHandle handle = new(Entity);
handle.Restart(_impact);
```

Handles validate their entity and required component. An effect reference must be valid before `Play` or `Restart`.

## Cursor Ownership

For a single simple owner, direct state methods are available:

```csharp
Cursor.Hide();
Cursor.Lock();
Cursor.Unlock();
Cursor.Show();
```

When multiple systems can influence the cursor, use scoped requests:

```csharp
private IDisposable? _capture;

protected override void OnEnable()
{
    _capture = Cursor.RequestCapture();
}

protected override void OnDisable()
{
    _capture?.Dispose();
    _capture = null;
}
```

`RequestCapture` hides and locks the cursor. `RequestVisible` makes it visible and unlocked. Visible requests take
priority while any are active; disposing the final visible request reveals any still-active capture request.

Release requests on disable and before reload. See [UI And Events](UiAndEvents.md) for menu coordination.

## Logging And Debug Drawing

Unity-style names:

```csharp
Debug.Log("Loaded");
Debug.Warn("Low ammunition");
Debug.Error("Missing target");
Debug.LogException(exception);
Debug.Assert(count >= 0, "Count cannot be negative.");
Debug.DrawLine(start, end, Color.White, duration: 1.0f);
```

Level-specific names:

```csharp
Log.Trace("Detailed trace");
Log.Debug("Debug detail");
Log.Info("Gameplay event");
Log.Warning("Recoverable issue");
Log.Error("Operation failed");
Log.Critical("Unrecoverable state");
```

Prefer useful context such as entity name, asset purpose, and operation. Avoid logging every frame.

## Profiling

Record a scoped CPU sample:

```csharp
using (Profiler.Sample("EnemyDirector.Update"))
{
    UpdateEnemies();
}
```

Record a current value:

```csharp
Profiler.Counter("Enemies.Active", activeEnemyCount);
```

Sample and counter names are registered and cached. Use stable names so profiler history groups consistently. Counter
values must be finite; non-finite values are ignored.

See [Profiling](../Profiling.md) for the profiler UI and native integration.

## Math Values

The managed API supplies `Vector2`, `Vector3`, `Vector4`, `Quaternion`, and `Color`. Common helpers include:

```csharp
Vector3 direction = (target - origin).Normalized;
float alignment = Vector3.Dot(direction, Entity.Transform.Forward);
Vector3 reflected = Vector3.Reflect(direction, surfaceNormal);
Vector3 blended = Vector3.Lerp(from, to, amount);
Quaternion facing = Quaternion.Euler(pitchDegrees, yawDegrees);
```

Vector normalization returns zero for a near-zero vector. `Vector3.Lerp` clamps its amount to `[0, 1]`.

## Production Gameplay Libraries

`Keire.Managed` also contains the first-party weapon, damage, ballistics, recoil, and weapon-presentation foundations.
Their data-authoring workflow is covered by [Weapon Authoring](../WeaponAuthoring.md), while the sandbox
`WeaponController.cs` demonstrates composition from a `Behaviour`.
