using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace Keire;

internal static class ManagedReferenceGraphCodec
{
    private const int MaximumDepth = 32;
    private const int MaximumObjects = 65_536;
    private const int MaximumEdges = 131_072;
    private const int MaximumCollectionEntries = 16_384;
    private const int MaximumRegisteredTypes = 4_096;
    private const int MaximumFieldsPerType = 1_024;
    private const int MaximumStringBytes = 1_048_576;
    private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);
    private static readonly ConditionalWeakTable<AssemblyLoadContext, SerializedTypeRegistry> TypeRegistries = new();
    private static readonly object TypeRegistryLock = new();

    private sealed class SerializedTypeRegistry(Dictionary<Guid, Type> types)
    {
        public IReadOnlyDictionary<Guid, Type> Types { get; } = types;
    }

    private sealed class GraphDocument
    {
        public int Version { get; set; } = 1;
        public GraphValue Root { get; set; } = new();
        public List<GraphRoot> Roots { get; set; } = [];
        public List<GraphNode> Objects { get; set; } = [];
    }

    private sealed class GraphRoot
    {
        public string Key { get; set; } = string.Empty;
        public GraphValue Value { get; set; } = new();
    }

    private sealed class GraphValue
    {
        public int Reference { get; set; }
        public JsonElement? Scalar { get; set; }
    }

    private sealed class GraphNode
    {
        public int Id { get; set; }
        public string Kind { get; set; } = string.Empty;
        public string StableTypeId { get; set; } = string.Empty;
        public List<GraphField> Fields { get; set; } = [];
        public List<GraphValue> Items { get; set; } = [];
        public List<GraphEntry> Entries { get; set; } = [];
    }

    private sealed class GraphField
    {
        public string StableId { get; set; } = string.Empty;
        public string Name { get; set; } = string.Empty;
        public GraphValue Value { get; set; } = new();
    }

    private sealed class GraphEntry
    {
        public GraphValue Key { get; set; } = new();
        public GraphValue Value { get; set; } = new();
    }

    private sealed record SerializableField(FieldInfo Field, Guid StableId);

    internal readonly record struct CaptureRoot(string Key, object? Value, Type DeclaredType, string Path,
                                                string Owner, string RootField);

    internal readonly record struct RestoreRoot(string Key, Type DeclaredType, string Path, string Owner,
                                                string RootField);

    private sealed class CaptureContext(JsonSerializerOptions options)
    {
        public JsonSerializerOptions Options { get; } = options;
        public Dictionary<object, int> References { get; } = new(ReferenceEqualityComparer.Instance);
        public List<GraphNode> Nodes { get; } = [];
        public int Edges;
    }

    private sealed class RestoreContext(JsonSerializerOptions options, Dictionary<int, GraphNode> nodes,
                                        IReadOnlyDictionary<Guid, Type> types)
    {
        public JsonSerializerOptions Options { get; } = options;
        public Dictionary<int, GraphNode> Nodes { get; } = nodes;
        public IReadOnlyDictionary<Guid, Type> Types { get; } = types;
        public Dictionary<int, object> Instances { get; } = [];
        public Dictionary<int, Type> InstanceTypes { get; } = [];
        public HashSet<int> Populated { get; } = [];
        public int Edges;
    }

    internal static JsonElement Capture(object? value, Type declaredType, string path,
                                        JsonSerializerOptions options)
    {
        _ = BuildTypeRegistry(path, declaredType);
        var context = new CaptureContext(options);
        GraphValue root = CaptureValue(value, declaredType, path, context, 0);
        var document = new GraphDocument { Root = root, Objects = context.Nodes.OrderBy(node => node.Id).ToList() };
        return JsonSerializer.SerializeToElement(document, options);
    }

    internal static JsonElement CaptureRoots(IEnumerable<CaptureRoot> roots, JsonSerializerOptions options)
    {
        CaptureRoot[] ordered = roots.OrderBy(root => root.Key, StringComparer.Ordinal).ToArray();
        Type diagnosticType = ordered.FirstOrDefault().DeclaredType ?? typeof(object);
        string diagnosticPath = ordered.FirstOrDefault().Path ?? "ReferenceGraph";
        ValidateRootKeys(ordered.Select(root => root.Key), diagnosticType, diagnosticPath);
        var context = new CaptureContext(options);
        var encoded = new List<GraphRoot>(ordered.Length);
        foreach (CaptureRoot root in ordered)
        {
            _ = BuildTypeRegistry(root.Path, root.DeclaredType);
            try
            {
                encoded.Add(new GraphRoot
                {
                    Key = root.Key,
                    Value = CaptureValue(root.Value, root.DeclaredType, root.Path, context, 0),
                });
            }
            catch (ManagedSerializationException exception)
            {
                throw exception.WithContext("capture", root.Owner, root.RootField);
            }
        }
        var document = new GraphDocument
        {
            Version = 2,
            Roots = encoded,
            Objects = context.Nodes.OrderBy(node => node.Id).ToList(),
        };
        return JsonSerializer.SerializeToElement(document, options);
    }

    internal static object? Restore(JsonElement source, Type declaredType, string path,
                                    JsonSerializerOptions options)
    {
        GraphDocument document;
        try
        {
            document = source.Deserialize<GraphDocument>(options) ??
                throw Invalid(declaredType, path, null, "the reference graph document is empty");
        }
        catch (ManagedSerializationException)
        {
            throw;
        }
        catch (Exception exception) when (exception is JsonException or NotSupportedException)
        {
            throw Invalid(declaredType, path, null, "the reference graph document is malformed", exception);
        }

        if (document.Version != 1)
            throw Invalid(declaredType, path, null,
                          $"reference graph version {document.Version} is unsupported");
        if (document.Objects.Count > MaximumObjects)
            throw Invalid(declaredType, path, null,
                          $"reference graphs cannot exceed {MaximumObjects} objects");

        var nodes = new Dictionary<int, GraphNode>();
        foreach (GraphNode node in document.Objects)
        {
            if (node.Id <= 0 || !nodes.TryAdd(node.Id, node))
                throw Invalid(declaredType, path, null, "reference graph object IDs must be unique and positive");
        }

        var context = new RestoreContext(options, nodes, BuildTypeRegistry(path, declaredType));
        AllocateValue(document.Root, declaredType, path, context, 0);
        if (context.Instances.Count != nodes.Count)
            throw Invalid(declaredType, path, null, "the reference graph contains unreachable objects");
        return RestoreValue(document.Root, declaredType, path, context, 0);
    }

    internal static IReadOnlyDictionary<string, object?> RestoreRoots(JsonElement source,
                                                                      IEnumerable<RestoreRoot> roots,
                                                                      JsonSerializerOptions options)
    {
        RestoreRoot[] requested = roots.OrderBy(root => root.Key, StringComparer.Ordinal).ToArray();
        Type diagnosticType = requested.FirstOrDefault().DeclaredType ?? typeof(object);
        string diagnosticPath = requested.FirstOrDefault().Path ?? "ReferenceGraph";
        ValidateRootKeys(requested.Select(root => root.Key), diagnosticType, diagnosticPath);
        GraphDocument document = DecodeDocument(source, diagnosticType, diagnosticPath, options);
        if (document.Version != 2)
            throw Invalid(diagnosticType, diagnosticPath, null,
                          $"shared reference graph version {document.Version} is unsupported");
        if (document.Objects.Count > MaximumObjects)
            throw Invalid(diagnosticType, diagnosticPath, null,
                          $"reference graphs cannot exceed {MaximumObjects} objects");

        var encodedRoots = new Dictionary<string, GraphValue>(StringComparer.Ordinal);
        foreach (GraphRoot root in document.Roots)
        {
            if (string.IsNullOrWhiteSpace(root.Key) || !encodedRoots.TryAdd(root.Key, root.Value))
                throw Invalid(diagnosticType, diagnosticPath, null,
                              "shared reference graph root keys must be unique and non-empty");
        }
        if (encodedRoots.Count != requested.Length || requested.Any(root => !encodedRoots.ContainsKey(root.Key)))
            throw Invalid(diagnosticType, diagnosticPath, null,
                          "shared reference graph roots do not match the serialized fields");

        Dictionary<int, GraphNode> nodes = DecodeNodes(document, diagnosticType, diagnosticPath);
        var context = new RestoreContext(options, nodes, BuildTypeRegistry(diagnosticPath, diagnosticType));
        foreach (RestoreRoot root in requested)
        {
            try
            {
                AllocateValue(encodedRoots[root.Key], root.DeclaredType, root.Path, context, 0);
            }
            catch (ManagedSerializationException exception)
            {
                throw exception.WithContext("allocate", root.Owner, root.RootField);
            }
        }
        if (context.Instances.Count != nodes.Count)
            throw Invalid(diagnosticType, diagnosticPath, null, "the reference graph contains unreachable objects");

        var result = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (RestoreRoot root in requested)
        {
            try
            {
                result.Add(root.Key,
                           RestoreValue(encodedRoots[root.Key], root.DeclaredType, root.Path, context, 0));
            }
            catch (ManagedSerializationException exception)
            {
                throw exception.WithContext("populate", root.Owner, root.RootField);
            }
        }
        return result;
    }

    internal static JsonElement ExtractRoot(JsonElement source, string key, Type declaredType, string path,
                                            JsonSerializerOptions options)
    {
        GraphDocument document = DecodeDocument(source, declaredType, path, options);
        if (document.Version != 2)
            throw Invalid(declaredType, path, null,
                          $"shared reference graph version {document.Version} is unsupported");
        GraphRoot root = document.Roots.SingleOrDefault(candidate =>
                             string.Equals(candidate.Key, key, StringComparison.Ordinal)) ??
                         throw Invalid(declaredType, path, null,
                                       $"shared reference graph root '{key}' does not exist");
        Dictionary<int, GraphNode> nodes = DecodeNodes(document, declaredType, path);
        var reached = new HashSet<int>();
        MarkReachable(root.Value, nodes, reached, declaredType, path);
        var extracted = new GraphDocument
        {
            Root = root.Value,
            Objects = document.Objects.Where(node => reached.Contains(node.Id)).OrderBy(node => node.Id).ToList(),
        };
        return JsonSerializer.SerializeToElement(extracted, options);
    }

    private static GraphDocument DecodeDocument(JsonElement source, Type declaredType, string path,
                                                JsonSerializerOptions options)
    {
        try
        {
            return source.Deserialize<GraphDocument>(options) ??
                   throw Invalid(declaredType, path, null, "the reference graph document is empty");
        }
        catch (ManagedSerializationException)
        {
            throw;
        }
        catch (Exception exception) when (exception is JsonException or NotSupportedException)
        {
            throw Invalid(declaredType, path, null, "the reference graph document is malformed", exception);
        }
    }

    private static Dictionary<int, GraphNode> DecodeNodes(GraphDocument document, Type declaredType, string path)
    {
        var nodes = new Dictionary<int, GraphNode>();
        foreach (GraphNode node in document.Objects)
        {
            if (node.Id <= 0 || !nodes.TryAdd(node.Id, node))
            {
                throw Invalid(declaredType, path, null,
                              "reference graph object IDs must be unique and positive");
            }
        }
        return nodes;
    }

    private static void ValidateRootKeys(IEnumerable<string> roots, Type declaredType, string path)
    {
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (string key in roots)
        {
            if (string.IsNullOrWhiteSpace(key) || !keys.Add(key))
                throw Invalid(declaredType, path, null,
                              "shared reference graph root keys must be unique and non-empty");
        }
    }

    private static void MarkReachable(GraphValue value, IReadOnlyDictionary<int, GraphNode> nodes,
                                      HashSet<int> reached, Type declaredType, string path)
    {
        if (value.Reference == 0)
            return;
        if (!nodes.TryGetValue(value.Reference, out GraphNode? node))
            throw Invalid(declaredType, path, null,
                          $"reference graph object {value.Reference} does not exist");
        if (!reached.Add(value.Reference))
            return;
        foreach (GraphField field in node.Fields)
            MarkReachable(field.Value, nodes, reached, declaredType, path);
        foreach (GraphValue item in node.Items)
            MarkReachable(item, nodes, reached, declaredType, path);
        foreach (GraphEntry entry in node.Entries)
        {
            MarkReachable(entry.Key, nodes, reached, declaredType, path);
            MarkReachable(entry.Value, nodes, reached, declaredType, path);
        }
    }

    private static GraphValue CaptureValue(object? value, Type declaredType, string path, CaptureContext context,
                                           int depth)
    {
        CheckDepth(declaredType, path, value?.GetType(), depth);
        if (value is null)
            return Scalar(null, declaredType, context.Options, path);

        Type runtimeType = value.GetType();
        if (!declaredType.IsAssignableFrom(runtimeType))
            throw Invalid(declaredType, path, runtimeType, "the runtime type is not assignable to the field");
        if (IsScalar(declaredType))
            return Scalar(value, declaredType, context.Options, path);
        if (typeof(EngineObject).IsAssignableFrom(declaredType))
            return Scalar(value, declaredType, context.Options, path);

        bool collection = declaredType.IsArray || IsList(declaredType, out _) ||
                          IsDictionary(declaredType, out _, out _);
        if (!collection && declaredType.IsValueType)
            return Scalar(value, declaredType, context.Options, path);

        if (context.References.TryGetValue(value, out int existing))
        {
            CountEdge(declaredType, path, runtimeType, context);
            return new GraphValue { Reference = existing };
        }
        if (context.Nodes.Count >= MaximumObjects)
            throw Invalid(declaredType, path, runtimeType,
                          $"reference graphs cannot exceed {MaximumObjects} objects");

        int id = context.Nodes.Count + 1;
        context.References.Add(value, id);
        var node = new GraphNode { Id = id };
        context.Nodes.Add(node);
        CountEdge(declaredType, path, runtimeType, context);

        if (declaredType.IsArray)
        {
            if (!declaredType.IsSZArray)
                throw Invalid(declaredType, path, runtimeType,
                              "only single-dimensional zero-based arrays are supported");
            node.Kind = "array";
            Type elementType = declaredType.GetElementType()!;
            var array = (Array)value;
            CheckCollectionCount(declaredType, path, runtimeType, array.Length);
            for (int index = 0; index < array.Length; ++index)
                node.Items.Add(CaptureValue(array.GetValue(index), elementType, $"{path}[{index}]", context,
                                            depth + 1));
            return new GraphValue { Reference = id };
        }

        if (IsList(declaredType, out Type? listElement))
        {
            node.Kind = "list";
            var list = (IList)value;
            CheckCollectionCount(declaredType, path, runtimeType, list.Count);
            for (int index = 0; index < list.Count; ++index)
                node.Items.Add(CaptureValue(list[index], listElement, $"{path}[{index}]", context, depth + 1));
            return new GraphValue { Reference = id };
        }

        if (IsDictionary(declaredType, out Type? keyType, out Type? valueType))
        {
            ValidateDictionaryKeyType(keyType, path);
            node.Kind = "dictionary";
            var dictionary = (IDictionary)value;
            ValidateDefaultDictionaryComparer(dictionary, declaredType, keyType, path);
            CheckCollectionCount(declaredType, path, runtimeType, dictionary.Count);
            var entries = new List<(string Order, object Key, object? Value)>();
            foreach (DictionaryEntry entry in dictionary)
            {
                object key = entry.Key ??
                    throw Invalid(keyType, $"{path}[key]", null, "dictionary keys cannot be null");
                ValidateScalarValue(key, keyType, $"{path}[key]");
                entries.Add((CanonicalKey(key, keyType, path), key, entry.Value));
            }
            entries.Sort((left, right) => string.CompareOrdinal(left.Order, right.Order));
            foreach ((string _, object key, object? item) in entries)
            {
                string itemPath = $"{path}[{FormatKey(key)}]";
                node.Entries.Add(new GraphEntry
                {
                    Key = Scalar(key, keyType, context.Options, $"{itemPath}.Key"),
                    Value = CaptureValue(item, valueType, itemPath, context, depth + 1),
                });
            }
            return new GraphValue { Reference = id };
        }

        ValidateReferenceType(runtimeType, declaredType, path);
        node.Kind = "object";
        node.StableTypeId = runtimeType.GetCustomAttribute<StableSerializedTypeIdAttribute>(false)!.Id.ToString("D");
        foreach (SerializableField member in SerializableFields(runtimeType, path))
        {
            string memberPath = $"{path}.{member.Field.Name}";
            object? memberValue;
            try
            {
                memberValue = member.Field.GetValue(value);
            }
            catch (Exception exception)
            {
                throw Invalid(member.Field.FieldType, memberPath, null, "the field could not be read", exception);
            }
            node.Fields.Add(new GraphField
            {
                StableId = member.StableId.ToString("D"),
                Name = member.Field.Name,
                Value = CaptureValue(memberValue, member.Field.FieldType, memberPath, context, depth + 1),
            });
        }
        return new GraphValue { Reference = id };
    }

    private static void AllocateValue(GraphValue value, Type declaredType, string path, RestoreContext context,
                                      int depth)
    {
        CheckDepth(declaredType, path, null, depth);
        if (value.Reference == 0)
        {
            ValidateScalarElement(value, declaredType, path);
            return;
        }
        AllocateNode(value.Reference, declaredType, path, context, depth);
    }

    private static void AllocateNode(int id, Type declaredType, string path, RestoreContext context, int depth)
    {
        context.Nodes.TryGetValue(id, out GraphNode? node);
        try
        {
            if (node is null)
                throw Invalid(declaredType, path, null, $"reference graph object {id} does not exist");
            if (context.Instances.TryGetValue(id, out object? existing))
            {
                if (!declaredType.IsAssignableFrom(existing.GetType()))
                    throw Invalid(declaredType, path, existing.GetType(),
                                  $"reference graph object {id} is incompatible with this field");
                return;
            }

            object instance;
            Type runtimeType;
            switch (node.Kind)
            {
                case "array":
                    if (!declaredType.IsSZArray)
                        throw Invalid(declaredType, path, null, $"reference graph object {id} is not an array field");
                    runtimeType = declaredType;
                    Type elementType = declaredType.GetElementType()!;
                    CheckCollectionCount(declaredType, path, null, node.Items.Count);
                    instance = Array.CreateInstance(elementType, node.Items.Count);
                    break;
                case "list":
                    if (!IsList(declaredType, out _))
                        throw Invalid(declaredType, path, null, $"reference graph object {id} is not a List field");
                    runtimeType = declaredType;
                    instance = Activator.CreateInstance(declaredType)!;
                    break;
                case "dictionary":
                    if (!IsDictionary(declaredType, out Type? keyType, out _))
                        throw Invalid(declaredType, path, null,
                                      $"reference graph object {id} is not a Dictionary field");
                    ValidateDictionaryKeyType(keyType, path);
                    runtimeType = declaredType;
                    instance = Activator.CreateInstance(declaredType)!;
                    break;
                case "object":
                    if (!Guid.TryParse(node.StableTypeId, out Guid stableTypeId) || stableTypeId == Guid.Empty ||
                        !context.Types.TryGetValue(stableTypeId, out runtimeType!))
                    {
                        throw Invalid(declaredType, path, null,
                                      $"reference graph object {id} has an unknown stable serialized type ID");
                    }
                    ValidateReferenceType(runtimeType, declaredType, path);
                    instance = Activator.CreateInstance(runtimeType, nonPublic: true)!;
                    break;
                default:
                    throw Invalid(declaredType, path, null,
                                  $"reference graph object {id} has unsupported kind '{node.Kind}'");
            }

            context.Instances.Add(id, instance);
            context.InstanceTypes.Add(id, runtimeType);

            if (node.Kind == "array" || node.Kind == "list")
            {
                Type elementType = node.Kind == "array"
                    ? declaredType.GetElementType()!
                    : declaredType.GetGenericArguments()[0];
                for (int index = 0; index < node.Items.Count; ++index)
                {
                    CountEdge(declaredType, path, runtimeType, context);
                    AllocateValue(node.Items[index], elementType, $"{path}[{index}]", context, depth + 1);
                }
                return;
            }
            if (node.Kind == "dictionary")
            {
                Type[] arguments = declaredType.GetGenericArguments();
                CheckCollectionCount(declaredType, path, runtimeType, node.Entries.Count);
                for (int index = 0; index < node.Entries.Count; ++index)
                {
                    CountEdge(declaredType, path, runtimeType, context);
                    AllocateValue(node.Entries[index].Key, arguments[0], $"{path}[{index}].Key", context, depth + 1);
                    AllocateValue(node.Entries[index].Value, arguments[1], $"{path}[{index}]", context, depth + 1);
                }
                return;
            }

            Dictionary<Guid, SerializableField> members = SerializableFields(runtimeType, path).ToDictionary(
                member => member.StableId);
            foreach (GraphField field in node.Fields)
            {
                if (!Guid.TryParse(field.StableId, out Guid stableId) ||
                    !members.TryGetValue(stableId, out var member))
                    continue;
                CountEdge(declaredType, path, runtimeType, context);
                AllocateValue(field.Value, member.Field.FieldType, $"{path}.{member.Field.Name}", context, depth + 1);
            }
        }
        catch (ManagedSerializationException exception)
        {
            throw exception.WithGraphNode("allocate", node?.StableTypeId, id);
        }
    }

    private static object? RestoreValue(GraphValue value, Type declaredType, string path, RestoreContext context,
                                        int depth)
    {
        CheckDepth(declaredType, path, null, depth);
        if (value.Reference == 0)
            return RestoreScalar(value, declaredType, path, context.Options);
        PopulateNode(value.Reference, declaredType, path, context, depth);
        return context.Instances[value.Reference];
    }

    private static void PopulateNode(int id, Type declaredType, string path, RestoreContext context, int depth)
    {
        if (!context.Populated.Add(id))
            return;
        GraphNode node = context.Nodes[id];
        try
        {
            object instance = context.Instances[id];
            Type runtimeType = context.InstanceTypes[id];

            if (node.Kind == "array")
            {
                var array = (Array)instance;
                Type elementType = declaredType.GetElementType()!;
                for (int index = 0; index < node.Items.Count; ++index)
                    array.SetValue(
                        RestoreValue(node.Items[index], elementType, $"{path}[{index}]", context, depth + 1),
                        index);
                return;
            }
            if (node.Kind == "list")
            {
                var list = (IList)instance;
                Type elementType = declaredType.GetGenericArguments()[0];
                for (int index = 0; index < node.Items.Count; ++index)
                    list.Add(RestoreValue(node.Items[index], elementType, $"{path}[{index}]", context, depth + 1));
                return;
            }
            if (node.Kind == "dictionary")
            {
                var dictionary = (IDictionary)instance;
                Type[] arguments = declaredType.GetGenericArguments();
                for (int index = 0; index < node.Entries.Count; ++index)
                {
                    object key = RestoreValue(node.Entries[index].Key, arguments[0], $"{path}[{index}].Key", context,
                                              depth + 1) ??
                        throw Invalid(arguments[0], $"{path}[{index}].Key", null, "dictionary keys cannot be null");
                    object? item = RestoreValue(node.Entries[index].Value, arguments[1], $"{path}[{FormatKey(key)}]",
                                                context, depth + 1);
                    try
                    {
                        dictionary.Add(key, item);
                    }
                    catch (Exception exception) when (exception is ArgumentException or InvalidOperationException)
                    {
                        throw Invalid(declaredType, $"{path}[{FormatKey(key)}]", runtimeType,
                                      "the dictionary entry is duplicated or invalid", exception);
                    }
                }
                return;
            }

            Dictionary<Guid, SerializableField> members = SerializableFields(runtimeType, path).ToDictionary(
                member => member.StableId);
            foreach (GraphField encoded in node.Fields)
            {
                if (!Guid.TryParse(encoded.StableId, out Guid stableId) ||
                    !members.TryGetValue(stableId, out var member))
                    continue;
                string memberPath = $"{path}.{member.Field.Name}";
                object? restored = RestoreValue(encoded.Value, member.Field.FieldType, memberPath, context,
                                                depth + 1);
                try
                {
                    member.Field.SetValue(instance, restored);
                }
                catch (Exception exception)
                {
                    throw Invalid(member.Field.FieldType, memberPath, restored?.GetType(),
                                  "the field could not be populated", exception);
                }
            }
        }
        catch (ManagedSerializationException exception)
        {
            throw exception.WithGraphNode("populate", node.StableTypeId, id);
        }
    }

    private static GraphValue Scalar(object? value, Type declaredType, JsonSerializerOptions options, string path)
    {
        if (value is not null)
            ValidateScalarValue(value, declaredType, path);
        try
        {
            return new GraphValue { Scalar = JsonSerializer.SerializeToElement(value, declaredType, options) };
        }
        catch (Exception exception) when (exception is JsonException or NotSupportedException)
        {
            throw Invalid(declaredType, path, value?.GetType(), "the scalar value could not be encoded", exception);
        }
    }

    private static object? RestoreScalar(GraphValue value, Type declaredType, string path,
                                         JsonSerializerOptions options)
    {
        ValidateScalarElement(value, declaredType, path);
        if (value.Scalar is null || value.Scalar.Value.ValueKind == JsonValueKind.Null)
            return null;
        try
        {
            object? restored = value.Scalar!.Value.Deserialize(declaredType, options);
            if (restored is not null)
                ValidateScalarValue(restored, declaredType, path);
            return restored;
        }
        catch (Exception exception) when (exception is JsonException or NotSupportedException)
        {
            throw Invalid(declaredType, path, null, "the scalar value could not be decoded", exception);
        }
    }

    private static void ValidateScalarElement(GraphValue value, Type declaredType, string path)
    {
        if (value.Scalar is null || value.Scalar.Value.ValueKind == JsonValueKind.Null)
        {
            if (!declaredType.IsValueType || Nullable.GetUnderlyingType(declaredType) is not null)
                return;
            throw Invalid(declaredType, path, null, "a non-nullable value field contains null");
        }
        if (value.Scalar.Value.ValueKind == JsonValueKind.Undefined)
            throw Invalid(declaredType, path, null, "the reference graph scalar is missing");
        if (!IsScalar(declaredType) && !typeof(EngineObject).IsAssignableFrom(declaredType) &&
            !declaredType.IsValueType && value.Scalar.Value.ValueKind != JsonValueKind.Null)
        {
            throw Invalid(declaredType, path, null,
                          "non-null class values in a reference graph must use an object reference");
        }
    }

    private static List<SerializableField> SerializableFields(Type type, string path)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type; current is not null && current != typeof(object); current = current.BaseType)
            hierarchy.Push(current);

        Guid typeId = type.GetCustomAttribute<StableSerializedTypeIdAttribute>(false)?.Id ?? Guid.Empty;
        var result = new List<SerializableField>();
        var stableIds = new HashSet<Guid>();
        while (hierarchy.TryPop(out Type? current))
        {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                       BindingFlags.DeclaredOnly;
            foreach (FieldInfo field in current.GetFields(flags).OrderBy(field => field.MetadataToken))
            {
                bool serialized = field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
                                  field.IsDefined(typeof(SerializeReferenceAttribute), true);
                if (!serialized || field.IsStatic || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(CompilerGeneratedAttribute), false))
                {
                    continue;
                }
                Guid stableId = field.GetCustomAttribute<StableFieldIdAttribute>(true)?.Id ??
                                ManagedStableIdentity.Field(field, typeId);
                if (!stableIds.Add(stableId))
                    throw Invalid(field.FieldType, $"{path}.{field.Name}", null,
                                  $"stable field ID '{stableId:D}' is duplicated");
                result.Add(new SerializableField(field, stableId));
            }
        }
        if (result.Count > MaximumFieldsPerType)
            throw Invalid(type, path, type, $"serialized types cannot exceed {MaximumFieldsPerType} fields");
        return result.OrderBy(field => field.StableId).ThenBy(field => field.Field.Name, StringComparer.Ordinal).ToList();
    }

    internal static IReadOnlyDictionary<Guid, Type> TypeRegistry(Type declaredType, string path)
    {
        AssemblyLoadContext loadContext = AssemblyLoadContext.GetLoadContext(declaredType.Assembly) ??
                                          AssemblyLoadContext.Default;
        lock (TypeRegistryLock)
        {
            return TypeRegistries.GetValue(loadContext,
                context => new SerializedTypeRegistry(CreateTypeRegistry(context, path, declaredType))).Types;
        }
    }

    internal static IReadOnlyDictionary<Guid, Type> TypeRegistryForTests(Type declaredType) =>
        TypeRegistry(declaredType, declaredType.FullName ?? declaredType.Name);

    internal static IReadOnlyDictionary<Guid, Type> TypeRegistryForTests(IEnumerable<Type> candidates,
                                                                         Type declaredType, string path) =>
        CreateTypeRegistry(candidates, path, declaredType);

    internal static IReadOnlyDictionary<Guid, Type> InstallTypeRegistry(IEnumerable<Type> candidates,
                                                                         Type contextType, string path)
    {
        AssemblyLoadContext loadContext = AssemblyLoadContext.GetLoadContext(contextType.Assembly) ??
                                          AssemblyLoadContext.Default;
        IEnumerable<Type> registryCandidates = candidates.Concat(SafeTypes(typeof(SerializableTypeAttribute).Assembly))
            .Distinct();
        var replacement = new SerializedTypeRegistry(CreateTypeRegistry(registryCandidates, path, contextType));
        lock (TypeRegistryLock)
        {
            TypeRegistries.Remove(loadContext);
            TypeRegistries.Add(loadContext, replacement);
        }
        return replacement.Types;
    }

    private static IReadOnlyDictionary<Guid, Type> BuildTypeRegistry(string path, Type declaredType) =>
        TypeRegistry(declaredType, path);

    private static Dictionary<Guid, Type> CreateTypeRegistry(AssemblyLoadContext loadContext, string path,
                                                              Type declaredType)
    {
        IEnumerable<Assembly> assemblies = loadContext.Assemblies;
        if (!assemblies.Contains(typeof(SerializableTypeAttribute).Assembly))
            assemblies = assemblies.Append(typeof(SerializableTypeAttribute).Assembly);
        return CreateTypeRegistry(assemblies.Where(assembly => !assembly.IsDynamic).SelectMany(SafeTypes), path,
                                  declaredType);
    }

    private static Dictionary<Guid, Type> CreateTypeRegistry(IEnumerable<Type> candidates, string path,
                                                              Type declaredType)
    {
        var result = new Dictionary<Guid, Type>();
        foreach (Type type in candidates.OrderBy(type => type.FullName, StringComparer.Ordinal))
        {
            StableSerializedTypeIdAttribute? attribute =
                type.GetCustomAttribute<StableSerializedTypeIdAttribute>(false);
            if (attribute is null)
                continue;
            if (attribute.Id == Guid.Empty)
                throw Invalid(type, path, type, "StableSerializedTypeId cannot be empty");
            ValidateReferenceType(type, type, path);
            if (!result.TryAdd(attribute.Id, type))
            {
                throw Invalid(type, path, type,
                              $"stable serialized type ID '{attribute.Id:D}' is already used by " +
                              $"'{result[attribute.Id].FullName}'");
            }
            if (result.Count > MaximumRegisteredTypes)
                throw Invalid(type, path, type,
                              $"the serialized type registry cannot exceed {MaximumRegisteredTypes} types");
        }
        return result;
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

    private static void ValidateReferenceType(Type runtimeType, Type declaredType, string path)
    {
        if (!declaredType.IsAssignableFrom(runtimeType))
            throw Invalid(declaredType, path, runtimeType, "the runtime type is not assignable to the field");
        if (runtimeType.IsAbstract || runtimeType.IsInterface || runtimeType.ContainsGenericParameters)
            throw Invalid(declaredType, path, runtimeType,
                          "SerializeReference runtime types must be closed concrete classes");
        if (!runtimeType.IsDefined(typeof(SerializableAttribute), false) &&
            !runtimeType.IsDefined(typeof(SerializableTypeAttribute), false))
        {
            throw Invalid(declaredType, path, runtimeType,
                          "SerializeReference runtime types must declare Serializable");
        }
        StableSerializedTypeIdAttribute? stableId =
            runtimeType.GetCustomAttribute<StableSerializedTypeIdAttribute>(false);
        if (stableId is null || stableId.Id == Guid.Empty)
            throw Invalid(declaredType, path, runtimeType,
                          "SerializeReference runtime types require StableSerializedTypeId");
        if (runtimeType.GetConstructor(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null,
                                       Type.EmptyTypes, null) is null)
        {
            throw Invalid(declaredType, path, runtimeType,
                          "SerializeReference runtime types require a parameterless constructor");
        }
    }

    private static bool IsScalar(Type type) =>
        ManagedCustomValueRegistry.TryResolve(type, out _) ||
        type == typeof(bool) || type == typeof(sbyte) || type == typeof(byte) || type == typeof(short) ||
        type == typeof(ushort) || type == typeof(int) || type == typeof(uint) || type == typeof(long) ||
        type == typeof(ulong) || type == typeof(char) || type == typeof(float) || type == typeof(double) ||
        type == typeof(decimal) || type == typeof(string) || type == typeof(Guid) || type.IsEnum ||
        type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) ||
        type == typeof(Quaternion) || type == typeof(Color);

    private static void ValidateScalarValue(object value, Type declaredType, string path)
    {
        if (declaredType != typeof(string))
            return;

        int byteCount;
        try
        {
            byteCount = StrictUtf8.GetByteCount((string)value);
        }
        catch (EncoderFallbackException exception)
        {
            throw Invalid(declaredType, path, declaredType,
                          "strings must contain valid Unicode text", exception);
        }
        if (byteCount > MaximumStringBytes)
            throw Invalid(declaredType, path, declaredType,
                          $"strings cannot exceed {MaximumStringBytes} UTF-8 bytes");
    }

    private static bool IsList(Type type, out Type elementType)
    {
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>))
        {
            elementType = type.GetGenericArguments()[0];
            return true;
        }
        elementType = null!;
        return false;
    }

    private static bool IsDictionary(Type type, out Type keyType, out Type valueType)
    {
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(Dictionary<,>))
        {
            Type[] arguments = type.GetGenericArguments();
            keyType = arguments[0];
            valueType = arguments[1];
            return true;
        }
        keyType = null!;
        valueType = null!;
        return false;
    }

    private static void ValidateDictionaryKeyType(Type type, string path)
    {
        if (type == typeof(string) || type == typeof(bool) || type == typeof(char) || type == typeof(sbyte) ||
            type == typeof(byte) || type == typeof(short) || type == typeof(ushort) || type == typeof(int) ||
            type == typeof(uint) || type == typeof(long) || type == typeof(ulong) || type == typeof(Guid) ||
            type.IsEnum)
        {
            return;
        }
        throw Invalid(type, $"{path}[key]", null,
                      "dictionary keys must be strings, booleans, characters, integers, enums, or GUIDs");
    }

    private static string CanonicalKey(object key, Type keyType, string path)
    {
        ValidateDictionaryKeyType(keyType, path);
        string value = key switch
        {
            string text => text,
            bool boolean => boolean ? "1" : "0",
            char character => ((int)character).ToString("X4", CultureInfo.InvariantCulture),
            Guid guid => guid.ToString("D"),
            Enum enumeration => CanonicalEnum(enumeration),
            sbyte or short or int or long => Convert.ToInt64(key, CultureInfo.InvariantCulture)
                .ToString("+0000000000000000000;-0000000000000000000", CultureInfo.InvariantCulture),
            _ => Convert.ToUInt64(key, CultureInfo.InvariantCulture).ToString("D20", CultureInfo.InvariantCulture),
        };
        return $"{keyType.FullName}:{value}";
    }

    private static string CanonicalEnum(Enum value)
    {
        Type underlying = Enum.GetUnderlyingType(value.GetType());
        return underlying == typeof(byte) || underlying == typeof(ushort) || underlying == typeof(uint) ||
               underlying == typeof(ulong)
            ? Convert.ToUInt64(value, CultureInfo.InvariantCulture).ToString("D20", CultureInfo.InvariantCulture)
            : Convert.ToInt64(value, CultureInfo.InvariantCulture)
                .ToString("+0000000000000000000;-0000000000000000000", CultureInfo.InvariantCulture);
    }

    private static void ValidateDefaultDictionaryComparer(IDictionary dictionary, Type dictionaryType, Type keyType,
                                                          string path)
    {
        object? comparer = dictionaryType.GetProperty("Comparer", BindingFlags.Instance | BindingFlags.Public)?
            .GetValue(dictionary);
        object? defaultComparer = typeof(EqualityComparer<>).MakeGenericType(keyType)
            .GetProperty("Default", BindingFlags.Static | BindingFlags.Public)?.GetValue(null);
        if (!ReferenceEquals(comparer, defaultComparer))
            throw Invalid(dictionaryType, path, dictionaryType, "custom comparers are not supported for dictionaries");
    }

    private static string FormatKey(object key) => key is string text ? $"\"{text}\"" :
        Convert.ToString(key, CultureInfo.InvariantCulture) ?? key.ToString() ?? "?";

    private static void CheckDepth(Type declaredType, string path, Type? runtimeType, int depth)
    {
        if (depth > MaximumDepth)
            throw Invalid(declaredType, path, runtimeType,
                          $"reference graph depth cannot exceed {MaximumDepth}");
    }

    private static void CheckCollectionCount(Type declaredType, string path, Type? runtimeType, int count)
    {
        if (count > MaximumCollectionEntries)
            throw Invalid(declaredType, path, runtimeType,
                          $"collections cannot exceed {MaximumCollectionEntries} entries");
    }

    private static void CountEdge(Type declaredType, string path, Type? runtimeType, CaptureContext context)
    {
        if (++context.Edges > MaximumEdges)
            throw Invalid(declaredType, path, runtimeType,
                          $"reference graphs cannot exceed {MaximumEdges} edges");
    }

    private static void CountEdge(Type declaredType, string path, Type? runtimeType, RestoreContext context)
    {
        if (++context.Edges > MaximumEdges)
            throw Invalid(declaredType, path, runtimeType,
                          $"reference graphs cannot exceed {MaximumEdges} edges");
    }

    private static ManagedSerializationException Invalid(Type declaredType, string path, Type? runtimeType,
                                                         string reason, Exception? innerException = null) =>
        new("KEIRE-MANAGED-SERIALIZATION-0002", path, declaredType, runtimeType, reason, innerException);
}
