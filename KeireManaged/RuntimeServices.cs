using System.Buffers;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace Keire;

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class RuntimeServiceAttribute : Attribute
{
    public RuntimeServiceAttribute(string stableId, bool optional = false)
    {
        if (!Guid.TryParse(stableId, out Guid parsed) || parsed == Guid.Empty)
            throw new ArgumentException("Runtime service stable IDs must be non-empty GUIDs.", nameof(stableId));
        StableId = parsed;
        Optional = optional;
    }

    public Guid StableId { get; }
    public bool Optional { get; }
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class RuntimeServiceDependencyAttribute : Attribute
{
    public RuntimeServiceDependencyAttribute(Type serviceType, bool optional = false)
    {
        ServiceType = serviceType ?? throw new ArgumentNullException(nameof(serviceType));
        Optional = optional;
    }

    public Type ServiceType { get; }
    public bool Optional { get; }
}

public interface IRuntimeService
{
    void Start(RuntimeServiceContext context);
    void Update(RuntimeServiceUpdateContext context);
    void Stop();
}

public interface IRuntimeServiceHotReloadState
{
    ManagedSerializedValue CaptureState();
    void RestoreState(ManagedSerializedValue state);
}

public sealed class RuntimeServiceContext
{
    private readonly Func<Type, IRuntimeService?> _resolve;

    internal RuntimeServiceContext(ulong generation, CancellationToken lifetimeToken,
                                   Func<Type, IRuntimeService?> resolve)
    {
        Generation = generation;
        LifetimeToken = lifetimeToken;
        _resolve = resolve;
    }

    public ulong Generation { get; }
    public CancellationToken LifetimeToken { get; }

    public T GetRequiredService<T>() where T : class, IRuntimeService =>
        GetService<T>() ?? throw new InvalidOperationException(
            $"Required runtime service '{typeof(T).FullName}' is unavailable.");

    public T? GetService<T>() where T : class, IRuntimeService => _resolve(typeof(T)) as T;
}

public readonly record struct RuntimeServiceUpdateContext
{
    public RuntimeServiceUpdateContext(double deltaSeconds, double unscaledDeltaSeconds, ulong frame,
                                       CancellationToken lifetimeToken)
    {
        if (!double.IsFinite(deltaSeconds) || deltaSeconds < 0.0)
            throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
        if (!double.IsFinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0.0)
            throw new ArgumentOutOfRangeException(nameof(unscaledDeltaSeconds));
        DeltaSeconds = deltaSeconds;
        UnscaledDeltaSeconds = unscaledDeltaSeconds;
        Frame = frame;
        LifetimeToken = lifetimeToken;
    }

    public double DeltaSeconds { get; }
    public double UnscaledDeltaSeconds { get; }
    public ulong Frame { get; }
    public CancellationToken LifetimeToken { get; }
}

public sealed record RuntimeServiceDiagnostic(Guid StableId, string ServiceType, string Phase, string Message,
                                              bool Quarantined);

public sealed class RuntimeServiceHost : IDisposable
{
    private sealed record Descriptor(Type Type, RuntimeServiceAttribute Attribute,
                                     IReadOnlyList<RuntimeServiceDependencyAttribute> Dependencies);
    private sealed record ActiveService(Descriptor Descriptor, IRuntimeService Instance);

    private readonly int _ownerThread = Environment.CurrentManagedThreadId;
    private readonly CancellationTokenSource _lifetime = new();
    private readonly List<ActiveService> _active = [];
    private readonly Dictionary<Type, IRuntimeService> _instances = [];
    private readonly List<RuntimeServiceDiagnostic> _diagnostics = [];
    private bool _stopped;

    private RuntimeServiceHost(ulong generation)
    {
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation));
        Generation = generation;
    }

    public ulong Generation { get; }
    public CancellationToken LifetimeToken => _lifetime.Token;
    public IReadOnlyList<RuntimeServiceDiagnostic> Diagnostics => _diagnostics;
    public IReadOnlyList<Guid> OrderedServiceIds => _active.Select(value => value.Descriptor.Attribute.StableId).ToArray();

    public static RuntimeServiceHost Start(ulong generation, IEnumerable<Type> exactAllowedTypes,
                                           IReadOnlyDictionary<Guid, ManagedSerializedValue>? previousState = null)
    {
        ArgumentNullException.ThrowIfNull(exactAllowedTypes);
        var host = new RuntimeServiceHost(generation);
        try
        {
            host.StartCandidate(Discover(exactAllowedTypes), previousState);
            return host;
        }
        catch
        {
            host.StopCore();
            throw;
        }
    }

    public T? GetService<T>() where T : class, IRuntimeService
    {
        EnsureOwnerThread();
        EnsureActive();
        return _instances.TryGetValue(typeof(T), out IRuntimeService? value) ? value as T : null;
    }

    public void Update(double deltaSeconds, double unscaledDeltaSeconds, ulong frame)
    {
        EnsureOwnerThread();
        EnsureActive();
        var context = new RuntimeServiceUpdateContext(deltaSeconds, unscaledDeltaSeconds, frame, _lifetime.Token);
        foreach (ActiveService service in _active.ToArray())
        {
            try
            {
                service.Instance.Update(context);
            }
            catch (Exception exception)
            {
                if (!service.Descriptor.Attribute.Optional)
                {
                    throw new InvalidOperationException(
                        $"Required runtime service '{service.Descriptor.Type.FullName}' failed during update.",
                        exception);
                }
                Quarantine(service, "update", exception);
            }
        }
    }

    public IReadOnlyDictionary<Guid, ManagedSerializedValue> CaptureHotReloadState()
    {
        EnsureOwnerThread();
        EnsureActive();
        var result = new SortedDictionary<Guid, ManagedSerializedValue>();
        foreach (ActiveService service in _active)
        {
            if (service.Instance is IRuntimeServiceHotReloadState state)
            {
                ManagedSerializedValue value = state.CaptureState() ??
                    throw new InvalidOperationException(
                        $"Runtime service '{service.Descriptor.Type.FullName}' returned null hot-reload state.");
                result.Add(service.Descriptor.Attribute.StableId, value);
            }
        }
        return result;
    }

    public void Dispose()
    {
        EnsureOwnerThread();
        StopCore();
        GC.SuppressFinalize(this);
    }

    private void StartCandidate(IReadOnlyList<Descriptor> descriptors,
                                IReadOnlyDictionary<Guid, ManagedSerializedValue>? previousState)
    {
        foreach (Descriptor descriptor in descriptors)
        {
            IRuntimeService instance;
            try
            {
                instance = (IRuntimeService?)Activator.CreateInstance(descriptor.Type) ??
                    throw new InvalidOperationException("The service constructor returned null.");
                _instances.Add(descriptor.Type, instance);
                var context = new RuntimeServiceContext(Generation, _lifetime.Token,
                    type => _instances.GetValueOrDefault(type));
                instance.Start(context);
                if (previousState is not null &&
                    previousState.TryGetValue(descriptor.Attribute.StableId, out ManagedSerializedValue? restored) &&
                    instance is IRuntimeServiceHotReloadState hotReload)
                {
                    hotReload.RestoreState(restored);
                }
                _active.Add(new ActiveService(descriptor, instance));
            }
            catch (Exception exception)
            {
                _instances.Remove(descriptor.Type);
                if (!descriptor.Attribute.Optional)
                {
                    throw new InvalidOperationException(
                        $"Required runtime service '{descriptor.Type.FullName}' failed during candidate startup.",
                        exception);
                }
                _diagnostics.Add(new RuntimeServiceDiagnostic(
                    descriptor.Attribute.StableId, descriptor.Type.FullName ?? descriptor.Type.Name,
                    "start", exception.Message, true));
            }
        }
    }

    private void Quarantine(ActiveService service, string phase, Exception exception)
    {
        try
        {
            service.Instance.Stop();
        }
        catch (Exception stopException)
        {
            exception = new AggregateException(exception, stopException);
        }
        _active.Remove(service);
        _instances.Remove(service.Descriptor.Type);
        _diagnostics.Add(new RuntimeServiceDiagnostic(
            service.Descriptor.Attribute.StableId,
            service.Descriptor.Type.FullName ?? service.Descriptor.Type.Name,
            phase, exception.Message, true));
    }

    private void StopCore()
    {
        if (_stopped)
            return;
        _stopped = true;
        _lifetime.Cancel();
        for (int index = _active.Count - 1; index >= 0; --index)
        {
            try
            {
                _active[index].Instance.Stop();
            }
            catch (Exception exception)
            {
                _diagnostics.Add(new RuntimeServiceDiagnostic(
                    _active[index].Descriptor.Attribute.StableId,
                    _active[index].Descriptor.Type.FullName ?? _active[index].Descriptor.Type.Name,
                    "stop", exception.Message, false));
            }
        }
        _active.Clear();
        _instances.Clear();
        _lifetime.Dispose();
    }

    private static IReadOnlyList<Descriptor> Discover(IEnumerable<Type> allowedTypes)
    {
        var descriptors = new Dictionary<Type, Descriptor>();
        var ids = new Dictionary<Guid, Type>();
        foreach (Type type in allowedTypes.Distinct().OrderBy(type => type.FullName, StringComparer.Ordinal))
        {
            RuntimeServiceAttribute? attribute = type.GetCustomAttribute<RuntimeServiceAttribute>(false);
            if (attribute is null)
                continue;
            if (!typeof(IRuntimeService).IsAssignableFrom(type) || type.IsAbstract || type.IsInterface ||
                type.ContainsGenericParameters || type.GetConstructor(Type.EmptyTypes) is null)
            {
                throw new InvalidOperationException(
                    $"Runtime service '{type.FullName}' must be a closed concrete IRuntimeService with a public " +
                    "parameterless constructor.");
            }
            if (!ids.TryAdd(attribute.StableId, type))
            {
                throw new InvalidOperationException(
                    $"Runtime service ID '{attribute.StableId:D}' is shared by '{ids[attribute.StableId].FullName}' " +
                    $"and '{type.FullName}'.");
            }
            descriptors.Add(type, new Descriptor(type, attribute,
                type.GetCustomAttributes<RuntimeServiceDependencyAttribute>(false).ToArray()));
        }

        foreach (Descriptor descriptor in descriptors.Values)
        {
            var dependencies = new HashSet<Type>();
            foreach (RuntimeServiceDependencyAttribute dependency in descriptor.Dependencies)
            {
                if (!typeof(IRuntimeService).IsAssignableFrom(dependency.ServiceType) ||
                    !dependencies.Add(dependency.ServiceType))
                {
                    throw new InvalidOperationException(
                        $"Runtime service '{descriptor.Type.FullName}' has an invalid or duplicate dependency.");
                }
                if (!dependency.Optional && !descriptors.ContainsKey(dependency.ServiceType))
                {
                    throw new InvalidOperationException(
                        $"Runtime service '{descriptor.Type.FullName}' requires unavailable service " +
                        $"'{dependency.ServiceType.FullName}'.");
                }
            }
        }

        var result = new List<Descriptor>();
        var states = new Dictionary<Type, byte>();
        void Visit(Descriptor descriptor)
        {
            if (states.GetValueOrDefault(descriptor.Type) == 1)
                throw new InvalidOperationException($"Runtime service dependency cycle includes '{descriptor.Type.FullName}'.");
            if (states.GetValueOrDefault(descriptor.Type) == 2)
                return;
            states[descriptor.Type] = 1;
            foreach (RuntimeServiceDependencyAttribute dependency in descriptor.Dependencies
                         .OrderBy(value => descriptors.TryGetValue(value.ServiceType, out Descriptor? found)
                             ? found.Attribute.StableId : Guid.Empty))
            {
                if (descriptors.TryGetValue(dependency.ServiceType, out Descriptor? found))
                    Visit(found);
            }
            states[descriptor.Type] = 2;
            result.Add(descriptor);
        }
        foreach (Descriptor descriptor in descriptors.Values.OrderBy(value => value.Attribute.StableId))
            Visit(descriptor);
        return result;
    }

    private void EnsureOwnerThread()
    {
        if (Environment.CurrentManagedThreadId != _ownerThread)
            throw new InvalidOperationException("Runtime services are owned by their application thread.");
    }

    private void EnsureActive()
    {
        ObjectDisposedException.ThrowIf(_stopped, this);
    }
}

internal static class ManagedCandidateTypeRegistry
{
    private sealed class CandidateTypes(IReadOnlyList<Type> types)
    {
        internal IReadOnlyList<Type> Types { get; } = types;
    }

    private static readonly ConditionalWeakTable<AssemblyLoadContext, CandidateTypes> Registries = new();
    private static readonly object Sync = new();

    internal static void Install(Type contextType, IEnumerable<Type> exactAllowedTypes)
    {
        ArgumentNullException.ThrowIfNull(contextType);
        ArgumentNullException.ThrowIfNull(exactAllowedTypes);
        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(contextType.Assembly) ??
                                      AssemblyLoadContext.Default;
        Type[] types = exactAllowedTypes.Distinct().OrderBy(type => type.Assembly.GetName().Name,
                                               StringComparer.Ordinal)
            .ThenBy(type => type.FullName, StringComparer.Ordinal).ToArray();
        lock (Sync)
        {
            Registries.Remove(context);
            Registries.Add(context, new CandidateTypes(types));
        }
    }

    internal static IReadOnlyList<Type> Get(Type contextType)
    {
        ArgumentNullException.ThrowIfNull(contextType);
        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(contextType.Assembly) ??
                                      AssemblyLoadContext.Default;
        return Registries.TryGetValue(context, out CandidateTypes? registry)
            ? registry.Types
            : throw new InvalidOperationException(
                "The exact managed candidate allowlist has not been installed for this generation.");
    }
}

internal static class ManagedRuntimeServiceBridge
{
    private const int MaximumStateBytes = 16 * 1024 * 1024;
    private static RuntimeServiceHost? s_pending;
    private static RuntimeServiceHost? s_current;

    internal static bool Stage(ulong generation, string previousStateJson)
    {
        if (generation == 0 || s_pending is not null)
            return false;
        IReadOnlyDictionary<Guid, ManagedSerializedValue> previousState = ParseState(previousStateJson);
        s_pending = RuntimeServiceHost.Start(generation,
            ManagedCandidateTypeRegistry.Get(typeof(ManagedRuntimeServiceBridge)), previousState);
        return true;
    }

    internal static bool Commit(ulong generation)
    {
        if (s_pending?.Generation != generation || s_current is not null)
            return false;
        s_current = s_pending;
        s_pending = null;
        return true;
    }

    internal static bool Cancel(ulong generation)
    {
        if (s_pending?.Generation != generation)
            return false;
        RuntimeServiceHost pending = s_pending;
        s_pending = null;
        pending.Dispose();
        return true;
    }

    internal static string CaptureState(ulong generation)
    {
        if (s_current?.Generation != generation)
            throw new InvalidOperationException("A stale managed runtime-service generation attempted to capture state.");
        return WriteState(s_current.CaptureHotReloadState());
    }

    internal static bool Update(ulong generation, double deltaSeconds, double unscaledDeltaSeconds, ulong frame)
    {
        if (s_current?.Generation != generation)
            return false;
        s_current.Update(deltaSeconds, unscaledDeltaSeconds, frame);
        return true;
    }

    internal static bool Shutdown(ulong generation)
    {
        if (s_current?.Generation != generation)
            return false;
        RuntimeServiceHost current = s_current;
        s_current = null;
        current.Dispose();
        return true;
    }

    private static IReadOnlyDictionary<Guid, ManagedSerializedValue> ParseState(string document)
    {
        if (document is null || Encoding.UTF8.GetByteCount(document) > MaximumStateBytes)
            throw new InvalidOperationException("Managed runtime-service reload state exceeds its bounded document size.");
        using JsonDocument parsed = JsonDocument.Parse(document, new JsonDocumentOptions { MaxDepth = 33 });
        if (parsed.RootElement.ValueKind != JsonValueKind.Object)
            throw new InvalidOperationException("Managed runtime-service reload state must be a JSON object.");
        var result = new SortedDictionary<Guid, ManagedSerializedValue>();
        foreach (JsonProperty property in parsed.RootElement.EnumerateObject())
        {
            if (!Guid.TryParse(property.Name, out Guid id) || id == Guid.Empty ||
                !result.TryAdd(id, ManagedSerializedValue.ReadCanonical(property.Value)))
            {
                throw new InvalidOperationException(
                    $"Managed runtime-service reload state key '{property.Name}' is invalid or duplicated.");
            }
        }
        return result;
    }

    private static string WriteState(IReadOnlyDictionary<Guid, ManagedSerializedValue> state)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            foreach ((Guid id, ManagedSerializedValue value) in state.OrderBy(value => value.Key))
            {
                writer.WritePropertyName(id.ToString("D"));
                value.WriteCanonical(writer);
            }
            writer.WriteEndObject();
        }
        if (buffer.WrittenCount > MaximumStateBytes)
            throw new InvalidOperationException("Managed runtime-service reload state exceeds its bounded document size.");
        return Encoding.UTF8.GetString(buffer.WrittenSpan);
    }
}
