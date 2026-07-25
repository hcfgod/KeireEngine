using System.Reflection;
using System.Text.Json;

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

    private static readonly JsonSerializerOptions Options = new()
    {
        IncludeFields = true,
        MaxDepth = 16,
        PropertyNameCaseInsensitive = false,
        WriteIndented = false
    };

    public static string Capture(Behaviour behaviour, string previousState, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(previousState);
        var retained = document.Fields.ToDictionary(FieldIdentity, StringComparer.Ordinal);
        var allFields = SerializableFields(behaviour.GetType(), true).ToArray();
        if (!includeReloadOnly)
        {
            foreach (var reloadOnly in allFields.Where(field =>
                         field.IsDefined(typeof(HotReloadStateAttribute), true) &&
                         !field.IsPublic && !field.IsDefined(typeof(SerializeFieldAttribute), true)))
                retained.Remove(FieldIdentity(Describe(reloadOnly)));
        }
        foreach (var field in allFields.Where(field => IsSerializable(field, includeReloadOnly)))
        {
            var descriptor = Describe(field);
            descriptor.Value = JsonSerializer.SerializeToElement(field.GetValue(behaviour), field.FieldType, Options);
            retained[FieldIdentity(descriptor)] = descriptor;
        }
        document.Fields = retained.Values.OrderBy(field => field.StableId, StringComparer.Ordinal)
            .ThenBy(field => field.Name, StringComparer.Ordinal).ToList();
        return JsonSerializer.Serialize(document, Options);
    }

    public static string Restore(Behaviour behaviour, string state, bool includeReloadOnly)
    {
        ArgumentNullException.ThrowIfNull(behaviour);
        var document = Read(state);
        var warnings = new List<string>();
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

    private static string FieldIdentity(StateField field) =>
        string.IsNullOrEmpty(field.StableId) ? $"name:{field.Name}" : $"id:{field.StableId}";

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
