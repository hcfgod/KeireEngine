internal static class RuntimeServiceTests
{
    internal static void Run()
    {
        RuntimeServiceProbeLog.Events.Clear();
        var previousState = new Dictionary<Guid, Keire.ManagedSerializedValue>
        {
            [Guid.Parse("73616e64-626f-4078-8000-00000000f501")] = Keire.ManagedSerializedValue.From(7L),
        };
        using var host = Keire.RuntimeServiceHost.Start(
            12, [typeof(RuntimeServiceRoot), typeof(RuntimeServiceDependent), typeof(OptionalRuntimeService)],
            previousState);
        Assert(host.OrderedServiceIds.SequenceEqual(
                   [
                       Guid.Parse("73616e64-626f-4078-8000-00000000f501"),
                       Guid.Parse("73616e64-626f-4078-8000-00000000f502"),
                       Guid.Parse("73616e64-626f-4078-8000-00000000f503"),
                   ]),
               "Runtime services must start in dependency order with stable IDs as the deterministic tie-breaker.");
        Assert(host.GetService<RuntimeServiceRoot>()?.Value == 7 &&
                   RuntimeServiceProbeLog.Events.Take(3).SequenceEqual(["root:start", "dependent:start", "optional:start"]),
               "Runtime services must restore candidate state after dependencies start.");

        host.Update(0.016, 0.02, 1);
        Assert(host.Diagnostics.Count == 1 && host.Diagnostics[0].Quarantined &&
                   host.GetService<OptionalRuntimeService>() is null,
               "An optional runtime-service update failure must quarantine only that service.");
        IReadOnlyDictionary<Guid, Keire.ManagedSerializedValue> state = host.CaptureHotReloadState();
        Assert(state.Single().Value.AsInt64() == 8,
               "Runtime service state must use bounded managed values for last-good reload transfer.");

        host.Dispose();
        Assert(RuntimeServiceProbeLog.Events.TakeLast(2).SequenceEqual(["dependent:stop", "root:stop"]),
               "Runtime services must stop in reverse dependency order.");

        AssertThrows<InvalidOperationException>(
            () => Keire.RuntimeServiceHost.Start(1, [typeof(CyclicRuntimeServiceA), typeof(CyclicRuntimeServiceB)]),
            "A runtime-service dependency cycle must reject the complete candidate.");

        RuntimeServiceProbeLog.Events.Clear();
        Keire.ManagedCandidateTypeRegistry.Install(typeof(Keire.ManagedRuntimeServiceBridge),
            [typeof(RuntimeServiceRoot), typeof(RuntimeServiceDependent)]);
        Assert(Keire.ManagedRuntimeServiceBridge.Stage(20, "{}") &&
                   !Keire.ManagedRuntimeServiceBridge.Update(20, 0.016, 0.016, 1),
               "A staged runtime-service generation must reject updates before publication.");
        Assert(Keire.ManagedRuntimeServiceBridge.Commit(20) &&
                   Keire.ManagedRuntimeServiceBridge.Update(20, 0.016, 0.016, 1),
               "A committed runtime-service generation must receive owner-thread updates.");
        string bridgedState = Keire.ManagedRuntimeServiceBridge.CaptureState(20);
        Assert(bridgedState.Contains("73616e64-626f-4078-8000-00000000f501", StringComparison.Ordinal),
               "The native bridge must capture runtime-service hot-reload state by stable ID.");
        Assert(!Keire.ManagedRuntimeServiceBridge.Update(19, 0.016, 0.016, 2) &&
                   Keire.ManagedRuntimeServiceBridge.Shutdown(20),
               "The native bridge must reject stale calls and stop the active generation explicitly.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void AssertThrows<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }
}

internal static class RuntimeServiceProbeLog
{
    internal static List<string> Events { get; } = [];
}

[Keire.RuntimeService("73616e64-626f-4078-8000-00000000f501")]
internal sealed class RuntimeServiceRoot : Keire.IRuntimeService, Keire.IRuntimeServiceHotReloadState
{
    public int Value { get; private set; }

    public void Start(Keire.RuntimeServiceContext context) => RuntimeServiceProbeLog.Events.Add("root:start");

    public void Update(Keire.RuntimeServiceUpdateContext context) => ++Value;

    public void Stop() => RuntimeServiceProbeLog.Events.Add("root:stop");

    public Keire.ManagedSerializedValue CaptureState() => Keire.ManagedSerializedValue.From((long)Value);

    public void RestoreState(Keire.ManagedSerializedValue state) => Value = checked((int)state.AsInt64());
}

[Keire.RuntimeService("73616e64-626f-4078-8000-00000000f502")]
[Keire.RuntimeServiceDependency(typeof(RuntimeServiceRoot))]
internal sealed class RuntimeServiceDependent : Keire.IRuntimeService
{
    public void Start(Keire.RuntimeServiceContext context)
    {
        _ = context.GetRequiredService<RuntimeServiceRoot>();
        RuntimeServiceProbeLog.Events.Add("dependent:start");
    }

    public void Update(Keire.RuntimeServiceUpdateContext context) { }

    public void Stop() => RuntimeServiceProbeLog.Events.Add("dependent:stop");
}

[Keire.RuntimeService("73616e64-626f-4078-8000-00000000f503", optional: true)]
internal sealed class OptionalRuntimeService : Keire.IRuntimeService
{
    public void Start(Keire.RuntimeServiceContext context) => RuntimeServiceProbeLog.Events.Add("optional:start");

    public void Update(Keire.RuntimeServiceUpdateContext context) =>
        throw new InvalidOperationException("Expected optional failure.");

    public void Stop() => RuntimeServiceProbeLog.Events.Add("optional:stop");
}

[Keire.RuntimeService("73616e64-626f-4078-8000-00000000f504")]
[Keire.RuntimeServiceDependency(typeof(CyclicRuntimeServiceB))]
internal sealed class CyclicRuntimeServiceA : Keire.IRuntimeService
{
    public void Start(Keire.RuntimeServiceContext context) { }
    public void Update(Keire.RuntimeServiceUpdateContext context) { }
    public void Stop() { }
}

[Keire.RuntimeService("73616e64-626f-4078-8000-00000000f505")]
[Keire.RuntimeServiceDependency(typeof(CyclicRuntimeServiceA))]
internal sealed class CyclicRuntimeServiceB : Keire.IRuntimeService
{
    public void Start(Keire.RuntimeServiceContext context) { }
    public void Update(Keire.RuntimeServiceUpdateContext context) { }
    public void Stop() { }
}
