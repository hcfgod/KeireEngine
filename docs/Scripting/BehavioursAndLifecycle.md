# Behaviours And Lifecycle

A `Behaviour` is a managed component attached to one entity. Kéire invokes only callbacks that the type overrides,
tracks update callback availability, and isolates a failing instance rather than taking down every script.

## Callback Reference

| Callback | When it runs | Typical use |
| --- | --- | --- |
| `Awake()` | Once when the instance is first attached to its runtime entity | Initialize local state and required services |
| `OnEnable()` | Each time the enabled instance becomes active | Subscribe, bind UI, and acquire scoped requests |
| `Start()` | Once before the first update after initial enable | Begin work that depends on other awakened objects |
| `FixedUpdate()` | On fixed simulation ticks | Physics-facing and deterministic simulation work |
| `Update()` | Once per variable frame | Input polling and ordinary gameplay |
| `LateUpdate()` | After ordinary updates | Camera follow and work that consumes final frame state |
| `OnDisable()` | When the instance is disabled or leaves active execution | Unsubscribe, cancel, and release external ownership |
| `OnDestroy()` | Before permanent instance teardown | Release permanent registrations and final resources |
| `OnCollisionEnter/Stay/Exit()` | For ordered non-trigger physics contacts | Collision gameplay |
| `OnTriggerEnter/Stay/Exit()` | For ordered trigger contacts | Volumes and detection |
| `OnAnimationEvent()` | When an animation event crosses the playback cursor | Footsteps, effects, and gameplay markers |
| `OnAnimatorIk()` | Reserved in the managed base class; not dispatched by the current runtime | Do not depend on it yet |
| `OnBeforeReload()` | Before the active managed context is migrated | Release old-context subscriptions and requests |
| `OnAfterReload()` | After a candidate instance is hydrated and activated | Rebind runtime-only relationships |

`Awake` and `Start` do not run again after a successful script reload. Use `OnAfterReload` for work that the replacement
instance must reacquire.

## Entity And Enabled State

Every attached instance exposes:

```csharp
public Entity Entity { get; }
public bool Enabled { get; set; }
public CancellationToken LifetimeToken { get; }
```

`Entity` is assigned before lifecycle callbacks begin. `Entity.Active` controls the scene object's local active state;
`Entity.ActiveInHierarchy` also accounts for its parents.

The current `Behaviour.Enabled` auto-property is not synchronized with the native component enabled state. Use a
component handle when code must enable or disable a script:

```csharp
ComponentHandle<PauseMenu> menu = Entity.GetComponentHandle<PauseMenu>();
if (menu.IsValid)
    menu.Enabled = false;
```

Do not cache native pointers or assume an entity remains alive. Kéire exposes only validated value handles. Check
`Entity.IsValid` when a reference may outlive the target.

## Choosing An Update Callback

Use `FixedUpdate` for work coupled to fixed simulation and `Update` for input and visual-frame behavior:

```csharp
protected override void FixedUpdate()
{
    SimulateMotor(Time.FixedDeltaTime);
}

protected override void Update()
{
    if (Input.Pressed("Interact"))
        TryInteract();
}
```

Use `Time.DeltaTime` for frame-rate-independent variable updates. Use `Time.UnscaledDeltaTime` for behavior that must
continue independently of scaled simulation time, such as some pause-menu animation. Avoid overriding an update
callback with an empty method; Kéire detects overridden update methods so it can skip unused managed calls.

## Execution Order

`ExecutionOrder` provides a stable relative order for managed components:

```csharp
[ExecutionOrder(-100)]
public sealed class PlayerInput : Behaviour
{
}

[ExecutionOrder(100)]
public sealed class FollowCamera : Behaviour
{
}
```

Lower values run before higher values at the same managed callback boundary. Use order only for a real data dependency;
prefer explicit references and clear ownership over a large web of order numbers.

## Required Components

`RequireComponent` is declared in the managed metadata surface:

```csharp
[StableComponentId("ff29550b-903a-4509-b7b8-67fd680f34f3")]
[RequireComponent(typeof(AudioSourceComponent))]
public sealed class AmbientEmitter : Behaviour
{
}
```

Multiple attributes are allowed, and the component type must have a `StableComponentId`. The current native
attachment path does not consume this metadata to add or reject missing components, so treat it as descriptive for now
and still validate the dependency:

```csharp
protected override void Awake()
{
    if (!Entity.HasComponent<AudioSourceComponent>())
        throw new InvalidOperationException("AmbientEmitter requires an Audio Source.");
}
```

Built-in marker types are listed in the [Managed API Index](ApiIndex.md).

## Cleanup Is Part Of The Contract

Runtime-only subscriptions and scoped ownership should be symmetric:

```csharp
private IDisposable? _cursorCapture;

protected override void OnEnable()
{
    GameState.Changed -= HandleGameStateChanged;
    GameState.Changed += HandleGameStateChanged;
    _cursorCapture ??= Cursor.RequestCapture();
}

protected override void OnDisable()
{
    GameState.Changed -= HandleGameStateChanged;
    ReleaseCursor();
}

protected override void OnBeforeReload()
{
    GameState.Changed -= HandleGameStateChanged;
    ReleaseCursor();
}

protected override void OnAfterReload()
{
    GameState.Changed -= HandleGameStateChanged;
    GameState.Changed += HandleGameStateChanged;
    _cursorCapture ??= Cursor.RequestCapture();
}

private void ReleaseCursor()
{
    _cursorCapture?.Dispose();
    _cursorCapture = null;
}
```

Use `OnDisable` for relationships that should pause with the component. Use `OnDestroy` for permanent registries that
were created in `Awake` and must survive an ordinary disable. If registration is reacquired in `OnEnable`, release it
in `OnDisable`.

## Reload Lifecycle

Play Mode reload is transactional:

1. Kéire captures persistent and `[HotReloadState]` fields.
2. Existing instances receive `OnBeforeReload`.
3. Candidate assemblies load into a new managed context.
4. Candidate objects are constructed, attached, and hydrated.
5. Every migration is validated.
6. The candidate becomes active and receives `OnAfterReload`.
7. The old context is retired.

If any candidate fails, Kéire abandons it and retains the last-good generation. The previous objects can resume without
rerunning `Awake` or `Start`.

Do not keep old-context delegates in static events, native registrations, or long-lived tasks. Unsubscribe and cancel
them in `OnBeforeReload`, then recreate them in `OnAfterReload`.

## Async Lifetime

Each instance owns a synchronization context and cancellation token. Managed continuations captured from lifecycle
callbacks resume on the simulation thread. Disable, destroy, reload, and Play Mode teardown cancel lifetime-bound work.

```csharp
protected override void OnEnable()
{
    _ = RefreshPathAsync(LifetimeToken);
}

private async Task RefreshPathAsync(CancellationToken cancellation)
{
    try
    {
        NavigationPath path = await Navigation.FindPathAsync(
            Entity.Transform.Position,
            _destination,
            cancellation: cancellation);

        if (!cancellation.IsCancellationRequested && Entity.IsValid)
            DrawPath(path);
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
        // Expected when this Behaviour is disabled, destroyed, or reloaded.
    }
}
```

Prefer `Task`-returning private methods over `async void`. Pass `LifetimeToken` to cancellation-aware APIs and still
validate referenced entities after an `await`.

See [Async, Reload, And Diagnostics](AsyncReloadAndDiagnostics.md) for linked cancellation and failure patterns.

`OnAnimatorIk` is present on `Behaviour` for the planned callback surface, but the current runtime does not dispatch
it. Submit named IK goals from `Update`, `LateUpdate`, animation events, or another callback that is currently
dispatched.

## Exception Isolation

An exception escaping a lifecycle callback quarantines that `Behaviour` instance. Diagnostics include the callback,
entity, managed type, script generation, and managed exception text. Other instances continue running.

Catch exceptions only when the script can recover or add useful context. Do not hide invariant failures just to keep an
instance enabled; the quarantine diagnostic is more useful than silently corrupted gameplay state.
