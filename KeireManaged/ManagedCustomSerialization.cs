using System.Collections;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Keire;

public enum ManagedSerializedValueKind
{
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Number,
    String,
    List,
    Map
}

public sealed class ManagedSerializedValue
{
    private const int MaximumDepth = 32;
    private const int MaximumCollectionItems = 4_096;
    private const int MaximumStringBytes = 1024 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    private static readonly IReadOnlyList<ManagedSerializedValue> EmptyList = Array.Empty<ManagedSerializedValue>();
    private static readonly IReadOnlyDictionary<string, ManagedSerializedValue> EmptyMap =
        new SortedDictionary<string, ManagedSerializedValue>(StringComparer.Ordinal);

    private ManagedSerializedValue(ManagedSerializedValueKind kind, object? value)
    {
        Kind = kind;
        Value = value;
    }

    public ManagedSerializedValueKind Kind { get; }
    private object? Value { get; }

    public static ManagedSerializedValue Null { get; } = new(ManagedSerializedValueKind.Null, null);
    public static ManagedSerializedValue From(bool value) => new(ManagedSerializedValueKind.Boolean, value);
    public static ManagedSerializedValue From(long value) => new(ManagedSerializedValueKind.SignedInteger, value);
    public static ManagedSerializedValue From(ulong value) => new(ManagedSerializedValueKind.UnsignedInteger, value);

    public static ManagedSerializedValue From(double value)
    {
        if (!double.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Managed numeric payloads must be finite.");
        return new ManagedSerializedValue(ManagedSerializedValueKind.Number, value);
    }

    public static ManagedSerializedValue From(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        ValidateString(value, nameof(value));
        return new ManagedSerializedValue(ManagedSerializedValueKind.String, value);
    }

    public static ManagedSerializedValue FromList(IEnumerable<ManagedSerializedValue> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        ManagedSerializedValue[] result = values.ToArray();
        if (result.Length > MaximumCollectionItems)
        {
            throw new ArgumentOutOfRangeException(nameof(values),
                $"Managed lists cannot exceed {MaximumCollectionItems} items.");
        }
        if (result.Any(value => value is null))
            throw new ArgumentException("Managed lists cannot contain null value objects; use ManagedSerializedValue.Null.",
                                        nameof(values));
        var serialized = new ManagedSerializedValue(ManagedSerializedValueKind.List, result);
        serialized.Validate(0);
        return serialized;
    }

    public static ManagedSerializedValue FromMap(IEnumerable<KeyValuePair<string, ManagedSerializedValue>> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        var result = new SortedDictionary<string, ManagedSerializedValue>(StringComparer.Ordinal);
        foreach ((string key, ManagedSerializedValue value) in values)
        {
            if (string.IsNullOrWhiteSpace(key))
                throw new ArgumentException("Managed map keys must contain visible text.", nameof(values));
            ValidateString(key, nameof(values));
            if (value is null)
                throw new ArgumentException("Managed maps cannot contain null value objects.", nameof(values));
            if (!result.TryAdd(key, value))
                throw new ArgumentException($"Managed map key '{key}' is duplicated.", nameof(values));
            if (result.Count > MaximumCollectionItems)
            {
                throw new ArgumentOutOfRangeException(nameof(values),
                    $"Managed maps cannot exceed {MaximumCollectionItems} items.");
            }
        }
        var serialized = new ManagedSerializedValue(ManagedSerializedValueKind.Map, result);
        serialized.Validate(0);
        return serialized;
    }

    public bool AsBoolean() => Kind == ManagedSerializedValueKind.Boolean
        ? (bool)Value!
        : throw KindMismatch(ManagedSerializedValueKind.Boolean);

    public long AsInt64()
    {
        if (Kind == ManagedSerializedValueKind.SignedInteger)
            return (long)Value!;
        if (Kind == ManagedSerializedValueKind.UnsignedInteger && (ulong)Value! <= long.MaxValue)
            return (long)(ulong)Value!;
        throw KindMismatch(ManagedSerializedValueKind.SignedInteger);
    }

    public ulong AsUInt64()
    {
        if (Kind == ManagedSerializedValueKind.UnsignedInteger)
            return (ulong)Value!;
        if (Kind == ManagedSerializedValueKind.SignedInteger && (long)Value! >= 0)
            return (ulong)(long)Value!;
        throw KindMismatch(ManagedSerializedValueKind.UnsignedInteger);
    }

    public double AsNumber() => Kind switch
    {
        ManagedSerializedValueKind.Number => (double)Value!,
        ManagedSerializedValueKind.SignedInteger => (long)Value!,
        ManagedSerializedValueKind.UnsignedInteger => (ulong)Value!,
        _ => throw KindMismatch(ManagedSerializedValueKind.Number)
    };

    public string AsString() => Kind == ManagedSerializedValueKind.String
        ? (string)Value!
        : throw KindMismatch(ManagedSerializedValueKind.String);

    public IReadOnlyList<ManagedSerializedValue> AsList() => Kind == ManagedSerializedValueKind.List
        ? (IReadOnlyList<ManagedSerializedValue>)Value!
        : throw KindMismatch(ManagedSerializedValueKind.List);

    public IReadOnlyDictionary<string, ManagedSerializedValue> AsMap() => Kind == ManagedSerializedValueKind.Map
        ? (IReadOnlyDictionary<string, ManagedSerializedValue>)Value!
        : throw KindMismatch(ManagedSerializedValueKind.Map);

    internal void WriteCanonical(Utf8JsonWriter writer)
    {
        switch (Kind)
        {
            case ManagedSerializedValueKind.Null:
                writer.WriteNullValue();
                break;
            case ManagedSerializedValueKind.Boolean:
                writer.WriteBooleanValue((bool)Value!);
                break;
            case ManagedSerializedValueKind.SignedInteger:
                writer.WriteNumberValue((long)Value!);
                break;
            case ManagedSerializedValueKind.UnsignedInteger:
                writer.WriteNumberValue((ulong)Value!);
                break;
            case ManagedSerializedValueKind.Number:
                writer.WriteNumberValue((double)Value!);
                break;
            case ManagedSerializedValueKind.String:
                writer.WriteStringValue((string)Value!);
                break;
            case ManagedSerializedValueKind.List:
                writer.WriteStartArray();
                foreach (ManagedSerializedValue item in AsList())
                    item.WriteCanonical(writer);
                writer.WriteEndArray();
                break;
            case ManagedSerializedValueKind.Map:
                writer.WriteStartObject();
                foreach ((string key, ManagedSerializedValue item) in AsMap())
                {
                    writer.WritePropertyName(key);
                    item.WriteCanonical(writer);
                }
                writer.WriteEndObject();
                break;
            default:
                throw new InvalidOperationException($"Unsupported managed payload kind '{Kind}'.");
        }
    }

    internal static ManagedSerializedValue ReadCanonical(JsonElement element, int depth = 0)
    {
        if (depth > MaximumDepth)
            throw new JsonException($"Managed payloads cannot exceed {MaximumDepth} nested levels.");
        return element.ValueKind switch
        {
            JsonValueKind.Null => Null,
            JsonValueKind.True => From(true),
            JsonValueKind.False => From(false),
            JsonValueKind.String => From(element.GetString() ?? string.Empty),
            JsonValueKind.Number => ReadNumber(element),
            JsonValueKind.Array => ReadList(element, depth),
            JsonValueKind.Object => ReadMap(element, depth),
            _ => throw new JsonException($"JSON kind '{element.ValueKind}' is not a managed payload value.")
        };
    }

    private static ManagedSerializedValue ReadNumber(JsonElement element)
    {
        if (element.TryGetInt64(out long signed))
            return From(signed);
        if (element.TryGetUInt64(out ulong unsigned))
            return From(unsigned);
        return From(element.GetDouble());
    }

    private static ManagedSerializedValue ReadList(JsonElement element, int depth)
    {
        if (element.GetArrayLength() > MaximumCollectionItems)
            throw new JsonException($"Managed lists cannot exceed {MaximumCollectionItems} items.");
        return FromList(element.EnumerateArray().Select(item => ReadCanonical(item, depth + 1)));
    }

    private static ManagedSerializedValue ReadMap(JsonElement element, int depth)
    {
        var result = new List<KeyValuePair<string, ManagedSerializedValue>>();
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonProperty property in element.EnumerateObject())
        {
            if (!names.Add(property.Name))
                throw new JsonException($"Managed map key '{property.Name}' is duplicated.");
            result.Add(new KeyValuePair<string, ManagedSerializedValue>(
                property.Name, ReadCanonical(property.Value, depth + 1)));
            if (result.Count > MaximumCollectionItems)
                throw new JsonException($"Managed maps cannot exceed {MaximumCollectionItems} items.");
        }
        return FromMap(result);
    }

    private void Validate(int depth)
    {
        if (depth > MaximumDepth)
            throw new ArgumentOutOfRangeException(nameof(depth),
                $"Managed payloads cannot exceed {MaximumDepth} nested levels.");
        if (Kind == ManagedSerializedValueKind.List)
        {
            foreach (ManagedSerializedValue item in AsList())
                item.Validate(depth + 1);
        }
        else if (Kind == ManagedSerializedValueKind.Map)
        {
            foreach ((string key, ManagedSerializedValue item) in AsMap())
            {
                ValidateString(key, nameof(key));
                item.Validate(depth + 1);
            }
        }
    }

    private static void ValidateString(string value, string parameter)
    {
        int bytes;
        try
        {
            bytes = StrictUtf8.GetByteCount(value);
        }
        catch (EncoderFallbackException exception)
        {
            throw new ArgumentException("Managed strings must contain valid Unicode text.", parameter, exception);
        }
        if (bytes > MaximumStringBytes)
        {
            throw new ArgumentOutOfRangeException(parameter,
                $"Managed strings cannot exceed {MaximumStringBytes} UTF-8 bytes.");
        }
    }

    private InvalidOperationException KindMismatch(ManagedSerializedValueKind expected) =>
        new($"Managed payload kind '{Kind}' cannot be read as '{expected}'.");
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomManagedValueConverterAttribute : Attribute
{
    public CustomManagedValueConverterAttribute(Type targetType, string stableId, uint version)
    {
        TargetType = targetType ?? throw new ArgumentNullException(nameof(targetType));
        if (!Guid.TryParse(stableId, out Guid parsed) || parsed == Guid.Empty)
            throw new ArgumentException("Custom converter stable IDs must be non-empty GUIDs.", nameof(stableId));
        if (version == 0)
            throw new ArgumentOutOfRangeException(nameof(version), "Custom converter versions begin at one.");
        StableId = parsed;
        Version = version;
    }

    public Type TargetType { get; }
    public Guid StableId { get; }
    public uint Version { get; }
}

public sealed record ManagedSerializationContext(string Path, string Phase);

public abstract class ManagedValueConverter
{
    internal abstract ManagedSerializedValue WriteObject(object? value, ManagedSerializationContext context);
    internal abstract object? ReadObject(ManagedSerializedValue value, ManagedSerializationContext context);
}

public abstract class ManagedValueConverter<T> : ManagedValueConverter
{
    public abstract ManagedSerializedValue Write(T value, ManagedSerializationContext context);
    public abstract T Read(ManagedSerializedValue value, ManagedSerializationContext context);

    internal sealed override ManagedSerializedValue WriteObject(object? value, ManagedSerializationContext context) =>
        Write((T)value!, context) ?? throw new InvalidOperationException(
            $"Managed converter '{GetType().FullName}' returned a null payload.");

    internal sealed override object? ReadObject(ManagedSerializedValue value, ManagedSerializationContext context) =>
        Read(value, context);
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ManagedValueMigrationAttribute : Attribute
{
    public ManagedValueMigrationAttribute(string stableId, uint fromVersion, uint toVersion)
    {
        if (!Guid.TryParse(stableId, out Guid parsed) || parsed == Guid.Empty)
            throw new ArgumentException("Managed migration stable IDs must be non-empty GUIDs.", nameof(stableId));
        if (fromVersion == 0 || toVersion != fromVersion + 1)
        {
            throw new ArgumentOutOfRangeException(nameof(toVersion),
                "Managed migrations must describe one contiguous version step beginning at version one.");
        }
        StableId = parsed;
        FromVersion = fromVersion;
        ToVersion = toVersion;
    }

    public Guid StableId { get; }
    public uint FromVersion { get; }
    public uint ToVersion { get; }
}

public sealed record ManagedMigrationContext(Guid StableId, uint FromVersion, uint ToVersion, string Path);

public interface IManagedValueMigration
{
    ManagedSerializedValue Migrate(ManagedSerializedValue value, ManagedMigrationContext context);
}

public interface ISerializationCallbackReceiver
{
    void OnBeforeSerialize();
    void OnAfterDeserialize();
}

internal static class ManagedCustomValueRegistry
{
    internal sealed record ConverterRegistration(Type TargetType, Guid StableId, uint Version,
                                                  ManagedValueConverter Converter, string AssemblyIdentity);
    private sealed record MigrationRegistration(uint ToVersion, IManagedValueMigration Migration,
                                                string AssemblyIdentity);
    private sealed class Registry(IReadOnlyDictionary<Type, ConverterRegistration> byType,
                                  IReadOnlyDictionary<Guid, ConverterRegistration> byId,
                                  IReadOnlyDictionary<(Guid Id, uint From), MigrationRegistration> migrations)
    {
        public IReadOnlyDictionary<Type, ConverterRegistration> ByType { get; } = byType;
        public IReadOnlyDictionary<Guid, ConverterRegistration> ById { get; } = byId;
        public IReadOnlyDictionary<(Guid Id, uint From), MigrationRegistration> Migrations { get; } = migrations;
    }

    private static readonly ConditionalWeakTable<AssemblyLoadContext, Registry> Registries = new();
    private static readonly object RegistryLock = new();

    internal static bool TryResolve(Type targetType, out ConverterRegistration registration)
    {
        Registry registry = GetRegistry(targetType);
        return registry.ByType.TryGetValue(targetType, out registration!);
    }

    internal static ManagedSerializedValue Write(object? value, Type targetType, string path)
    {
        if (!TryResolve(targetType, out ConverterRegistration? registration))
            throw new InvalidOperationException($"No custom managed converter is registered for '{targetType.FullName}'.");
        try
        {
            return registration.Converter.WriteObject(value, new ManagedSerializationContext(path, "serialize"));
        }
        catch (Exception exception) when (exception is not ManagedSerializationException)
        {
            throw CustomFailure(registration, path, "converter write", exception);
        }
    }

    internal static object? Read(Guid stableId, uint version, ManagedSerializedValue value, Type targetType,
                                 string path)
    {
        Registry registry = GetRegistry(targetType);
        if (!registry.ById.TryGetValue(stableId, out ConverterRegistration? registration) ||
            registration.TargetType != targetType)
        {
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0005", path, targetType, null,
                $"custom codec '{stableId:D}' is unavailable for the exact target type", phase: "converter",
                serializedTypeId: stableId.ToString("D"));
        }
        if (version == 0 || version > registration.Version)
        {
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0005", path, targetType, null,
                $"custom codec '{stableId:D}' version {version} cannot be read by version {registration.Version}",
                phase: "converter", serializedTypeId: stableId.ToString("D"));
        }

        ManagedSerializedValue migrated = value;
        for (uint current = version; current < registration.Version; ++current)
        {
            if (!registry.Migrations.TryGetValue((stableId, current), out MigrationRegistration? migration) ||
                migration.ToVersion != current + 1)
            {
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0006", path, targetType, null,
                    $"custom codec '{stableId:D}' has no contiguous migration from version {current} to {current + 1}",
                    phase: "migration", serializedTypeId: stableId.ToString("D"));
            }
            try
            {
                migrated = migration.Migration.Migrate(
                    migrated, new ManagedMigrationContext(stableId, current, current + 1, path)) ??
                    throw new InvalidOperationException("The migration returned a null payload.");
            }
            catch (Exception exception) when (exception is not ManagedSerializationException)
            {
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0006", path, targetType, null,
                    $"custom codec '{stableId:D}' migration {current}->{current + 1} from " +
                    $"'{migration.AssemblyIdentity}' failed", exception, phase: "migration",
                    serializedTypeId: stableId.ToString("D"));
            }
        }

        try
        {
            return registration.Converter.ReadObject(migrated, new ManagedSerializationContext(path, "deserialize"));
        }
        catch (Exception exception) when (exception is not ManagedSerializationException)
        {
            throw CustomFailure(registration, path, "converter read", exception);
        }
    }

    internal static void WriteRecord(Utf8JsonWriter writer, object? value, Type targetType, string path)
    {
        if (!TryResolve(targetType, out ConverterRegistration? registration))
            throw new JsonException($"No custom managed converter is registered for '{targetType.FullName}'.");
        ManagedSerializedValue payload = Write(value, targetType, path);
        writer.WriteStartObject();
        writer.WriteString("$custom", registration.StableId);
        writer.WriteNumber("version", registration.Version);
        writer.WritePropertyName("payload");
        payload.WriteCanonical(writer);
        writer.WriteEndObject();
    }

    internal static object? ReadRecord(JsonElement element, Type targetType, string path)
    {
        if (element.ValueKind != JsonValueKind.Object ||
            !element.TryGetProperty("$custom", out JsonElement stableElement) ||
            !Guid.TryParse(stableElement.GetString(), out Guid stableId) || stableId == Guid.Empty ||
            !element.TryGetProperty("version", out JsonElement versionElement) ||
            !versionElement.TryGetUInt32(out uint version) ||
            !element.TryGetProperty("payload", out JsonElement payloadElement))
        {
            throw new JsonException($"Custom managed value '{path}' has an invalid v4 record.");
        }
        ManagedSerializedValue payload = ManagedSerializedValue.ReadCanonical(payloadElement);
        return Read(stableId, version, payload, targetType, path);
    }

    internal static IReadOnlyDictionary<Type, ConverterRegistration> Install(IEnumerable<Type> candidates,
                                                                              Type contextType, string path)
    {
        AssemblyLoadContext loadContext = AssemblyLoadContext.GetLoadContext(contextType.Assembly) ??
                                          AssemblyLoadContext.Default;
        Registry replacement = Create(candidates.Distinct(), path);
        lock (RegistryLock)
        {
            Registries.Remove(loadContext);
            Registries.Add(loadContext, replacement);
        }
        return replacement.ByType;
    }

    internal static IReadOnlyDictionary<Type, ConverterRegistration> InstallForTests(IEnumerable<Type> candidates,
                                                                                      Type contextType) =>
        Install(candidates, contextType, contextType.FullName ?? contextType.Name);

    private static Registry GetRegistry(Type contextType)
    {
        AssemblyLoadContext loadContext = AssemblyLoadContext.GetLoadContext(contextType.Assembly) ??
                                          AssemblyLoadContext.Default;
        lock (RegistryLock)
        {
            return Registries.GetValue(loadContext, context => Create(
                context.Assemblies.Where(assembly => !assembly.IsDynamic).SelectMany(SafeTypes),
                contextType.FullName ?? contextType.Name));
        }
    }

    private static Registry Create(IEnumerable<Type> candidates, string path)
    {
        var byType = new Dictionary<Type, ConverterRegistration>();
        var byId = new Dictionary<Guid, ConverterRegistration>();
        var migrations = new Dictionary<(Guid Id, uint From), MigrationRegistration>();
        foreach (Type type in candidates.OrderBy(type => type.Assembly.FullName, StringComparer.Ordinal)
                     .ThenBy(type => type.FullName, StringComparer.Ordinal))
        {
            CustomManagedValueConverterAttribute? converterAttribute =
                type.GetCustomAttribute<CustomManagedValueConverterAttribute>(false);
            ManagedValueMigrationAttribute? migrationAttribute =
                type.GetCustomAttribute<ManagedValueMigrationAttribute>(false);
            if (converterAttribute is not null)
            {
                if (type.IsAbstract || type.ContainsGenericParameters ||
                    !typeof(ManagedValueConverter).IsAssignableFrom(type))
                {
                    throw RegistryFailure(path, type, "converter types must be closed concrete ManagedValueConverter classes");
                }
                Type expectedBase = typeof(ManagedValueConverter<>).MakeGenericType(converterAttribute.TargetType);
                if (!expectedBase.IsAssignableFrom(type))
                {
                    throw RegistryFailure(path, type,
                        $"converter target '{converterAttribute.TargetType.FullName}' does not match its generic base");
                }
                var converter = (ManagedValueConverter?)Activator.CreateInstance(type) ??
                    throw RegistryFailure(path, type, "converter types require a public parameterless constructor");
                string identity = type.Assembly.FullName ?? type.Assembly.GetName().Name ?? type.Name;
                var registration = new ConverterRegistration(converterAttribute.TargetType,
                    converterAttribute.StableId, converterAttribute.Version, converter, identity);
                if (!byType.TryAdd(registration.TargetType, registration))
                {
                    throw RegistryFailure(path, type,
                        $"target type '{registration.TargetType.FullName}' already uses converter " +
                        $"'{byType[registration.TargetType].Converter.GetType().FullName}'");
                }
                if (!byId.TryAdd(registration.StableId, registration))
                {
                    throw RegistryFailure(path, type,
                        $"stable codec ID '{registration.StableId:D}' already targets " +
                        $"'{byId[registration.StableId].TargetType.FullName}'");
                }
            }
            if (migrationAttribute is not null)
            {
                if (type.IsAbstract || type.ContainsGenericParameters ||
                    !typeof(IManagedValueMigration).IsAssignableFrom(type))
                {
                    throw RegistryFailure(path, type,
                        "migration types must be closed concrete IManagedValueMigration classes");
                }
                var migration = (IManagedValueMigration?)Activator.CreateInstance(type) ??
                    throw RegistryFailure(path, type, "migration types require a public parameterless constructor");
                string identity = type.Assembly.FullName ?? type.Assembly.GetName().Name ?? type.Name;
                var registration = new MigrationRegistration(migrationAttribute.ToVersion, migration, identity);
                if (!migrations.TryAdd((migrationAttribute.StableId, migrationAttribute.FromVersion), registration))
                {
                    throw RegistryFailure(path, type,
                        $"migration edge '{migrationAttribute.StableId:D}' " +
                        $"{migrationAttribute.FromVersion}->{migrationAttribute.ToVersion} is duplicated");
                }
            }
        }
        foreach (((Guid id, uint from), MigrationRegistration migration) in migrations)
        {
            if (!byId.TryGetValue(id, out ConverterRegistration? converter))
                throw RegistryFailure(path, migration.Migration.GetType(), $"migration codec '{id:D}' is not registered");
            if (migration.ToVersion > converter.Version)
            {
                throw RegistryFailure(path, migration.Migration.GetType(),
                    $"migration {from}->{migration.ToVersion} exceeds converter version {converter.Version}");
            }
        }
        return new Registry(byType, byId, migrations);
    }

    private static Type[] SafeTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException exception)
        {
            return exception.Types.Where(type => type is not null).Cast<Type>().ToArray();
        }
    }

    private static ManagedSerializationException RegistryFailure(string path, Type type, string reason) =>
        new("KEIRE-MANAGED-SERIALIZATION-0005", path, type, type, reason, phase: "catalog");

    private static ManagedSerializationException CustomFailure(ConverterRegistration registration, string path,
                                                               string operation, Exception exception) =>
        new("KEIRE-MANAGED-SERIALIZATION-0005", path, registration.TargetType, registration.TargetType,
            $"custom codec '{registration.StableId:D}' version {registration.Version} {operation} from " +
            $"'{registration.AssemblyIdentity}' failed", exception, phase: "converter",
            serializedTypeId: registration.StableId.ToString("D"));
}

internal sealed class ManagedCustomValueJsonConverterFactory : JsonConverterFactory
{
    public override bool CanConvert(Type typeToConvert) => ManagedCustomValueRegistry.TryResolve(typeToConvert, out _);

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) =>
        (JsonConverter)Activator.CreateInstance(typeof(ManagedCustomValueJsonConverter<>).MakeGenericType(typeToConvert))!;
}

internal sealed class ManagedCustomValueJsonConverter<T> : JsonConverter<T>
{
    public override T? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        using JsonDocument document = JsonDocument.ParseValue(ref reader);
        return (T?)ManagedCustomValueRegistry.ReadRecord(
            document.RootElement, typeToConvert, typeToConvert.FullName ?? typeToConvert.Name);
    }

    public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options) =>
        ManagedCustomValueRegistry.WriteRecord(
            writer, value, typeof(T), typeof(T).FullName ?? typeof(T).Name);
}

internal static class ManagedSerializationCallbacks
{
    internal static void InvokeBeforeSerialize(object root) => Invoke(root, true);
    internal static void InvokeAfterDeserialize(object root) => Invoke(root, false);

    internal static bool ContainsCallbacks(object? root)
    {
        var visited = new HashSet<object>(ReferenceEqualityComparer.Instance);
        return Contains(root, visited, 0);
    }

    private static void Invoke(object root, bool before)
    {
        var visited = new HashSet<object>(ReferenceEqualityComparer.Instance);
        Visit(root, root.GetType().FullName ?? root.GetType().Name, before, visited, 0);
    }

    private static void Visit(object? value, string path, bool before, HashSet<object> visited, int depth)
    {
        if (value is null || depth > 32)
            return;
        Type type = value.GetType();
        if (typeof(EngineObject).IsAssignableFrom(type) && value is not Behaviour && value is not ScriptableObject)
            return;
        if (IsAtomic(type) || ManagedCustomValueRegistry.TryResolve(type, out _))
            return;
        if (!type.IsValueType && !visited.Add(value))
            return;
        if (value is ISerializationCallbackReceiver receiver)
        {
            if (type.IsValueType)
                throw new InvalidOperationException($"Serialization callback receiver '{type.FullName}' must be a class.");
            try
            {
                if (before)
                    receiver.OnBeforeSerialize();
                else
                    receiver.OnAfterDeserialize();
            }
            catch (Exception exception)
            {
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0007", path, type, type,
                    $"serialization callback phase '{(before ? "beforeSerialize" : "afterDeserialize")}' failed",
                    exception, phase: before ? "beforeSerialize" : "afterDeserialize");
            }
        }
        if (value is Array array)
        {
            for (int index = 0; index < array.Length; ++index)
                Visit(array.GetValue(index), $"{path}[{index}]", before, visited, depth + 1);
            return;
        }
        if (value is IList list)
        {
            for (int index = 0; index < list.Count; ++index)
                Visit(list[index], $"{path}[{index}]", before, visited, depth + 1);
            return;
        }
        if (value is IDictionary dictionary)
        {
            var entries = new List<DictionaryEntry>(dictionary.Count);
            IDictionaryEnumerator enumerator = dictionary.GetEnumerator();
            while (enumerator.MoveNext())
                entries.Add(enumerator.Entry);
            foreach (DictionaryEntry entry in entries.OrderBy(entry => Convert.ToString(entry.Key,
                         System.Globalization.CultureInfo.InvariantCulture), StringComparer.Ordinal))
            {
                string key = Convert.ToString(entry.Key, System.Globalization.CultureInfo.InvariantCulture) ?? "?";
                Visit(entry.Value, $"{path}[{key}]", before, visited, depth + 1);
            }
            return;
        }
        foreach (FieldInfo field in SerializableFields(type))
            Visit(field.GetValue(value), $"{path}.{field.Name}", before, visited, depth + 1);
    }

    private static bool Contains(object? value, HashSet<object> visited, int depth)
    {
        if (value is null || depth > 32)
            return false;
        Type type = value.GetType();
        if (value is ISerializationCallbackReceiver)
            return true;
        if (typeof(EngineObject).IsAssignableFrom(type) || IsAtomic(type) ||
            ManagedCustomValueRegistry.TryResolve(type, out _))
        {
            return false;
        }
        if (!type.IsValueType && !visited.Add(value))
            return false;
        if (value is Array array)
            return array.Cast<object?>().Any(item => Contains(item, visited, depth + 1));
        if (value is IList list)
        {
            foreach (object? item in list)
            {
                if (Contains(item, visited, depth + 1))
                    return true;
            }
            return false;
        }
        if (value is IDictionary dictionary)
        {
            IDictionaryEnumerator enumerator = dictionary.GetEnumerator();
            while (enumerator.MoveNext())
            {
                if (Contains(enumerator.Value, visited, depth + 1))
                    return true;
            }
            return false;
        }
        return SerializableFields(type).Any(field => Contains(field.GetValue(value), visited, depth + 1));
    }

    private static IEnumerable<FieldInfo> SerializableFields(Type type)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type; current is not null && current != typeof(object); current = current.BaseType)
            hierarchy.Push(current);
        while (hierarchy.TryPop(out Type? current))
        {
            foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                           BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                         .OrderBy(field => field.MetadataToken))
            {
                if (!field.IsStatic && !field.IsInitOnly && !field.IsDefined(typeof(NonSerializedAttribute), false) &&
                    (field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
                     field.IsDefined(typeof(SerializeReferenceAttribute), true)))
                {
                    yield return field;
                }
            }
        }
    }

    private static bool IsAtomic(Type type) =>
        type.IsPrimitive || type.IsEnum || type == typeof(string) || type == typeof(decimal) ||
        type == typeof(Guid) || type == typeof(Vector2) || type == typeof(Vector3) ||
        type == typeof(Vector4) || type == typeof(Quaternion) || type == typeof(Color);
}
