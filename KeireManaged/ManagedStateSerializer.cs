using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;

namespace Keire;

internal static class ManagedStateSerializer
{
    private const int MaximumDocumentBytes = 16 * 1024 * 1024;
    private const int MaximumFieldsPerType = 1_024;

    private sealed class StateDocument
    {
        public int Version { get; set; } = 3;
        public List<StateField> Fields { get; set; } = [];
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public JsonElement? ReferenceGraph { get; set; }
    }

    private sealed class StateField
    {
        public string StableId { get; set; } = string.Empty;
        public string Name { get; set; } = string.Empty;
        public string Type { get; set; } = string.Empty;
        public string[] Aliases { get; set; } = [];
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public bool ReferenceGraph { get; set; }
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public string? ReferenceGraphRoot { get; set; }
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public JsonElement Value { get; set; }
    }

    [ThreadStatic]
    private static ulong s_restoreWorld;

    private sealed class EntityJsonConverter : JsonConverter<Entity>
    {
        public override Entity? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            using JsonDocument document = JsonDocument.ParseValue(ref reader);
            JsonElement root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Null)
                return null;
            JsonElement id = TryProperty(root, "entity", "Entity", out JsonElement entity)
                ? entity
                : TryProperty(root, "Id", "id", out JsonElement nested) ? nested : root;
            ulong high = ReadUInt64(id, "High", "high");
            ulong low = ReadUInt64(id, "Low", "low");
            return Entity.FromId(s_restoreWorld, new EntityId(high, low));
        }

        public override void Write(Utf8JsonWriter writer, Entity value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("$ref", "entity");
            writer.WritePropertyName("entity");
            writer.WriteStartObject();
            writer.WriteNumber("High", value.Id.High);
            writer.WriteNumber("Low", value.Id.Low);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        private static bool TryProperty(JsonElement value, string primary, string fallback, out JsonElement result) =>
            value.TryGetProperty(primary, out result) || value.TryGetProperty(fallback, out result);

        private static ulong ReadUInt64(JsonElement value, string primary, string fallback) =>
            TryProperty(value, primary, fallback, out JsonElement result) && result.ValueKind == JsonValueKind.Number
                ? result.GetUInt64()
                : 0;
    }

    private sealed class ComponentJsonConverterFactory : JsonConverterFactory
    {
        public override bool CanConvert(Type typeToConvert) => typeof(Component).IsAssignableFrom(typeToConvert) ||
            typeToConvert.IsInterface && ComponentType.AssignableTypes(typeToConvert).Any();
        public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) =>
            (JsonConverter)Activator.CreateInstance(typeof(ComponentJsonConverter<>).MakeGenericType(typeToConvert))!;
    }

    private sealed class ComponentJsonConverter<T> : JsonConverter<T> where T : class
    {
        public override T? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType == JsonTokenType.Null)
                return null;
            using JsonDocument document = JsonDocument.ParseValue(ref reader);
            if (!document.RootElement.TryGetProperty("entity", out JsonElement entityElement))
                return null;
            Entity? entity = JsonSerializer.Deserialize<Entity>(entityElement, options);
            if (entity is null)
                return null;
            if (document.RootElement.TryGetProperty("component", out JsonElement componentElement) &&
                componentElement.ValueKind == JsonValueKind.Object)
            {
                ulong high = componentElement.TryGetProperty("High", out JsonElement oldHigh)
                    ? oldHigh.GetUInt64()
                    : componentElement.TryGetProperty("high", out JsonElement highValue) ? highValue.GetUInt64() : 0;
                ulong low = componentElement.TryGetProperty("Low", out JsonElement oldLow)
                    ? oldLow.GetUInt64()
                    : componentElement.TryGetProperty("low", out JsonElement lowValue) ? lowValue.GetUInt64() : 0;
                Type? concrete = ComponentType.FromId(new ComponentTypeId(high, low), typeToConvert);
                if (concrete is not null)
                    return entity.GetComponent(concrete) as T;
            }
            return entity.GetComponent(typeToConvert) as T;
        }

        public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options)
        {
            if (value is not Component component)
                throw new JsonException($"'{typeof(T).FullName}' does not contain a component reference.");
            writer.WriteStartObject();
            writer.WriteString("$ref", "component");
            writer.WritePropertyName("entity");
            JsonSerializer.Serialize(writer, component.Entity, options);
            writer.WritePropertyName("component");
            writer.WriteStartObject();
            writer.WriteNumber("High", component.Type.High);
            writer.WriteNumber("Low", component.Type.Low);
            writer.WriteEndObject();
            writer.WriteString("type", component.GetType().AssemblyQualifiedName);
            writer.WriteEndObject();
        }
    }

    private sealed class AssetJsonConverterFactory : JsonConverterFactory
    {
        public override bool CanConvert(Type typeToConvert) => typeof(Asset).IsAssignableFrom(typeToConvert);
        public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options) =>
            (JsonConverter)Activator.CreateInstance(typeof(AssetJsonConverter<>).MakeGenericType(typeToConvert))!;
    }

    private sealed class AssetJsonConverter<T> : JsonConverter<T> where T : Asset
    {
        public override T? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType == JsonTokenType.Null)
                return null;
            using JsonDocument document = JsonDocument.ParseValue(ref reader);
            JsonElement root = document.RootElement;
            JsonElement id = root.TryGetProperty("asset", out JsonElement asset) ? asset : root;
            ulong high = id.TryGetProperty("High", out JsonElement oldHigh) ? oldHigh.GetUInt64()
                : id.TryGetProperty("high", out JsonElement highValue) ? highValue.GetUInt64() : 0;
            ulong low = id.TryGetProperty("Low", out JsonElement oldLow) ? oldLow.GetUInt64()
                : id.TryGetProperty("low", out JsonElement lowValue) ? lowValue.GetUInt64() : 0;
            return Asset.FromId(typeToConvert, new AssetId(high, low)) as T;
        }

        public override void Write(Utf8JsonWriter writer, T value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("$ref", "asset");
            writer.WritePropertyName("asset");
            JsonSerializer.Serialize(writer, value.Id, options);
            writer.WriteString("type", value.GetType().AssemblyQualifiedName);
            writer.WriteEndObject();
        }
    }

    private static readonly JsonSerializerOptions Options = CreateOptions();
    internal static JsonSerializerOptions SerializerOptions => Options;

    private static JsonSerializerOptions CreateOptions()
    {
        var resolver = new DefaultJsonTypeInfoResolver();
        resolver.Modifiers.Add(static typeInfo =>
        {
            if (typeInfo.Kind != JsonTypeInfoKind.Object)
                return;
            bool fieldOnly = typeInfo.Type.IsDefined(typeof(SerializableAttribute), false) ||
                             typeInfo.Type.IsDefined(typeof(SerializableTypeAttribute), false);
            if (fieldOnly)
            {
                for (int index = typeInfo.Properties.Count - 1; index >= 0; --index)
                {
                    ICustomAttributeProvider? provider = typeInfo.Properties[index].AttributeProvider;
                    if (provider is PropertyInfo || provider is FieldInfo field &&
                        (field.IsStatic || field.IsInitOnly || field.IsDefined(typeof(NonSerializedAttribute), false)))
                    {
                        typeInfo.Properties.RemoveAt(index);
                    }
                }
            }
            var existing = typeInfo.Properties.Select(property => property.Name).ToHashSet(StringComparer.Ordinal);
            foreach (FieldInfo field in typeInfo.Type.GetFields(BindingFlags.Instance | BindingFlags.NonPublic))
            {
                if (field.IsStatic || field.IsInitOnly || field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    !field.IsDefined(typeof(SerializeFieldAttribute), true) || !existing.Add(field.Name))
                    continue;
                JsonPropertyInfo property = typeInfo.CreateJsonPropertyInfo(field.FieldType, field.Name);
                property.Get = field.GetValue;
                property.Set = field.SetValue;
                typeInfo.Properties.Add(property);
            }
        });
        var options = new JsonSerializerOptions
        {
            IncludeFields = true,
            IgnoreReadOnlyProperties = true,
            MaxDepth = 32,
            PropertyNameCaseInsensitive = true,
            TypeInfoResolver = resolver,
            WriteIndented = false
        };
        options.Converters.Add(new ManagedCanonicalDictionaryConverterFactory());
        options.Converters.Add(new EntityJsonConverter());
        options.Converters.Add(new ComponentJsonConverterFactory());
        options.Converters.Add(new AssetJsonConverterFactory());
        return options;
    }

    public static string Capture(Behaviour behaviour, string previousState, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(previousState);
        var retained = document.Fields.ToList();
        MaterializeRetainedGraphRoots(document, retained);
        var allFields = SerializableFields(behaviour.GetType(), true).ToArray();
        var graphRoots = new List<ManagedReferenceGraphCodec.CaptureRoot>();
        if (!includeReloadOnly)
        {
            foreach (var reloadOnly in allFields.Where(field =>
                         field.IsDefined(typeof(HotReloadStateAttribute), true) &&
                         !field.IsPublic && !field.IsDefined(typeof(SerializeFieldAttribute), true)))
                RemoveCapturedFields(retained, Describe(reloadOnly));
        }
        foreach (var field in allFields.Where(field => IsSerializable(field, includeReloadOnly)))
        {
            var descriptor = Describe(field);
            object? value = field.GetValue(behaviour);
            string path = $"{field.DeclaringType?.FullName}.{field.Name}";
            descriptor.ReferenceGraph = field.IsDefined(typeof(SerializeReferenceAttribute), true);
            if (descriptor.ReferenceGraph)
            {
                string rootKey = GraphRootKey(field, descriptor);
                descriptor.ReferenceGraphRoot = rootKey;
                graphRoots.Add(new ManagedReferenceGraphCodec.CaptureRoot(
                    rootKey, value, field.FieldType, path,
                    behaviour.GetType().FullName ?? behaviour.GetType().Name, field.Name));
            }
            else
            {
                try
                {
                    ManagedObjectSerializer.ValidateSerializableValue(value, field.FieldType, path);
                    descriptor.Value = JsonSerializer.SerializeToElement(value, field.FieldType, Options);
                }
                catch (ManagedSerializationException exception)
                {
                    throw exception.WithContext("capture", behaviour.GetType().FullName ?? behaviour.GetType().Name,
                                                field.Name);
                }
            }
            RemoveCapturedFields(retained, descriptor);
            retained.Add(descriptor);
        }
        document.Version = 3;
        document.Fields = retained.OrderBy(field => field.StableId, StringComparer.Ordinal)
            .ThenBy(field => field.Name, StringComparer.Ordinal).ToList();
        document.ReferenceGraph = graphRoots.Count == 0
            ? null
            : ManagedReferenceGraphCodec.CaptureRoots(graphRoots, Options);
        string result = JsonSerializer.Serialize(document, Options);
        if (System.Text.Encoding.UTF8.GetByteCount(result) > MaximumDocumentBytes)
            throw new InvalidOperationException(
                $"Managed state documents cannot exceed {MaximumDocumentBytes} bytes.");
        return result;
    }

    public static string Restore(Behaviour behaviour, string state, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(state);
        var warnings = new List<string>();
        var prepared = new List<(FieldInfo Field, object? Value, string Path, bool Fallback)>();
        var matched = new List<(FieldInfo Field, StateField Source, string Path, bool Fallback)>();
        ulong previousWorld = s_restoreWorld;
        s_restoreWorld = behaviour.Entity?.World ?? 0;
        try
        {
            foreach (var field in SerializableFields(behaviour.GetType(), includeReloadOnly))
            {
                var descriptor = Describe(field);
                var source = Find(document.Fields, descriptor, out var fallback);
                if (source is null)
                    continue;
                string path = $"{field.DeclaringType?.FullName}.{field.Name}";
                matched.Add((field, source, path, fallback));
            }

            var shared = matched.Where(candidate => !string.IsNullOrWhiteSpace(candidate.Source.ReferenceGraphRoot))
                .ToArray();
            IReadOnlyDictionary<string, object?> sharedValues = new Dictionary<string, object?>();
            if (shared.Length != 0)
            {
                if (document.ReferenceGraph is null)
                {
                    throw new ManagedSerializationException(
                        "KEIRE-MANAGED-SERIALIZATION-0003", shared[0].Path, shared[0].Field.FieldType, null,
                        "the shared reference graph object table is missing", phase: "validate",
                        owner: behaviour.GetType().FullName ?? behaviour.GetType().Name,
                        rootField: shared[0].Field.Name);
                }
                var roots = new List<ManagedReferenceGraphCodec.RestoreRoot>(shared.Length);
                foreach (var candidate in shared)
                {
                    if (!candidate.Source.ReferenceGraph ||
                        !candidate.Field.IsDefined(typeof(SerializeReferenceAttribute), true))
                    {
                        throw new ManagedSerializationException(
                            "KEIRE-MANAGED-SERIALIZATION-0003", candidate.Path, candidate.Field.FieldType, null,
                            "shared reference-graph data requires SerializeReference on the destination field",
                            phase: "validate", owner: behaviour.GetType().FullName ?? behaviour.GetType().Name,
                            rootField: candidate.Field.Name);
                    }
                    roots.Add(new ManagedReferenceGraphCodec.RestoreRoot(
                        candidate.Source.ReferenceGraphRoot!, candidate.Field.FieldType, candidate.Path,
                        behaviour.GetType().FullName ?? behaviour.GetType().Name, candidate.Field.Name));
                }
                sharedValues = ManagedReferenceGraphCodec.RestoreRoots(document.ReferenceGraph.Value, roots, Options);
            }

            foreach (var candidate in matched)
            {
                var field = candidate.Field;
                var source = candidate.Source;
                try
                {
                    bool expectsGraph = field.IsDefined(typeof(SerializeReferenceAttribute), true);
                    if (source.ReferenceGraph && !expectsGraph)
                    {
                        throw new ManagedSerializationException(
                            "KEIRE-MANAGED-SERIALIZATION-0003", candidate.Path, field.FieldType, null,
                            "serialized reference-graph data requires SerializeReference on the destination field",
                            phase: "validate", owner: behaviour.GetType().FullName ?? behaviour.GetType().Name,
                            rootField: field.Name);
                    }
                    object? restored;
                    if (!string.IsNullOrWhiteSpace(source.ReferenceGraphRoot))
                    {
                        restored = sharedValues[source.ReferenceGraphRoot!];
                    }
                    else
                    {
                        JsonElement value = source.Value.ValueKind == JsonValueKind.Undefined
                            ? throw new ManagedSerializationException(
                                "KEIRE-MANAGED-SERIALIZATION-0003", candidate.Path, field.FieldType, null,
                                "the serialized field value is missing", phase: "validate",
                                owner: behaviour.GetType().FullName ?? behaviour.GetType().Name,
                                rootField: field.Name)
                            : source.Value;
                        restored = source.ReferenceGraph
                            ? ManagedReferenceGraphCodec.Restore(value, field.FieldType, candidate.Path, Options)
                            : value.Deserialize(field.FieldType, Options);
                    }
                    if (!expectsGraph)
                        ManagedObjectSerializer.ValidateSerializableValue(restored, field.FieldType, candidate.Path);
                    prepared.Add((field, restored, candidate.Path, candidate.Fallback));
                }
                catch (ManagedSerializationException exception)
                {
                    throw exception.WithContext(exception.Phase,
                                                behaviour.GetType().FullName ?? behaviour.GetType().Name, field.Name);
                }
                catch (Exception exception) when (exception is JsonException or ArgumentException or
                                                  InvalidOperationException or NotSupportedException)
                {
                    throw new InvalidOperationException(
                        $"Managed field '{field.DeclaringType?.FullName}.{field.Name}' could not migrate from " +
                        $"'{source.Type}'.",
                        exception);
                }
            }
        }
        finally
        {
            s_restoreWorld = previousWorld;
        }

        var applied = new List<(FieldInfo Field, object? Previous)>(prepared.Count);
        try
        {
            foreach (var candidate in prepared)
            {
                object? previous = candidate.Field.GetValue(behaviour);
                candidate.Field.SetValue(behaviour, candidate.Value);
                applied.Add((candidate.Field, previous));
                if (candidate.Fallback)
                    warnings.Add($"{candidate.Path} restored through a rename fallback.");
            }
        }
        catch (Exception exception)
        {
            for (int index = applied.Count - 1; index >= 0; --index)
            {
                try
                {
                    applied[index].Field.SetValue(behaviour, applied[index].Previous);
                }
                catch
                {
                    // Preserve the original assignment failure.
                }
            }
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0004", behaviour.GetType().FullName ?? behaviour.GetType().Name,
                behaviour.GetType(), behaviour.GetType(),
                "the transactional state commit failed and previous field values were restored", exception,
                phase: "commit", owner: behaviour.GetType().FullName ?? behaviour.GetType().Name,
                rootField: prepared.FirstOrDefault().Field?.Name ?? behaviour.GetType().Name);
        }
        return string.Join(Environment.NewLine, warnings);
    }

    private static StateDocument Read(string state)
    {
        if (string.IsNullOrWhiteSpace(state))
            return new StateDocument();
        if (System.Text.Encoding.UTF8.GetByteCount(state) > MaximumDocumentBytes)
            throw new InvalidOperationException(
                $"Managed state documents cannot exceed {MaximumDocumentBytes} bytes.");
        StateDocument result = JsonSerializer.Deserialize<StateDocument>(state, Options) ??
                               throw new InvalidOperationException("Managed state document is empty.");
        if (result.Version is < 1 or > 3)
            throw new InvalidOperationException($"Managed state version {result.Version} is unsupported.");
        foreach (StateField field in result.Fields)
        {
            if (string.IsNullOrWhiteSpace(field.ReferenceGraphRoot))
                field.ReferenceGraphRoot = null;
        }
        return result;
    }

    internal static void ValidateFieldLimitForTests(Type type) => _ = SerializableFields(type, true);

    private static FieldInfo[] SerializableFields(Type type, bool includeReloadOnly)
    {
        var result = new List<FieldInfo>();
        for (var current = type; current is not null && current != typeof(Behaviour); current = current.BaseType)
        {
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                    BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.IsInitOnly || field.IsDefined(typeof(NonSerializedAttribute), false))
                    continue;
                if (IsSerializable(field, includeReloadOnly))
                {
                    if (result.Count >= MaximumFieldsPerType)
                    {
                        string owner = type.FullName ?? type.Name;
                        throw new ManagedSerializationException(
                            "KEIRE-MANAGED-SERIALIZATION-0001",
                            $"{field.DeclaringType?.FullName ?? owner}.{field.Name}", field.FieldType, null,
                            $"serialized types cannot exceed {MaximumFieldsPerType} serialized fields",
                            phase: "validate", owner: owner, rootField: field.Name);
                    }
                    result.Add(field);
                }
            }
        }
        return result.ToArray();
    }

    private static bool IsSerializable(FieldInfo field, bool includeReloadOnly) =>
        field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
        field.IsDefined(typeof(SerializeReferenceAttribute), true) ||
        (includeReloadOnly && field.IsDefined(typeof(HotReloadStateAttribute), true));

    private static StateField Describe(FieldInfo field) => new()
    {
        StableId = field.GetCustomAttribute<StableFieldIdAttribute>()?.Id.ToString("D") ?? string.Empty,
        Name = field.Name,
        Type = field.FieldType.AssemblyQualifiedName ?? field.FieldType.FullName ?? field.FieldType.Name,
        Aliases = field.GetCustomAttributes<FormerlySerializedAsAttribute>().Select(attribute => attribute.Name).ToArray()
    };

    private static string GraphRootKey(FieldInfo field, StateField descriptor) =>
        !string.IsNullOrWhiteSpace(descriptor.StableId)
            ? $"id:{descriptor.StableId.ToLowerInvariant()}"
            : $"field:{field.DeclaringType?.FullName ?? field.DeclaringType?.Name}.{field.Name}";

    private static void MaterializeRetainedGraphRoots(StateDocument document, List<StateField> retained)
    {
        if (document.ReferenceGraph is null)
            return;
        foreach (StateField field in retained.Where(field => field.ReferenceGraph &&
                                                       !string.IsNullOrWhiteSpace(field.ReferenceGraphRoot)))
        {
            field.Value = ManagedReferenceGraphCodec.ExtractRoot(
                document.ReferenceGraph.Value, field.ReferenceGraphRoot!, typeof(object), field.Name, Options);
            field.ReferenceGraphRoot = null;
        }
        document.ReferenceGraph = null;
    }

    private static void RemoveCapturedFields(List<StateField> fields, StateField target)
    {
        var names = target.Aliases.Append(target.Name).ToHashSet(StringComparer.Ordinal);
        fields.RemoveAll(field =>
        {
            if (!string.IsNullOrEmpty(target.StableId) && !string.IsNullOrEmpty(field.StableId) &&
                string.Equals(field.StableId, target.StableId, StringComparison.OrdinalIgnoreCase))
                return true;
            return string.IsNullOrEmpty(field.StableId) &&
                   (names.Contains(field.Name) || field.Aliases.Any(names.Contains));
        });
    }

    private static StateField? Find(IEnumerable<StateField> fields, StateField target, out bool fallback)
    {
        fallback = false;
        if (!string.IsNullOrEmpty(target.StableId))
        {
            var stable = fields.FirstOrDefault(field =>
                string.Equals(field.StableId, target.StableId, StringComparison.OrdinalIgnoreCase));
            if (stable is not null)
                return stable;
        }
        var names = target.Aliases.Append(target.Name).ToHashSet(StringComparer.Ordinal);
        var named = fields.FirstOrDefault(field => names.Contains(field.Name) || field.Aliases.Any(names.Contains));
        fallback = named is not null;
        return named;
    }
}
