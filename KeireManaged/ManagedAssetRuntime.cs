using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;

namespace Keire;

internal delegate ValueTask<ScriptableObject?> ManagedAssetLoadProvider(AssetId id,
                                                                        CancellationToken cancellation);

internal sealed class NativeManagedAssetProvider(ulong generation)
{
    private readonly ConcurrentDictionary<AssetId, TaskCompletionSource<ScriptableObject?>> _pending = new();

    public ulong Generation { get; } = generation;

    public async ValueTask<ScriptableObject?> LoadAsync(AssetId id, CancellationToken cancellation)
    {
        cancellation.ThrowIfCancellationRequested();
        var completion = new TaskCompletionSource<ScriptableObject?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_pending.TryAdd(id, completion))
            throw new InvalidOperationException($"Native managed asset request {id} is already pending.");
        try
        {
            if (!NativeRuntime.HasManagedAssetLoadProvider)
                throw new InvalidOperationException("The native managed asset load provider is not bound.");
            using CancellationTokenRegistration registration =
                cancellation.Register(() => Cancel(id, completion, cancellation));
            if (!NativeRuntime.RequestManagedAssetLoad(Generation, id) && !completion.Task.IsCompleted)
                throw new InvalidOperationException($"The native managed asset provider rejected {id}.");
            return await completion.Task.ConfigureAwait(false);
        }
        finally
        {
            if (_pending.TryGetValue(id, out TaskCompletionSource<ScriptableObject?>? current) &&
                ReferenceEquals(current, completion))
                _pending.TryRemove(id, out _);
        }
    }

    public bool Complete(AssetId id, ScriptableObject asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return _pending.TryGetValue(id, out TaskCompletionSource<ScriptableObject?>? completion) &&
               completion.TrySetResult(asset);
    }

    public bool Fail(AssetId id, string diagnostic)
    {
        string message = string.IsNullOrWhiteSpace(diagnostic)
            ? $"The native managed asset provider failed to load {id}."
            : diagnostic;
        return _pending.TryGetValue(id, out TaskCompletionSource<ScriptableObject?>? completion) &&
               completion.TrySetException(new InvalidOperationException(message));
    }

    private void Cancel(AssetId id, TaskCompletionSource<ScriptableObject?> completion,
                        CancellationToken cancellation)
    {
        if (!completion.TrySetCanceled(cancellation))
            return;
        NativeRuntime.CancelManagedAssetLoad(Generation, id);
    }
}

internal readonly record struct ManagedAssetRegistrySnapshot(ulong Generation, int LoadedAssets,
                                                              int InFlightLoads, int MaximumLoadedAssets,
                                                              int MaximumInFlightLoads);

internal sealed class ManagedAssetRegistry : IDisposable
{
    private sealed class InFlightLoad
    {
        public readonly TaskCompletionSource<ScriptableObject> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private readonly object _gate = new();
    private readonly Dictionary<AssetId, ScriptableObject> _loaded = [];
    private readonly Dictionary<AssetId, InFlightLoad> _inFlight = [];
    private readonly CancellationTokenSource _shutdown = new();
    private readonly CancellationToken _shutdownToken;
    private readonly ManagedAssetLoadProvider? _provider;
    private readonly int _maximumLoadedAssets;
    private readonly int _maximumInFlightLoads;
    private bool _mutating;
    private bool _disposed;

    public ManagedAssetRegistry(ulong generation, int maximumLoadedAssets, int maximumInFlightLoads,
                                ManagedAssetLoadProvider? provider)
    {
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation), "A managed asset generation must be non-zero.");
        if (maximumLoadedAssets <= 0)
            throw new ArgumentOutOfRangeException(nameof(maximumLoadedAssets));
        if (maximumInFlightLoads <= 0 || maximumInFlightLoads > maximumLoadedAssets)
            throw new ArgumentOutOfRangeException(nameof(maximumInFlightLoads));
        Generation = generation;
        _maximumLoadedAssets = maximumLoadedAssets;
        _maximumInFlightLoads = maximumInFlightLoads;
        _provider = provider;
        _shutdownToken = _shutdown.Token;
    }

    public ulong Generation { get; }

    public void Register(AssetId id, ScriptableObject asset)
    {
        if (!id.IsValid)
            throw new ArgumentException("Asset ID must be valid.", nameof(id));
        ArgumentNullException.ThrowIfNull(asset);
        asset.BindAsset(id);

        InFlightLoad? completedLoad = null;
        lock (_gate)
        {
            BeginMutation();
            try
            {
                _loaded.TryGetValue(id, out ScriptableObject? previous);
                if (previous is null && _loaded.Count >= _maximumLoadedAssets)
                    throw new InvalidOperationException(
                        $"Managed asset generation {Generation} reached its {_maximumLoadedAssets}-asset capacity.");

                asset.Validate();
                if (ReferenceEquals(previous, asset))
                {
                    asset.Enable();
                }
                else
                {
                    bool activatedHere = asset.Enable();
                    try
                    {
                        previous?.Disable();
                    }
                    catch (Exception exception)
                    {
                        RollBackActivation(asset, activatedHere, exception);
                    }
                    _loaded[id] = asset;
                }

                if (_inFlight.Remove(id, out InFlightLoad? load))
                    completedLoad = load;
            }
            finally
            {
                EndMutation();
            }
        }
        completedLoad?.Completion.TrySetResult(asset);
    }

    public bool TryLoad<T>(AssetId id, out T? value) where T : class
    {
        lock (_gate)
        {
            if (!_disposed && id.IsValid && _loaded.TryGetValue(id, out ScriptableObject? asset) &&
                asset is T typed)
            {
                value = typed;
                return true;
            }
        }
        value = null;
        return false;
    }

    public bool Reload(AssetId id, ScriptableObject candidate)
    {
        if (!id.IsValid)
            throw new ArgumentException("Asset ID must be valid.", nameof(id));
        ArgumentNullException.ThrowIfNull(candidate);

        lock (_gate)
        {
            BeginMutation();
            try
            {
                if (!_loaded.TryGetValue(id, out ScriptableObject? active))
                    return false;
                if (ReferenceEquals(active, candidate))
                {
                    active.Validate();
                    return true;
                }
                if (active.GetType() != candidate.GetType())
                    throw new InvalidOperationException(
                        $"Managed asset {id} cannot reload from '{active.GetType().FullName}' to " +
                        $"'{candidate.GetType().FullName}'.");

                candidate.Validate();
                ScriptableObject rollback = ManagedObjectSerializer.Clone(active);
                try
                {
                    ManagedObjectSerializer.CopySerializedState(candidate, active);
                    active.Validate();
                }
                catch (Exception exception)
                {
                    RollBackReload(active, rollback, exception);
                }
                return true;
            }
            finally
            {
                EndMutation();
            }
        }
    }

    public ValueTask<T> LoadAsync<T>(AssetId id, CancellationToken cancellation)
        where T : ScriptableObject
    {
        cancellation.ThrowIfCancellationRequested();
        if (!id.IsValid)
            return ValueTask.FromException<T>(new ArgumentException("Asset ID must be valid.", nameof(id)));

        Task<ScriptableObject> task;
        InFlightLoad? startedLoad = null;
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_loaded.TryGetValue(id, out ScriptableObject? loaded))
                return loaded is T typed
                    ? ValueTask.FromResult(typed)
                    : ValueTask.FromException<T>(TypeMismatch<T>(id, loaded));

            if (_provider is null)
                return ValueTask.FromException<T>(
                    new InvalidOperationException(
                        $"Managed asset {id} is not loaded and no provider exists."));

            if (!_inFlight.TryGetValue(id, out InFlightLoad? inFlight))
            {
                if (_loaded.Count + _inFlight.Count >= _maximumLoadedAssets)
                    return ValueTask.FromException<T>(new InvalidOperationException(
                        $"Managed asset generation {Generation} has no remaining asset capacity."));
                if (_inFlight.Count >= _maximumInFlightLoads)
                    return ValueTask.FromException<T>(new InvalidOperationException(
                        $"Managed asset generation {Generation} reached its " +
                        $"{_maximumInFlightLoads}-request asynchronous load capacity."));
                inFlight = new InFlightLoad();
                _inFlight.Add(id, inFlight);
                startedLoad = inFlight;
            }
            task = inFlight.Completion.Task;
        }

        if (startedLoad is not null)
            _ = ResolveAsync(id, startedLoad);
        return new ValueTask<T>(AwaitTyped<T>(id, task, cancellation));
    }

    public bool Unload(AssetId id)
    {
        if (!id.IsValid)
            return false;
        lock (_gate)
        {
            BeginMutation();
            try
            {
                if (!_loaded.TryGetValue(id, out ScriptableObject? asset))
                    return false;
                asset.Disable();
                return _loaded.Remove(id);
            }
            finally
            {
                EndMutation();
            }
        }
    }

    public ManagedAssetRegistrySnapshot Snapshot()
    {
        lock (_gate)
        {
            return new ManagedAssetRegistrySnapshot(Generation, _loaded.Count, _inFlight.Count,
                                                    _maximumLoadedAssets, _maximumInFlightLoads);
        }
    }

    public void Dispose()
    {
        List<InFlightLoad> canceledLoads;
        lock (_gate)
        {
            if (_disposed)
                return;
            BeginMutation();
            try
            {
                var disabled = new List<ScriptableObject>(_loaded.Count);
                try
                {
                    foreach (ScriptableObject asset in _loaded.OrderBy(pair => pair.Key.High)
                                 .ThenBy(pair => pair.Key.Low).Select(pair => pair.Value))
                    {
                        if (asset.Disable())
                            disabled.Add(asset);
                    }
                }
                catch (Exception exception)
                {
                    RollBackDeactivation(disabled, exception);
                }

                _disposed = true;
                _loaded.Clear();
                canceledLoads = _inFlight.Values.ToList();
                _inFlight.Clear();
            }
            finally
            {
                EndMutation();
            }
        }

        _shutdown.Cancel();
        foreach (InFlightLoad load in canceledLoads)
            load.Completion.TrySetCanceled(_shutdownToken);
        _shutdown.Dispose();
    }

    private async Task ResolveAsync(AssetId id, InFlightLoad load)
    {
        try
        {
            ScriptableObject? asset = await _provider!(id, _shutdownToken).ConfigureAwait(false);
            if (asset is null)
                throw new InvalidOperationException($"The managed asset provider did not resolve {id}.");
            if (!load.Completion.Task.IsCompleted)
                Register(id, asset);
        }
        catch (OperationCanceledException) when (_shutdownToken.IsCancellationRequested)
        {
            load.Completion.TrySetCanceled(_shutdownToken);
        }
        catch (Exception exception)
        {
            load.Completion.TrySetException(exception);
        }
        finally
        {
            lock (_gate)
            {
                if (_inFlight.TryGetValue(id, out InFlightLoad? current) && ReferenceEquals(current, load))
                    _inFlight.Remove(id);
            }
        }
    }

    private static async Task<T> AwaitTyped<T>(AssetId id, Task<ScriptableObject> task,
                                                CancellationToken cancellation) where T : ScriptableObject
    {
        ScriptableObject loaded = cancellation.CanBeCanceled
            ? await task.WaitAsync(cancellation).ConfigureAwait(false)
            : await task.ConfigureAwait(false);
        return loaded is T typed ? typed : throw TypeMismatch<T>(id, loaded);
    }

    private static InvalidOperationException TypeMismatch<T>(AssetId id, ScriptableObject loaded) =>
        new($"Managed asset {id} is loaded as {loaded.GetType().FullName}, not {typeof(T).FullName}.");

    private void BeginMutation()
    {
        ThrowIfDisposed();
        if (_mutating)
            throw new InvalidOperationException("Managed asset lifecycle callbacks cannot mutate their registry.");
        _mutating = true;
    }

    private void EndMutation() => _mutating = false;

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ManagedAssetRegistry));
    }

    private static void RollBackActivation(ScriptableObject asset, bool activatedHere, Exception exception)
    {
        if (activatedHere)
        {
            try
            {
                asset.Disable();
            }
            catch (Exception rollbackException)
            {
                throw new AggregateException("Managed asset replacement rollback failed.", exception,
                                             rollbackException);
            }
        }
        ExceptionDispatchInfo.Capture(exception).Throw();
    }

    private static void RollBackDeactivation(IReadOnlyList<ScriptableObject> disabled, Exception exception)
    {
        List<Exception>? rollbackFailures = null;
        for (int index = disabled.Count - 1; index >= 0; --index)
        {
            try
            {
                disabled[index].Enable();
            }
            catch (Exception rollbackException)
            {
                rollbackFailures ??= [exception];
                rollbackFailures.Add(rollbackException);
            }
        }
        if (rollbackFailures is not null)
            throw new AggregateException("Managed asset generation reset rollback failed.", rollbackFailures);
        ExceptionDispatchInfo.Capture(exception).Throw();
    }

    private static void RollBackReload(ScriptableObject active, ScriptableObject rollback, Exception exception)
    {
        try
        {
            ManagedObjectSerializer.CopySerializedState(rollback, active);
        }
        catch (Exception rollbackException)
        {
            throw new AggregateException("Managed asset hot-reload rollback failed.", exception, rollbackException);
        }
        ExceptionDispatchInfo.Capture(exception).Throw();
    }
}

public static class Assets
{
    public static AssetLoadOperation<T> LoadRuntime<T>(T asset,
                                                       AssetLoadPriority priority = AssetLoadPriority.Normal)
        where T : Asset
    {
        ArgumentNullException.ThrowIfNull(asset);
        if (!asset.Id.IsValid)
            throw new ArgumentException("Runtime asset loading requires a persistent asset.", nameof(asset));
        if (priority is < AssetLoadPriority.Critical or > AssetLoadPriority.Background)
            throw new ArgumentOutOfRangeException(nameof(priority));
        ulong generation = NativeRuntime.ManagedAssets?.Generation ??
            throw new InvalidOperationException("No managed asset generation is installed for this application.");
        ulong handle = NativeAssets.BeginRuntimeAssetLoad(generation, asset.Id, RuntimeAssetType<T>.Id, priority);
        return handle != 0
            ? new AssetLoadOperation<T>(asset, handle)
            : throw new InvalidOperationException(
                $"Runtime asset {asset.Id} could not enter the application asset pipeline.");
    }

    public static void Register<T>(AssetId id, T asset) where T : ScriptableObject =>
        RequireRegistry().Register(id, asset);

    public static T Load<T>(T asset) where T : ScriptableObject
    {
        ArgumentNullException.ThrowIfNull(asset);
        if (TryLoad(asset, out T? value))
            return value!;
        throw new InvalidOperationException($"Managed asset {asset.Id} is not loaded as {typeof(T).FullName}.");
    }

    public static bool TryLoad<T>(T asset, out T? value) where T : ScriptableObject
    {
        ArgumentNullException.ThrowIfNull(asset);
        ManagedAssetRegistry? registry = NativeRuntime.ManagedAssets;
        if (registry is not null)
            return registry.TryLoad(asset.Id, out value);
        value = null;
        return false;
    }

    public static ValueTask<T> LoadAsync<T>(T asset, CancellationToken cancellation = default)
        where T : ScriptableObject
    {
        cancellation.ThrowIfCancellationRequested();
        ManagedAssetRegistry? registry = NativeRuntime.ManagedAssets;
        return registry is null
            ? ValueTask.FromException<T>(
                new InvalidOperationException("No managed asset generation is installed for this application."))
            : registry.LoadAsync<T>(asset.Id, cancellation);
    }

    public static bool Unload(AssetId id)
    {
        ManagedAssetRegistry? registry = NativeRuntime.ManagedAssets;
        if (registry is null || !registry.Unload(id))
            return false;
        NativeRuntime.ReleaseManagedAsset(registry.Generation, id);
        return true;
    }

    private static ManagedAssetRegistry RequireRegistry() =>
        NativeRuntime.ManagedAssets ??
        throw new InvalidOperationException("No managed asset generation is installed for this application.");
}

internal static class ManagedAssetRuntimeSelfTests
{
    [StableAssetTypeId("6b656972-652d-4077-8000-000000002001")]
    private sealed class SelfTestAsset : ScriptableObject
    {
        public float Value = 123.0f;
        public int[] Modes = [1, 2, 3];
    }

    [StableAssetTypeId("6b656972-652d-4077-8000-000000002002")]
    private sealed class SelfTestInlineAsset : ScriptableObject
    {
        public SelfTestAsset Inline = ScriptableObject.CreateInstance<SelfTestAsset>();
    }

    [SerializableType]
    private sealed class SelfTestLeaf
    {
        public int Value;
    }

    [SerializableType]
    private sealed class SelfTestNode
    {
        public string Label = string.Empty;
        public List<SelfTestLeaf> Leaves = [];
        public SelfTestAsset? Asset;
        public SelfTestNode? Next;
    }

    [SerializableType]
    private sealed class SelfTestDictionary
    {
        public Dictionary<string, int> Values = [];
    }

    [SerializableType]
    private class SelfTestBase
    {
        public int Value = 1;
    }

    [SerializableType]
    private sealed class SelfTestDerived : SelfTestBase;

    [SerializableType]
    private sealed class SelfTestPolymorphic
    {
        public SelfTestBase Value = new SelfTestDerived();
    }

    internal static string Run()
    {
        if (NativeRuntime.ManagedAssets is not null)
            throw new InvalidOperationException("Managed asset self-tests require an application without a session.");

        const ulong generation = ulong.MaxValue - 1;
        var id = new AssetId(0x6b656972652d4077, 0x8000000000001001);
        var reference = (SelfTestAsset)Asset.FromId(typeof(SelfTestAsset), id)!;
        var release = new TaskCompletionSource<ScriptableObject?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        int providerCalls = 0;
        ManagedAssetLoadProvider provider = async (_, cancellation) =>
        {
            Interlocked.Increment(ref providerCalls);
            return await release.Task.WaitAsync(cancellation).ConfigureAwait(false);
        };

        NativeRuntime.InstallManagedAssetsForTests(generation, maximumLoadedAssets: 4,
                                                   maximumInFlightLoads: 2, provider);
        try
        {
            Task<SelfTestAsset> first = Assets.LoadAsync(reference).AsTask();
            Task<SelfTestAsset> second = Assets.LoadAsync(reference).AsTask();
            using var canceled = new CancellationTokenSource();
            Task<SelfTestAsset> canceledWaiter = Assets.LoadAsync(reference, canceled.Token).AsTask();
            canceled.Cancel();
            RequireCanceled(canceledWaiter);
            if (Volatile.Read(ref providerCalls) != 1)
                throw new InvalidOperationException("Managed asset asynchronous requests were not deduplicated.");

            SelfTestAsset asset = ScriptableObject.CreateInstance<SelfTestAsset>();
            asset.Name = "Self Test";
            asset.Value = 777.0f;
            release.SetResult(asset);
            SelfTestAsset firstResult = first.GetAwaiter().GetResult();
            SelfTestAsset secondResult = second.GetAwaiter().GetResult();
            if (!ReferenceEquals(firstResult, secondResult) || !ReferenceEquals(firstResult, asset))
                throw new InvalidOperationException("Managed asset asynchronous waiters did not retain identity.");

            SelfTestAsset replacement = ScriptableObject.CreateInstance<SelfTestAsset>();
            replacement.Name = "Reloaded Self Test";
            replacement.Value = 654.0f;
            if (!NativeRuntime.ManagedAssets!.Reload(id, replacement) ||
                !ReferenceEquals(Assets.Load(reference), asset) ||
                asset.Name != replacement.Name || asset.Value != replacement.Value)
                throw new InvalidOperationException("Managed asset hot reload did not retain object identity.");

            SelfTestAsset clone = ScriptableObject.Instantiate(asset);
            if (clone.RuntimeInstanceId == asset.RuntimeInstanceId || clone.Name != asset.Name ||
                clone.Value != asset.Value)
                throw new InvalidOperationException(
                    "Managed asset serialization clone did not preserve authored data.");

            var arraySource = ScriptableObject.CreateInstance<SelfTestAsset>();
            var arrayClone = ScriptableObject.Instantiate(arraySource);
            int[] sourceModes = arraySource.Modes;
            int[] cloneModes = arrayClone.Modes;
            if (ReferenceEquals(sourceModes, cloneModes) || !sourceModes.SequenceEqual(cloneModes))
                throw new InvalidOperationException("Managed asset serialization clone did not deep-copy arrays.");

            var nested = new SelfTestNode
            {
                Label = "nested",
                Leaves = [new SelfTestLeaf { Value = 42 }],
                Asset = reference
            };
            SelfTestNode nestedClone = ManagedObjectSerializer.CloneSerializableValueForTests(nested);
            if (ReferenceEquals(nested, nestedClone) || ReferenceEquals(nested.Leaves, nestedClone.Leaves) ||
                ReferenceEquals(nested.Leaves[0], nestedClone.Leaves[0]) ||
                nestedClone.Leaves[0].Value != 42 || nestedClone.Asset != reference)
                throw new InvalidOperationException(
                    "Managed asset serialization clone did not deep-copy nested values and lists.");

            nested.Next = nested;
            RequireFailure(() => ManagedObjectSerializer.CloneSerializableValueForTests(nested), "cyclic");
            RequireFailure(
                () => ManagedObjectSerializer.CloneSerializableValueForTests(new SelfTestDictionary()),
                "dictionaries");
            RequireFailure(
                () => ManagedObjectSerializer.CloneSerializableValueForTests(new SelfTestPolymorphic()),
                "polymorphic");

            var unsupported = ScriptableObject.CreateInstance<SelfTestInlineAsset>();
            RequireFailure(() => ScriptableObject.Instantiate(unsupported), "inline ScriptableObjects");

            if (!Assets.Unload(id) || Assets.TryLoad(reference, out _))
                throw new InvalidOperationException("Managed asset unload did not remove the registered object.");
            ManagedAssetRegistrySnapshot snapshot = NativeRuntime.ManagedAssets!.Snapshot();
            if (snapshot.LoadedAssets != 0 || snapshot.InFlightLoads != 0)
                throw new InvalidOperationException("Managed asset self-test leaked registry state.");
            return "ok";
        }
        finally
        {
            NativeRuntime.ResetManagedAssets(generation);
        }
    }

    private static void RequireCanceled(Task task)
    {
        try
        {
            task.GetAwaiter().GetResult();
        }
        catch (OperationCanceledException)
        {
            return;
        }
        throw new InvalidOperationException("A canceled managed asset waiter completed successfully.");
    }

    private static void RequireFailure(Action action, string expectedMessage)
    {
        try
        {
            action();
        }
        catch (InvalidOperationException exception) when (
            exception.Message.Contains(expectedMessage, StringComparison.Ordinal))
        {
            return;
        }
        throw new InvalidOperationException($"Expected a managed asset failure containing '{expectedMessage}'.");
    }
}
