using System.Collections.ObjectModel;
using System.Reflection;

namespace Keire.Editor;

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorExtensionIdAttribute : Attribute
{
    public EditorExtensionIdAttribute(string id) => Id = Guid.Parse(id);

    public Guid Id { get; }
}

public enum EditorExtensionKind : byte
{
    PropertyDrawer,
    PropertyDecorator,
    CustomEditor,
    ScriptedImporter,
    AssetPostprocessor,
    EditorWindow,
    EditorTool,
    SettingsProvider,
    BuildProcessor
}

public sealed record EditorExtensionDiagnostic(string Code, string ExtensionType, string Message);

public sealed record EditorExtensionDescriptor(Guid Id, EditorExtensionKind Kind, Type ExtensionType,
                                                string AssemblyName);

public sealed class EditorExtensionLifetime : IDisposable
{
    private readonly CancellationTokenSource _cancellation = new();
    private bool _disposed;

    internal EditorExtensionLifetime(ulong generation)
    {
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation));
        Generation = generation;
    }

    public ulong Generation { get; }
    public bool IsValid => !_disposed;
    public CancellationToken CancellationToken => _cancellation.Token;

    public void ThrowIfInvalid()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        _cancellation.Cancel();
        _cancellation.Dispose();
    }
}

public abstract class EditorExtension : IDisposable
{
    private EditorExtensionLifetime? _lifetime;

    public EditorExtensionLifetime Lifetime => _lifetime ??
        throw new InvalidOperationException("The editor extension has not been activated.");

    protected virtual void OnEnable() { }
    protected virtual void OnDisable() { }

    internal void Activate(EditorExtensionLifetime lifetime)
    {
        ArgumentNullException.ThrowIfNull(lifetime);
        if (_lifetime is not null)
            throw new InvalidOperationException("An editor extension instance cannot be activated twice.");
        _lifetime = lifetime;
        OnEnable();
    }

    public void Dispose()
    {
        if (_lifetime is null)
            return;
        try
        {
            OnDisable();
        }
        finally
        {
            _lifetime = null;
        }
        GC.SuppressFinalize(this);
    }
}

public sealed class EditorExtensionCatalog
{
    private readonly ReadOnlyCollection<EditorExtensionDescriptor> _extensions;

    private EditorExtensionCatalog(ulong generation, IReadOnlyList<EditorExtensionDescriptor> extensions)
    {
        Generation = generation;
        _extensions = new ReadOnlyCollection<EditorExtensionDescriptor>(extensions.ToArray());
    }

    public ulong Generation { get; }
    public IReadOnlyList<EditorExtensionDescriptor> Extensions => _extensions;

    public static EditorExtensionCatalog Discover(ulong generation, IEnumerable<Type> allowedTypes)
    {
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation));
        ArgumentNullException.ThrowIfNull(allowedTypes);

        var result = new List<EditorExtensionDescriptor>();
        var identities = new Dictionary<Guid, Type>();
        foreach (Type type in allowedTypes.Where(type => type is not null).Distinct()
                     .OrderBy(type => type.Assembly.GetName().Name, StringComparer.Ordinal)
                     .ThenBy(type => type.FullName, StringComparer.Ordinal))
        {
            EditorExtensionKind? kind = ExtensionKind(type);
            if (kind is null)
                continue;
            if (!type.IsClass || type.IsAbstract || type.ContainsGenericParameters)
                throw Invalid(type, "extension types must be closed, concrete classes");
            if (type.GetConstructor(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
                                    binder: null, Type.EmptyTypes, modifiers: null) is null)
            {
                throw Invalid(type, "extension types require a parameterless constructor");
            }
            EditorExtensionIdAttribute identity = type.GetCustomAttribute<EditorExtensionIdAttribute>(false) ??
                throw Invalid(type, "extension types require EditorExtensionId");
            if (identity.Id == Guid.Empty)
                throw Invalid(type, "extension IDs cannot be empty");
            if (identities.TryGetValue(identity.Id, out Type? existing))
            {
                throw Invalid(type,
                    $"extension ID '{identity.Id:D}' is already registered by '{existing.FullName}'");
            }
            identities.Add(identity.Id, type);
            result.Add(new EditorExtensionDescriptor(identity.Id, kind.Value, type,
                type.Assembly.GetName().Name ?? type.Assembly.FullName ?? "unknown"));
        }
        result.Sort(static (left, right) => left.Id.CompareTo(right.Id));
        ValidateRegistrations(result);
        return new EditorExtensionCatalog(generation, result);
    }

    private static EditorExtensionKind? ExtensionKind(Type type)
    {
        if (typeof(PropertyDrawer).IsAssignableFrom(type))
            return EditorExtensionKind.PropertyDrawer;
        if (typeof(PropertyDecorator).IsAssignableFrom(type))
            return EditorExtensionKind.PropertyDecorator;
        if (typeof(Editor).IsAssignableFrom(type))
            return EditorExtensionKind.CustomEditor;
        if (typeof(ScriptedImporter).IsAssignableFrom(type))
            return EditorExtensionKind.ScriptedImporter;
        if (typeof(AssetPostprocessor).IsAssignableFrom(type))
            return EditorExtensionKind.AssetPostprocessor;
        if (typeof(EditorWindow).IsAssignableFrom(type))
            return EditorExtensionKind.EditorWindow;
        if (typeof(EditorTool).IsAssignableFrom(type))
            return EditorExtensionKind.EditorTool;
        if (typeof(SettingsProvider).IsAssignableFrom(type))
            return EditorExtensionKind.SettingsProvider;
        if (typeof(IBuildProcessor).IsAssignableFrom(type))
            return EditorExtensionKind.BuildProcessor;
        return null;
    }

    private static void ValidateRegistrations(IEnumerable<EditorExtensionDescriptor> descriptors)
    {
        ValidateUniqueTarget<CustomPropertyDrawerAttribute>(descriptors, EditorExtensionKind.PropertyDrawer,
            static attribute => (attribute.TargetType, attribute.UseForChildren));
        ValidateUniqueTarget<CustomPropertyDecoratorAttribute>(descriptors, EditorExtensionKind.PropertyDecorator,
            static attribute => (attribute.AttributeType, false));
        ValidateUniqueTarget<CustomEditorAttribute>(descriptors, EditorExtensionKind.CustomEditor,
            static attribute => (attribute.TargetType, attribute.EditorForChildClasses));

        var extensions = new Dictionary<string, Type>(StringComparer.OrdinalIgnoreCase);
        foreach (EditorExtensionDescriptor descriptor in descriptors.Where(value =>
                     value.Kind == EditorExtensionKind.ScriptedImporter))
        {
            ScriptedImporterAttribute registration =
                descriptor.ExtensionType.GetCustomAttribute<ScriptedImporterAttribute>(false) ??
                throw Invalid(descriptor.ExtensionType, "scripted importers require ScriptedImporter");
            foreach (string extension in registration.Extensions)
            {
                if (extensions.TryGetValue(extension, out Type? existing))
                {
                    throw Invalid(descriptor.ExtensionType,
                        $"source extension '{extension}' is already handled by '{existing.FullName}'");
                }
                extensions.Add(extension, descriptor.ExtensionType);
            }
        }
    }

    private static void ValidateUniqueTarget<TAttribute>(IEnumerable<EditorExtensionDescriptor> descriptors,
                                                          EditorExtensionKind kind,
                                                          Func<TAttribute, (Type Type, bool Derived)> target)
        where TAttribute : Attribute
    {
        var registrations = new Dictionary<(Type Type, bool Derived), Type>();
        foreach (EditorExtensionDescriptor descriptor in descriptors.Where(value => value.Kind == kind))
        {
            TAttribute registration = descriptor.ExtensionType.GetCustomAttribute<TAttribute>(false) ??
                throw Invalid(descriptor.ExtensionType, $"{kind} extensions require {typeof(TAttribute).Name}");
            (Type Type, bool Derived) key = target(registration);
            if (registrations.TryGetValue(key, out Type? existing))
            {
                throw Invalid(descriptor.ExtensionType,
                    $"the {kind} target '{key.Type.FullName}' is already registered by '{existing.FullName}'");
            }
            registrations.Add(key, descriptor.ExtensionType);
        }
    }

    private static InvalidOperationException Invalid(Type type, string reason) =>
        new($"Editor extension '{type.FullName ?? type.Name}' is invalid: {reason}.");
}
