using System.Reflection;
using System.Text.Json;

namespace Keire;

internal static class ManagedDataHydrator
{
    private sealed record SerializableMember(MemberInfo Member, Type ValueType)
    {
        public void SetValue(object owner, object? value)
        {
            if (Member is FieldInfo field)
                field.SetValue(owner, value);
            else
                ((PropertyInfo)Member).SetValue(owner, value);
        }
    }

    internal static void Restore(ScriptableObject target, string document)
    {
        ArgumentNullException.ThrowIfNull(target);
        if (string.IsNullOrWhiteSpace(document))
            throw new InvalidOperationException("Managed data hydration document is empty.");

        using JsonDocument parsed = JsonDocument.Parse(document);
        JsonElement root = parsed.RootElement;
        if (root.GetProperty("schemaVersion").GetUInt32() != 1)
            throw new InvalidOperationException("Managed data hydration uses an unsupported schema.");

        Type type = target.GetType();
        StableAssetTypeIdAttribute stableType = type.GetCustomAttribute<StableAssetTypeIdAttribute>(false) ??
            throw new InvalidOperationException(
                $"Managed data type '{type.FullName}' does not declare StableAssetTypeId.");
        Guid sourceType = Guid.Parse(root.GetProperty("managedTypeId").GetString() ?? string.Empty);
        if (sourceType != stableType.Id)
            throw new InvalidOperationException(
                $"Managed data type '{sourceType:D}' cannot hydrate '{stableType.Id:D}'.");

        JsonElement fields = root.GetProperty("fields");
        if (fields.ValueKind != JsonValueKind.Array)
            throw new InvalidOperationException("Managed data hydration fields are malformed.");
        JsonElement[] sourceFields = fields.EnumerateArray().ToArray();
        foreach (SerializableMember member in SerializableMembers(type))
        {
            Guid stableId = ManagedStableIdentity.Field(member.Member, stableType.Id);
            JsonElement? source = FindField(sourceFields, member.Member, stableId);
            if (source is null)
                continue;
            try
            {
                JsonElement value = source.Value.GetProperty("value");
                object? restored = JsonSerializer.Deserialize(value.GetRawText(), member.ValueType,
                                                               ManagedStateSerializer.SerializerOptions);
                member.SetValue(target, restored);
            }
            catch (Exception exception) when (
                exception is JsonException or ArgumentException or InvalidOperationException or TargetInvocationException)
            {
                throw new InvalidOperationException(
                    $"Managed data member '{type.FullName}.{member.Member.Name}' could not migrate.", exception);
            }
        }
        target.Validate();
    }

    private static JsonElement? FindField(IEnumerable<JsonElement> fields, MemberInfo member, Guid stableId)
    {
        string stableText = stableId.ToString("D");
        foreach (JsonElement field in fields)
        {
            if (field.TryGetProperty("stableId", out JsonElement encodedStable) &&
                string.Equals(encodedStable.GetString(), stableText, StringComparison.OrdinalIgnoreCase))
                return field;
        }

        var names = member.GetCustomAttributes<FormerlySerializedAsAttribute>(true)
            .Select(attribute => attribute.Name).Append(member.Name).ToHashSet(StringComparer.Ordinal);
        foreach (JsonElement field in fields)
        {
            if (field.TryGetProperty("stableId", out JsonElement encodedStable) &&
                !string.IsNullOrEmpty(encodedStable.GetString()))
                continue;
            string? name = field.TryGetProperty("name", out JsonElement encodedName) ? encodedName.GetString() : null;
            if (name is not null && names.Contains(name))
                return field;
            if (field.TryGetProperty("formerNames", out JsonElement aliases) &&
                aliases.ValueKind == JsonValueKind.Array &&
                aliases.EnumerateArray().Any(alias => alias.GetString() is { } value && names.Contains(value)))
                return field;
        }
        return null;
    }

    private static IEnumerable<SerializableMember> SerializableMembers(Type type)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type; current is not null && current != typeof(ScriptableObject);
             current = current.BaseType)
            hierarchy.Push(current);

        while (hierarchy.TryPop(out Type? current))
        {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                       BindingFlags.DeclaredOnly;
            foreach (FieldInfo field in current.GetFields(flags).OrderBy(value => value.MetadataToken))
            {
                bool serialized = field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true);
                if (!serialized || field.IsStatic || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), false))
                    continue;
                yield return new SerializableMember(field, field.FieldType);
            }
            foreach (PropertyInfo property in current.GetProperties(flags).OrderBy(value => value.MetadataToken))
            {
                bool serialized = (property.GetMethod?.IsPublic == true && property.SetMethod?.IsPublic == true) ||
                                  property.IsDefined(typeof(SerializeFieldAttribute), true);
                if (!serialized || property.GetIndexParameters().Length != 0 || property.GetMethod is null ||
                    property.SetMethod is null)
                    continue;
                yield return new SerializableMember(property, property.PropertyType);
            }
        }
    }
}
