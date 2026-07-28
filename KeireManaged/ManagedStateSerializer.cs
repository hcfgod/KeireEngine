using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;

namespace Keire;

internal static class ManagedStateSerializer
{
    private sealed class StateDocument
    {
        public int Version { get; set; } = 1;
        public List<StateField> Fields { get; set; } = [];
    }

    private sealed class StateField
    {
        public string StableId { get; set; } = string.Empty;
        public string Name { get; set; } = string.Empty;
        public string Type { get; set; } = string.Empty;
        public string[] Aliases { get; set; } = [];
        public JsonElement Value { get; set; }
    }

    [ThreadStatic]
    private static ulong s_restoreWorld;

    private sealed class EntityJsonConverter : JsonConverter<Entity>
    {
        public override Entity Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            using JsonDocument document = JsonDocument.ParseValue(ref reader);
            JsonElement root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Null)
                return default;
            JsonElement id = TryProperty(root, "Id", "id", out JsonElement nested) ? nested : root;
            ulong high = ReadUInt64(id, "High", "high");
            ulong low = ReadUInt64(id, "Low", "low");
            return new Entity(s_restoreWorld, new EntityId(high, low));
        }

        public override void Write(Utf8JsonWriter writer, Entity value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WritePropertyName("Id");
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

    private sealed class UiButtonJsonConverter : JsonConverter<UiButton>
    {
        public override UiButton? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType == JsonTokenType.Null)
                return null;
            Entity entity = JsonSerializer.Deserialize<Entity>(ref reader, options);
            return entity.Id == default ? null : new UiButton(entity);
        }

        public override void Write(Utf8JsonWriter writer, UiButton value, JsonSerializerOptions options) =>
            JsonSerializer.Serialize(writer, value.Entity, options);
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
            MaxDepth = 16,
            PropertyNameCaseInsensitive = true,
            TypeInfoResolver = resolver,
            WriteIndented = false
        };
        options.Converters.Add(new EntityJsonConverter());
        options.Converters.Add(new UiButtonJsonConverter());
        return options;
    }

    public static string Capture(Behaviour behaviour, string previousState, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(previousState);
        var retained = document.Fields.ToList();
        var allFields = SerializableFields(behaviour.GetType(), true).ToArray();
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
            descriptor.Value = JsonSerializer.SerializeToElement(field.GetValue(behaviour), field.FieldType, Options);
            RemoveCapturedFields(retained, descriptor);
            retained.Add(descriptor);
        }
        document.Fields = retained.OrderBy(field => field.StableId, StringComparer.Ordinal)
            .ThenBy(field => field.Name, StringComparer.Ordinal).ToList();
        return JsonSerializer.Serialize(document, Options);
    }

    public static string Restore(Behaviour behaviour, string state, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(state);
        var warnings = new List<string>();
        ulong previousWorld = s_restoreWorld;
        s_restoreWorld = behaviour.Entity.World;
        try
        {
            foreach (var field in SerializableFields(behaviour.GetType(), includeReloadOnly))
            {
                var descriptor = Describe(field);
                var source = Find(document.Fields, descriptor, out var fallback);
                if (source is null)
                    continue;
                try
                {
                    field.SetValue(behaviour, source.Value.Deserialize(field.FieldType, Options));
                    if (fallback)
                        warnings.Add($"{field.DeclaringType?.FullName}.{field.Name} restored through a rename fallback.");
                }
                catch (Exception exception) when (exception is JsonException or ArgumentException or InvalidOperationException)
                {
                    throw new InvalidOperationException(
                        $"Managed field '{field.DeclaringType?.FullName}.{field.Name}' could not migrate from '{source.Type}'.",
                        exception);
                }
            }
        }
        finally
        {
            s_restoreWorld = previousWorld;
        }
        return string.Join(Environment.NewLine, warnings);
    }

    private static StateDocument Read(string state)
    {
        if (string.IsNullOrWhiteSpace(state))
            return new StateDocument();
        return JsonSerializer.Deserialize<StateDocument>(state, Options) ??
               throw new InvalidOperationException("Managed state document is empty.");
    }

    private static IEnumerable<FieldInfo> SerializableFields(Type type, bool includeReloadOnly)
    {
        for (var current = type; current is not null && current != typeof(Behaviour); current = current.BaseType)
        {
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                    BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.IsInitOnly || field.IsDefined(typeof(NonSerializedAttribute), false))
                    continue;
                if (IsSerializable(field, includeReloadOnly))
                    yield return field;
            }
        }
    }

    private static bool IsSerializable(FieldInfo field, bool includeReloadOnly) =>
        field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
        (includeReloadOnly && field.IsDefined(typeof(HotReloadStateAttribute), true));

    private static StateField Describe(FieldInfo field) => new()
    {
        StableId = field.GetCustomAttribute<StableFieldIdAttribute>()?.Id.ToString("D") ?? string.Empty,
        Name = field.Name,
        Type = field.FieldType.AssemblyQualifiedName ?? field.FieldType.FullName ?? field.FieldType.Name,
        Aliases = field.GetCustomAttributes<FormerlySerializedAsAttribute>().Select(attribute => attribute.Name).ToArray()
    };

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
