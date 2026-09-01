using System.Collections.ObjectModel;

namespace Keire.Editor;

public sealed class EditorExtensionGeneration : IDisposable
{
    private sealed record Active(EditorExtensionDescriptor Descriptor, object Instance,
                                 EditorExtensionLifetime? Lifetime);
    private readonly List<Active> _active;
    private readonly Dictionary<Guid, Active> _byId;
    private readonly HashSet<Guid> _quarantined = [];
    private readonly List<EditorExtensionDiagnostic> _diagnostics = [];
    private bool _disposed;

    private EditorExtensionGeneration(EditorExtensionCatalog catalog, List<Active> active)
    {
        Catalog = catalog;
        _active = active;
        _byId = active.ToDictionary(value => value.Descriptor.Id);
    }

    public EditorExtensionCatalog Catalog { get; }
    public ulong Generation => Catalog.Generation;
    public IReadOnlyList<EditorExtensionDiagnostic> Diagnostics => _diagnostics;
    public IReadOnlyCollection<Guid> QuarantinedExtensions => _quarantined;

    public static EditorExtensionGeneration Stage(ulong generation, IEnumerable<Type> exactAllowedTypes)
    {
        EditorExtensionCatalog catalog = EditorExtensionCatalog.Discover(generation, exactAllowedTypes);
        var active = new List<Active>();
        try
        {
            foreach (EditorExtensionDescriptor descriptor in catalog.Extensions)
            {
                object instance = Activator.CreateInstance(descriptor.ExtensionType, nonPublic: true) ??
                    throw new InvalidOperationException(
                        $"Editor extension '{descriptor.ExtensionType.FullName}' could not be constructed.");
                EditorExtensionLifetime? lifetime = null;
                if (instance is EditorExtension extension)
                {
                    lifetime = new EditorExtensionLifetime(generation);
                    try
                    {
                        extension.Activate(lifetime);
                        if (extension is EditorWindow window)
                            window.CreateGUI();
                    }
                    catch
                    {
                        lifetime.Dispose();
                        throw;
                    }
                }
                active.Add(new Active(descriptor, instance, lifetime));
            }
            return new EditorExtensionGeneration(catalog, active);
        }
        catch
        {
            DisposeReverse(active);
            throw;
        }
    }

    public T? Get<T>(Guid extensionId) where T : class
    {
        ThrowIfInvalid();
        if (_quarantined.Contains(extensionId))
            return null;
        return _byId.TryGetValue(extensionId, out Active? active) ? active.Instance as T : null;
    }

    public bool Invoke(Guid extensionId, string operation, Action<object> callback,
                       string? stablePropertyPath = null)
    {
        ThrowIfInvalid();
        ArgumentException.ThrowIfNullOrWhiteSpace(operation);
        ArgumentNullException.ThrowIfNull(callback);
        if (_quarantined.Contains(extensionId) || !_byId.TryGetValue(extensionId, out Active? active))
            return false;
        try
        {
            active.Lifetime?.ThrowIfInvalid();
            callback(active.Instance);
            return true;
        }
        catch (Exception exception)
        {
            _quarantined.Add(extensionId);
            Exception? cleanupFailure = DisposeActive(active);
            string path = string.IsNullOrWhiteSpace(stablePropertyPath)
                ? string.Empty
                : $" Property: {stablePropertyPath}.";
            string cleanup = cleanupFailure is null ? string.Empty : $" Cleanup also failed: {cleanupFailure.Message}";
            _diagnostics.Add(new EditorExtensionDiagnostic(
                "KEIRE-EDITOR-EXTENSION-0002", active.Descriptor.ExtensionType.FullName ??
                active.Descriptor.ExtensionType.Name,
                $"Extension operation '{operation}' was quarantined for generation {Generation}.{path} " +
                exception.Message + cleanup));
            return false;
        }
    }

    internal void ReportDiagnostic(string code, string owner, string message)
    {
        ThrowIfInvalid();
        _diagnostics.Add(new EditorExtensionDiagnostic(code, owner, message));
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        DisposeReverse(_active, _diagnostics);
        _active.Clear();
        _byId.Clear();
        GC.SuppressFinalize(this);
    }

    private static void DisposeReverse(IReadOnlyList<Active> active,
                                       List<EditorExtensionDiagnostic>? diagnostics = null)
    {
        for (int index = active.Count - 1; index >= 0; --index)
        {
            Exception? failure = DisposeActive(active[index]);
            if (failure is not null && diagnostics is not null)
            {
                diagnostics.Add(new EditorExtensionDiagnostic(
                    "KEIRE-EDITOR-EXTENSION-0003",
                    active[index].Descriptor.ExtensionType.FullName ?? active[index].Descriptor.ExtensionType.Name,
                    $"Extension cleanup failed for generation {active[index].Lifetime?.Generation ?? 0}: " +
                    failure.Message));
            }
        }
    }

    private static Exception? DisposeActive(Active active)
    {
        Exception? failure = null;
        try
        {
            if (active.Instance is IDisposable disposable)
                disposable.Dispose();
        }
        catch (Exception exception)
        {
            failure = exception;
        }
        finally
        {
            try
            {
                active.Lifetime?.Dispose();
            }
            catch (Exception exception)
            {
                failure ??= exception;
            }
        }
        return failure;
    }

    private void ThrowIfInvalid() => ObjectDisposedException.ThrowIf(_disposed, this);
}

public sealed class EditorExtensionPlatform : IDisposable
{
    private readonly List<EditorExtensionDiagnostic> _diagnostics = [];
    private EditorExtensionGeneration? _current;
    private int _consecutiveFailures;

    public EditorExtensionGeneration? Current => _current;
    public bool SafeModeRecommended => _consecutiveFailures >= 3;
    public IReadOnlyList<EditorExtensionDiagnostic> Diagnostics => _diagnostics;

    public bool PublishCandidate(ulong generation, IEnumerable<Type> exactAllowedTypes,
                                 bool disableProjectExtensions = false)
    {
        if (disableProjectExtensions)
        {
            EditorExtensionGeneration empty = EditorExtensionGeneration.Stage(generation, []);
            Swap(empty);
            return true;
        }
        try
        {
            EditorExtensionGeneration candidate = EditorExtensionGeneration.Stage(generation, exactAllowedTypes);
            Swap(candidate);
            return true;
        }
        catch (Exception exception)
        {
            ++_consecutiveFailures;
            _diagnostics.Add(new EditorExtensionDiagnostic(
                "KEIRE-EDITOR-EXTENSION-0001", "catalog",
                $"Editor extension generation {generation} was rejected; the last-good generation remains active. " +
                exception.Message));
            return false;
        }
    }

    public void Dispose()
    {
        _current?.Dispose();
        _current = null;
        GC.SuppressFinalize(this);
    }

    private void Swap(EditorExtensionGeneration candidate)
    {
        EditorExtensionGeneration? previous = _current;
        _current = candidate;
        _consecutiveFailures = 0;
        previous?.Dispose();
    }
}
