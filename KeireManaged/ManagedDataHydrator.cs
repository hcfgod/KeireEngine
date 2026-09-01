using System.Reflection;
using System.Text.Json;

namespace Keire;

internal static class ManagedDataHydrator
{
    private const int MaximumFieldsPerType = 1_024;

    private sealed record SerializableMember(MemberInfo Member, Type ValueType)
    {
        public object? GetValue(object owner)
        {
            return Member is FieldInfo field ? field.GetValue(owner) : ((PropertyInfo)Member).GetValue(owner);
        }

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
        uint schemaVersion = root.GetProperty("schemaVersion").GetUInt32();
        if (schemaVersion < 1 || schemaVersion > 4)
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
        var prepared = new List<(SerializableMember Member, object? Value, string Path)>();
        var matched = new List<(SerializableMember Member, JsonElement Source, string Path)>();
        foreach (SerializableMember member in SerializableMembers(type))
        {
            Guid stableId = ManagedStableIdentity.Field(member.Member, stableType.Id);
            JsonElement? source = FindField(sourceFields, member.Member, stableId);
            if (source is null)
                continue;
            matched.Add((member, source.Value, $"{type.FullName}.{member.Member.Name}"));
        }

        var shared = matched.Where(candidate =>
            candidate.Source.TryGetProperty("referenceGraphRoot", out JsonElement graphRoot) &&
            graphRoot.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(graphRoot.GetString())).ToArray();
        IReadOnlyDictionary<string, object?> sharedValues = new Dictionary<string, object?>();
        if (shared.Length != 0)
        {
            if (schemaVersion is not 3 and not 4 ||
                !root.TryGetProperty("referenceGraph", out JsonElement sharedGraph))
            {
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0003", shared[0].Path, shared[0].Member.ValueType, null,
                    "the managed-data shared reference graph object table is missing", phase: "validate",
                    owner: type.FullName ?? type.Name, rootField: shared[0].Member.Member.Name);
            }
            var roots = new List<ManagedReferenceGraphCodec.RestoreRoot>(shared.Length);
            foreach (var candidate in shared)
            {
                bool expectsGraph = candidate.Member.Member.IsDefined(typeof(SerializeReferenceAttribute), true);
                bool referenceGraph = candidate.Source.TryGetProperty("referenceGraph", out JsonElement marker) &&
                                      marker.ValueKind == JsonValueKind.True;
                if (!expectsGraph || !referenceGraph)
                {
                    throw new ManagedSerializationException(
                        "KEIRE-MANAGED-SERIALIZATION-0003", candidate.Path, candidate.Member.ValueType, null,
                        "shared managed-data graph roots require SerializeReference", phase: "validate",
                        owner: type.FullName ?? type.Name, rootField: candidate.Member.Member.Name);
                }
                string key = candidate.Source.GetProperty("referenceGraphRoot").GetString()!;
                roots.Add(new ManagedReferenceGraphCodec.RestoreRoot(
                    key, candidate.Member.ValueType, candidate.Path, type.FullName ?? type.Name,
                    candidate.Member.Member.Name));
            }
            sharedValues = ManagedReferenceGraphCodec.RestoreRoots(sharedGraph, roots,
                                                                   ManagedStateSerializer.SerializerOptions);
        }

        foreach (var candidate in matched)
        {
            SerializableMember member = candidate.Member;
            JsonElement source = candidate.Source;
            try
            {
                bool referenceGraph = source.TryGetProperty("referenceGraph", out JsonElement graph) &&
                                      graph.ValueKind == JsonValueKind.True;
                bool expectsGraph = member.Member.IsDefined(typeof(SerializeReferenceAttribute), true);
                if (referenceGraph && !expectsGraph)
                {
                    throw new ManagedSerializationException(
                        "KEIRE-MANAGED-SERIALIZATION-0003", candidate.Path, member.ValueType, null,
                        "reference-graph data requires SerializeReference on the destination field",
                        phase: "validate", owner: type.FullName ?? type.Name, rootField: member.Member.Name);
                }
                object? restored;
                if (source.TryGetProperty("referenceGraphRoot", out JsonElement rootKey) &&
                    rootKey.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(rootKey.GetString()))
                {
                    restored = sharedValues[rootKey.GetString()!];
                }
                else
                {
                    JsonElement value = source.GetProperty("value");
                    restored = referenceGraph
                        ? ManagedReferenceGraphCodec.Restore(value, member.ValueType, candidate.Path,
                                                             ManagedStateSerializer.SerializerOptions)
                        : JsonSerializer.Deserialize(value.GetRawText(), member.ValueType,
                                                     ManagedStateSerializer.SerializerOptions);
                }
                if (!expectsGraph)
                    ManagedObjectSerializer.ValidateSerializableValue(restored, member.ValueType, candidate.Path);
                prepared.Add((member, restored, candidate.Path));
            }
            catch (ManagedSerializationException exception)
            {
                throw exception.WithContext(exception.Phase, type.FullName ?? type.Name, member.Member.Name);
            }
            catch (Exception exception) when (
                exception is JsonException or ArgumentException or InvalidOperationException or TargetInvocationException)
            {
                throw new InvalidOperationException(
                    $"Managed data member '{type.FullName}.{member.Member.Name}' could not migrate.", exception);
            }
        }

        var applied = new List<(SerializableMember Member, object? Previous)>(prepared.Count);
        try
        {
            foreach (var candidate in prepared)
            {
                object? previous = candidate.Member.GetValue(target);
                candidate.Member.SetValue(target, candidate.Value);
                applied.Add((candidate.Member, previous));
            }
            ManagedSerializationCallbacks.InvokeAfterDeserialize(target);
            target.Validate();
        }
        catch (Exception exception)
        {
            for (int index = applied.Count - 1; index >= 0; --index)
            {
                try
                {
                    applied[index].Member.SetValue(target, applied[index].Previous);
                }
                catch
                {
                    // Preserve the original hydration or validation failure.
                }
            }
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0004", type.FullName ?? type.Name, type, type,
                "the transactional managed-data commit failed and previous values were restored", exception,
                phase: "commit", owner: type.FullName ?? type.Name,
                rootField: prepared.FirstOrDefault().Member?.Member.Name ?? type.Name);
        }
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

    internal static void ValidateFieldLimitForTests(Type type) => _ = SerializableMembers(type);

    private static SerializableMember[] SerializableMembers(Type type)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type; current is not null && current != typeof(ScriptableObject);
             current = current.BaseType)
            hierarchy.Push(current);

        var result = new List<SerializableMember>();
        while (hierarchy.TryPop(out Type? current))
        {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                       BindingFlags.DeclaredOnly;
            foreach (FieldInfo field in current.GetFields(flags).OrderBy(value => value.MetadataToken))
            {
                bool serialized = field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
                                  field.IsDefined(typeof(SerializeReferenceAttribute), true);
                if (!serialized || field.IsStatic || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), false))
                    continue;
                AddMember(new SerializableMember(field, field.FieldType));
            }
        }
        return result.ToArray();

        void AddMember(SerializableMember member)
        {
            if (result.Count >= MaximumFieldsPerType)
            {
                string owner = type.FullName ?? type.Name;
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0001", $"{member.Member.DeclaringType?.FullName ?? owner}." +
                    member.Member.Name, member.ValueType, null,
                    $"serialized types cannot exceed {MaximumFieldsPerType} serialized fields",
                    phase: "validate", owner: owner, rootField: member.Member.Name);
            }
            result.Add(member);
        }
    }
}
