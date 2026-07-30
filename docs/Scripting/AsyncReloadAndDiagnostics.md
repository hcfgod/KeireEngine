# Async, Reload, And Diagnostics

Managed scripting is built around last-good generations. Async work, reload state, runtime-only registrations, and
failure handling should all preserve that boundary.

## The Behaviour Synchronization Context

Lifecycle callbacks run on the simulation thread with a `Behaviour`-owned synchronization context. An `await` that
captures this context resumes through that context rather than mutating scene state from an arbitrary worker thread.

```csharp
private async Task RefreshAsync(CancellationToken cancellation)
{
    NavigationPath path = await Navigation.FindPathAsync(
        Entity.Transform.Position,
        _destination,
        cancellation: cancellation);

    cancellation.ThrowIfCancellationRequested();
    if (Entity.IsValid)
        ApplyPath(path);
}
```

The context does not make an entity immortal. Validate handles after asynchronous suspension.

## Lifetime Cancellation

Every `Behaviour` exposes `LifetimeToken`. Kéire cancels it during:

- disable;
- destruction;
- managed reload;
- Play Mode teardown.

Start lifetime-bound work without `async void`:

```csharp
protected override void OnEnable()
{
    _ = RunAsync(LifetimeToken);
}

private async Task RunAsync(CancellationToken cancellation)
{
    try
    {
        await RefreshAsync(cancellation);
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
        // Expected lifecycle cancellation.
    }
    catch (Exception exception)
    {
        Debug.LogException(exception);
    }
}
```

A fire-and-forget task does not throw through the lifecycle callback that started it. Observe and handle its failures;
do not assume the runtime's callback quarantine will see an exception stored on an unobserved task.

## Adding Your Own Cancellation

Link an operation-specific token to the Behaviour lifetime:

```csharp
private CancellationTokenSource? _searchCancellation;

private void StartSearch()
{
    CancelSearch();
    _searchCancellation = CancellationTokenSource.CreateLinkedTokenSource(LifetimeToken);
    _ = SearchAsync(_searchCancellation.Token);
}

private void CancelSearch()
{
    _searchCancellation?.Cancel();
    _searchCancellation?.Dispose();
    _searchCancellation = null;
}

protected override void OnDisable()
{
    CancelSearch();
}

protected override void OnBeforeReload()
{
    CancelSearch();
}
```

Although the linked source observes `LifetimeToken`, explicitly disposing it releases registrations promptly and keeps
the ownership pattern obvious.

## Do Not Retain Old-Generation Objects

After reload, managed objects from the retired context must be collectible. Common roots that accidentally retain them
include:

- static C# events;
- delegates registered with runtime services;
- timers and long-running tasks;
- stored `Behaviour` object references;
- undisposed cursor requests or other tokens;
- custom thread-local or process-global collections.

Store stable value identity such as `Entity` or `AssetReference<T>` where a relationship must be resolved again.
Unsubscribe and cancel in `OnBeforeReload`; reacquire in `OnAfterReload`.

## Persistent And Reload-Only State

```csharp
[SerializeField, StableFieldId("5340887d-42c5-44d4-8e1a-88eb0454656e")]
private float _movementSpeed = 5.0f;

[HotReloadState]
private Vector3 _velocity;

private IDisposable? _cursorCapture;
```

- `_movementSpeed` is saved in scene/prefab state and migrated.
- `_velocity` is migrated only between Play Mode generations.
- `_cursorCapture` is neither serialized nor migrated; release and reacquire it.

Successful reload does not rerun `Awake` or `Start`.

## Transactional Reload

A script save does not immediately replace live code. Kéire:

1. builds a candidate immutable generation;
2. discovers and validates managed types;
3. captures active instance state;
4. invokes `OnBeforeReload`;
5. constructs and hydrates candidate managed data and Behaviours;
6. rejects the candidate if any required migration fails;
7. otherwise swaps generations and invokes `OnAfterReload`.

A compile failure, type-discovery failure, or migration failure leaves the active generation unchanged. During a
failed candidate reload, old instances can resume with a fresh synchronization context.

## Callback Exceptions

An exception escaping a managed lifecycle callback quarantines only that `Behaviour`. Diagnostics identify:

- entity and instance;
- callback;
- managed type;
- active script generation;
- managed exception text.

Use `Debug.LogException` when handling an exception locally. Avoid a broad `catch (Exception)` that logs and continues
when invariants are already broken.

## Logging

Use `Debug` for familiar gameplay diagnostics:

```csharp
Debug.Log("Door opened.");
Debug.Warn("Optional target is missing.");
Debug.Error("Required asset was not assigned.");
Debug.Assert(_count >= 0, "Count must remain non-negative.");
```

Use `Log` for explicit severity:

```csharp
Log.Info($"Loaded tuning for {Entity.Name}.");
Log.Warning("Using fallback configuration.");
```

Good messages answer:

- what operation failed;
- which entity, asset purpose, state, or action was involved;
- whether the script recovered or stopped.

Do not log inside a normal per-frame path unless diagnosing a temporary issue.

## Profiling Managed Work

```csharp
protected override void Update()
{
    using (Profiler.Sample("EnemyDirector.Update"))
    {
        UpdateDirector();
    }

    Profiler.Counter("EnemyDirector.Active", _activeEnemies.Count);
}
```

Kéire also records managed callback calls, skips, interop calls, cumulative callback time, and maximum callback
duration. An empty overridden update still incurs managed callback participation, so remove overrides that do no work.

## Build Diagnostics

The editor watches stable `.cs` and `.keireasm` changes and builds after a short debounce. A newer edit cancels an
obsolete build. Build failures never overwrite the last-good generation.

When a change appears not to run:

1. open the managed build or Console diagnostics;
2. confirm the candidate generation compiled;
3. check type-discovery and migration diagnostics after compilation;
4. verify the script is in a declared assembly source root;
5. verify the component is enabled and its entity is active in hierarchy;
6. confirm the instance was not quarantined by a callback exception.

## Troubleshooting

| Symptom | Likely cause | Resolution |
| --- | --- | --- |
| Cannot convert `AudioClip` to `AssetId` | Marker type used instead of an asset reference | Declare `AssetReference<AudioClip>` and pass it or `.Id` |
| Cannot compare `AssetReference<T>` with `null` | It is a value type | Use `.IsValid` |
| Script is absent from Add Component | File is outside a source root, build failed, or type shape is invalid | Check `.keireasm`, diagnostics, filename/type match, and stable ID |
| Saved code is not running | Candidate did not publish | Fix build, discovery, or migration diagnostics; last-good code remains active |
| Values reset after reload | Field is neither serialized nor `[HotReloadState]` | Add the attribute matching the intended persistence |
| Duplicate callbacks after reload | Runtime event was rebound without being removed | Unbind in `OnDisable` and `OnBeforeReload`; make binding idempotent |
| Cursor remains visible or captured | A scoped request was not disposed | Store the `IDisposable` and release it on every ownership exit |
| Async continuation touches a dead entity | Target was destroyed while awaiting | Pass cancellation and recheck `Entity.IsValid` after `await` |
| Animator getter throws | State parameter or layer is absent | Correct the controller name/type or use the matching `TryGet` API |
| Playback options throw | Values violate API limits | Validate bus, gain, pitch, priority, and distance values |
| UI click fires in one path but not another | Button and input handlers contain different logic | Route both to one action method |

## Handoff Checklist For A Script Change

- Focused behavior works from every intended input path.
- Optional entity and asset references are validated.
- Runtime subscriptions and tokens have symmetric cleanup.
- Async work observes `LifetimeToken`.
- Transient Play Mode state uses `[HotReloadState]` only when needed.
- Persistent fields have durable, unique stable IDs.
- Reload succeeds without duplicate listeners or stale objects.
- Managed build diagnostics are clean.
- Cooked dependency references are valid for any added assets or managed data.
