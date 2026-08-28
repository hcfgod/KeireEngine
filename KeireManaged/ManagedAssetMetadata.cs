using System.Collections;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Keire;

internal static class ManagedAssetMetadata
{
    private const string ManagedDataAssetTypeId = "4b454952-454d-4441-5441-415353455401";
    private const int MaximumDepth = 32;
    private const int MaximumRegisteredTypes = 4_096;
    private const int MaximumReferenceTypeChoices = 256;
    private const int MaximumFieldsPerType = 1_024;

    private sealed class ExportDocument
    {
        public int SchemaVersion { get; init; } = 1;
        public List<TypeDocument> Types { get; } = [];
        public List<BehaviourGraphDocument> Behaviours { get; } = [];
        public List<DiagnosticDocument> Diagnostics { get; } = [];
    }

    private sealed class BehaviourGraphDocument
    {
        public required string FullName { get; init; }
        public List<PropertyDocument> Properties { get; } = [];
        public List<ReferenceTypeDocument> ReferenceTypes { get; } = [];
    }

    private sealed class TypeDocument
    {
        public required string StableTypeId { get; init; }
        public required string FullName { get; init; }
        public required string DisplayName { get; init; }
        public string? BaseTypeId { get; init; }
        public required string MenuPath { get; init; }
        public required string DefaultFileName { get; init; }
        public List<PropertyDocument> Properties { get; } = [];
        public List<ReferenceTypeDocument> ReferenceTypes { get; } = [];
    }

    private sealed class ReferenceTypeDocument
    {
        public required string StableTypeId { get; init; }
        public required string FullName { get; init; }
        public required string DisplayName { get; init; }
        public List<PropertyDocument> Properties { get; } = [];
    }

    private sealed class PropertyDocument
    {
        public required string StableFieldId { get; init; }
        public required string Name { get; set; }
        public required string DisplayName { get; init; }
        public required string ManagedTypeName { get; init; }
        public required int Kind { get; init; }
        public bool ReadOnly { get; init; }
        public bool Hidden { get; init; }
        public double? Minimum { get; set; }
        public double? Maximum { get; set; }
        public required string Header { get; init; }
        public required string Tooltip { get; init; }
        public string? ExpectedAssetType { get; set; }
        public string? ExpectedManagedType { get; set; }
        public bool IncludeDerivedAssetTypes { get; init; } = true;
        public List<PropertyDocument> Children { get; } = [];
        public double Step { get; set; } = 0.1;
        public bool Slider { get; set; }
        public int TextLines { get; set; } = 1;
        public bool ReferenceGraph { get; set; }
        public List<string> ReferenceTypeChoices { get; } = [];
    }

    private sealed class DiagnosticDocument
    {
        public required string Code { get; init; }
        public required string Phase { get; init; }
        public required string Owner { get; init; }
        public required string RootField { get; init; }
        public required string TypeName { get; init; }
        public required string FieldPath { get; init; }
        public required string DeclaredType { get; init; }
        public string? RuntimeType { get; init; }
        public string? SerializedTypeId { get; init; }
        public int? ObjectId { get; init; }
        public required string Message { get; init; }
    }

    private sealed record SerializableMember(MemberInfo Member, Type ValueType, bool CanWrite);

    private sealed class DiscoveryContext
    {
        public Dictionary<Guid, string> StableFieldOwners { get; } = [];
        public HashSet<Type> ActiveTypes { get; } = [];
        public Dictionary<Guid, ReferenceTypeDocument> ReferenceTypes { get; } = [];
        public HashSet<Type> ActiveReferenceTypes { get; } = [];
        public IReadOnlyDictionary<Guid, Type>? SerializedTypeRegistry { get; set; }
    }

    internal static string Export()
    {
        _ = ManagedReferenceGraphCodec.TypeRegistry(typeof(ManagedAssetMetadata), nameof(ManagedAssetMetadata));
        var document = new ExportDocument();
        AssemblyLoadContext? loadContext = AssemblyLoadContext.GetLoadContext(typeof(ManagedAssetMetadata).Assembly);
        IEnumerable<Assembly> loadedAssemblies = loadContext?.Assemblies ?? AppDomain.CurrentDomain.GetAssemblies();
        Type[] loadedTypes = loadedAssemblies
            .Where(assembly => !assembly.IsDynamic)
            .SelectMany(SafeTypes)
            .ToArray();
        IEnumerable<Type> candidates = loadedTypes
            .Where(type => type.Assembly != typeof(ScriptableObject).Assembly &&
                           type != typeof(ScriptableObject) && !type.IsAbstract &&
                           typeof(ScriptableObject).IsAssignableFrom(type))
            .OrderBy(type => type.FullName, StringComparer.Ordinal);

        foreach (Type candidate in candidates)
        {
            try
            {
                document.Types.Add(DescribeType(candidate));
            }
            catch (ManagedSerializationException exception)
            {
                document.Diagnostics.Add(new DiagnosticDocument
                {
                    Code = exception.Code,
                    Phase = exception.Phase,
                    Owner = exception.Owner,
                    RootField = exception.RootField,
                    TypeName = candidate.FullName ?? candidate.Name,
                    FieldPath = exception.FieldPath,
                    DeclaredType = exception.DeclaredType.FullName ?? exception.DeclaredType.Name,
                    RuntimeType = exception.RuntimeType?.FullName,
                    SerializedTypeId = exception.SerializedTypeId,
                    ObjectId = exception.ObjectId,
                    Message = exception.Message,
                });
            }
            catch (Exception exception)
            {
                document.Diagnostics.Add(new DiagnosticDocument
                {
                    Code = "KEIRE-MANAGED-SERIALIZATION-0001",
                    Phase = "metadata",
                    Owner = candidate.FullName ?? candidate.Name,
                    RootField = candidate.Name,
                    TypeName = candidate.FullName ?? candidate.Name,
                    FieldPath = candidate.FullName ?? candidate.Name,
                    DeclaredType = candidate.FullName ?? candidate.Name,
                    Message = exception.Message,
                });
            }
        }

        IEnumerable<Type> behaviourCandidates = loadedTypes
            .Where(type => type.Assembly != typeof(Behaviour).Assembly && !type.IsAbstract &&
                           typeof(Behaviour).IsAssignableFrom(type))
            .OrderBy(type => type.FullName, StringComparer.Ordinal);
        foreach (Type candidate in behaviourCandidates)
        {
            try
            {
                BehaviourGraphDocument? behaviour = DescribeBehaviourGraphs(candidate);
                if (behaviour is not null)
                    document.Behaviours.Add(behaviour);
            }
            catch (ManagedSerializationException exception)
            {
                document.Diagnostics.Add(new DiagnosticDocument
                {
                    Code = exception.Code,
                    Phase = exception.Phase,
                    Owner = exception.Owner,
                    RootField = exception.RootField,
                    TypeName = candidate.FullName ?? candidate.Name,
                    FieldPath = exception.FieldPath,
                    DeclaredType = exception.DeclaredType.FullName ?? exception.DeclaredType.Name,
                    RuntimeType = exception.RuntimeType?.FullName,
                    SerializedTypeId = exception.SerializedTypeId,
                    ObjectId = exception.ObjectId,
                    Message = exception.Message,
                });
            }
            catch (Exception exception)
            {
                document.Diagnostics.Add(new DiagnosticDocument
                {
                    Code = "KEIRE-MANAGED-SERIALIZATION-0001",
                    Phase = "metadata",
                    Owner = candidate.FullName ?? candidate.Name,
                    RootField = candidate.Name,
                    TypeName = candidate.FullName ?? candidate.Name,
                    FieldPath = candidate.FullName ?? candidate.Name,
                    DeclaredType = candidate.FullName ?? candidate.Name,
                    Message = exception.Message,
                });
            }
        }

        document.Types.Sort((left, right) => string.CompareOrdinal(left.FullName, right.FullName));
        document.Behaviours.Sort((left, right) => string.CompareOrdinal(left.FullName, right.FullName));
        document.Diagnostics.Sort((left, right) =>
        {
            int type = string.CompareOrdinal(left.TypeName, right.TypeName);
            return type != 0 ? type : string.CompareOrdinal(left.Message, right.Message);
        });
        return JsonSerializer.Serialize(document, new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        });
    }

    private static TypeDocument DescribeType(Type type)
    {
        StableAssetTypeIdAttribute stableType = type.GetCustomAttribute<StableAssetTypeIdAttribute>(false) ??
            throw Invalid(type, "concrete ScriptableObject types require StableAssetTypeId");
        if (type.ContainsGenericParameters)
            throw Invalid(type, "open generic ScriptableObject types are not supported");
        if (type.GetConstructor(BindingFlags.Instance | BindingFlags.Public, null, Type.EmptyTypes, null) is null)
            throw Invalid(type, "a public parameterless constructor is required");

        CreateAssetMenuAttribute? menu = type.GetCustomAttribute<CreateAssetMenuAttribute>(false);
        Type? baseType = type.BaseType;
        StableAssetTypeIdAttribute? stableBase =
            baseType is not null && baseType != typeof(ScriptableObject)
                ? baseType.GetCustomAttribute<StableAssetTypeIdAttribute>(false)
                : null;
        var result = new TypeDocument
        {
            StableTypeId = stableType.Id.ToString("D"),
            FullName = type.FullName ?? type.Name,
            DisplayName = SplitName(type.Name),
            BaseTypeId = stableBase?.Id.ToString("D"),
            MenuPath = menu?.MenuName ?? string.Empty,
            DefaultFileName = string.IsNullOrWhiteSpace(menu?.FileName) ? type.Name : menu!.FileName,
        };

        var context = new DiscoveryContext();
        context.ActiveTypes.Add(type);
        foreach (SerializableMember member in SerializableMembers(type, stopAtScriptableObject: true))
            result.Properties.Add(DescribeMember(type, member, context, 0, stableType.Id));
        result.ReferenceTypes.AddRange(context.ReferenceTypes.Values.OrderBy(value => value.FullName,
                                                                              StringComparer.Ordinal));
        return result;
    }

    private static BehaviourGraphDocument? DescribeBehaviourGraphs(Type type)
    {
        StableComponentIdAttribute? stableType = type.GetCustomAttribute<StableComponentIdAttribute>(false);
        if (stableType is null)
            return null;
        var result = new BehaviourGraphDocument { FullName = type.FullName ?? type.Name };
        var context = new DiscoveryContext();
        context.ActiveTypes.Add(type);
        foreach (SerializableMember member in SerializableMembers(type, stopAtScriptableObject: false))
        {
            if (member.Member.IsDefined(typeof(SerializeReferenceAttribute), true) ||
                member.ValueType.IsArray || IsList(member.ValueType, out _) ||
                IsDictionary(member.ValueType, out _, out _))
            {
                result.Properties.Add(DescribeMember(type, member, context, 0, stableType.Id));
            }
        }
        CollectNestedBehaviourCollections(type, type, string.Empty, context, 0, stableType.Id, result.Properties,
                                          [type]);
        if (result.Properties.Count == 0)
            return null;
        result.ReferenceTypes.AddRange(context.ReferenceTypes.Values.OrderBy(value => value.FullName,
                                                                              StringComparer.Ordinal));
        return result;
    }

    private static void CollectNestedBehaviourCollections(Type ownerType, Type containerType, string prefix,
                                                           DiscoveryContext context, int depth, Guid parentId,
                                                           List<PropertyDocument> destination,
                                                           HashSet<Type> active)
    {
        if (depth >= MaximumDepth)
            return;
        foreach (SerializableMember member in SerializableMembers(containerType, stopAtScriptableObject: false))
        {
            if (member.Member.IsDefined(typeof(SerializeReferenceAttribute), true))
                continue;
            Type valueType = member.ValueType;
            if (valueType.IsArray || IsList(valueType, out _) || IsDictionary(valueType, out _, out _))
            {
                if (containerType == ownerType)
                    continue;
                PropertyDocument descriptor = DescribeMember(ownerType, member, context, depth, parentId);
                descriptor.Name = prefix + descriptor.Name;
                destination.Add(descriptor);
                continue;
            }
            bool serializable = valueType.IsDefined(typeof(SerializableAttribute), false) ||
                                valueType.IsDefined(typeof(SerializableTypeAttribute), false);
            if (!serializable || valueType == typeof(string) || typeof(EngineObject).IsAssignableFrom(valueType) ||
                typeof(KeireEventBase).IsAssignableFrom(valueType) ||
                !active.Add(valueType))
            {
                continue;
            }
            try
            {
                Guid stableId = ManagedStableIdentity.Field(member.Member, parentId);
                CollectNestedBehaviourCollections(ownerType, valueType, prefix + member.Member.Name + ".", context,
                                                  depth + 1, stableId, destination, active);
            }
            finally
            {
                active.Remove(valueType);
            }
        }
    }

    private static PropertyDocument DescribeMember(Type ownerType, SerializableMember member,
                                                   DiscoveryContext context, int depth, Guid parentId,
                                                   bool graphContext = false)
    {
        if (depth > MaximumDepth)
            throw Invalid(ownerType, $"{ownerType.FullName}.{member.Member.Name}", member.ValueType,
                          $"member exceeds the supported nesting depth of {MaximumDepth}");
        Guid stableId = ManagedStableIdentity.Field(member.Member, parentId);
        string stableOwner = $"{member.Member.Module.ModuleVersionId:D}:{member.Member.MetadataToken}";
        if (context.StableFieldOwners.TryGetValue(stableId, out string? existingOwner) && existingOwner != stableOwner)
            throw Invalid(ownerType, $"{ownerType.FullName}.{member.Member.Name}", member.ValueType,
                          $"stable field ID '{stableId:D}' is duplicated");
        context.StableFieldOwners[stableId] = stableOwner;

        Type valueType = member.ValueType;
        string memberPath = $"{ownerType.FullName}.{member.Member.Name}";
        bool referenceGraph = member.Member.IsDefined(typeof(SerializeReferenceAttribute), true) ||
                              graphContext && IsGraphNodeSlot(valueType);
        var result = new PropertyDocument
        {
            StableFieldId = stableId.ToString("D"),
            Name = member.Member.Name,
            DisplayName = member.Member.GetCustomAttribute<InspectorNameAttribute>(true)?.Name ??
                          SplitName(member.Member.Name.TrimStart('_')),
            ManagedTypeName = valueType.FullName ?? valueType.Name,
            Kind = PropertyKind(valueType),
            ReadOnly = !member.CanWrite || member.Member.IsDefined(typeof(ReadOnlyInInspectorAttribute), true),
            Hidden = member.Member.IsDefined(typeof(HideInInspectorAttribute), true),
            Header = member.Member.GetCustomAttribute<HeaderAttribute>(true)?.Text ?? string.Empty,
            Tooltip = member.Member.GetCustomAttribute<TooltipAttribute>(true)?.Text ?? string.Empty,
            ReferenceGraph = referenceGraph,
        };
        RangeAttribute? range = member.Member.GetCustomAttribute<RangeAttribute>(true);
        MinAttribute? minimum = member.Member.GetCustomAttribute<MinAttribute>(true);
        MaxAttribute? maximum = member.Member.GetCustomAttribute<MaxAttribute>(true);
        InspectorStepAttribute? step = member.Member.GetCustomAttribute<InspectorStepAttribute>(true);
        MultilineAttribute? multiline = member.Member.GetCustomAttribute<MultilineAttribute>(true);
        if (range is not null && (minimum is not null || maximum is not null))
            throw Invalid(ownerType, $"member '{member.Member.Name}' combines Range with Min or Max");
        if ((range is not null || minimum is not null || maximum is not null || step is not null) &&
            !IsNumeric(valueType))
        {
            throw Invalid(ownerType,
                          $"member '{member.Member.Name}' uses numeric Inspector attributes on a non-numeric type");
        }
        if (multiline is not null && valueType != typeof(string))
            throw Invalid(ownerType, $"member '{member.Member.Name}' uses Multiline on a non-string type");
        if (range is not null)
        {
            result.Minimum = range.Minimum;
            result.Maximum = range.Maximum;
            result.Slider = true;
        }
        else
        {
            result.Minimum = minimum?.Minimum;
            result.Maximum = maximum?.Maximum;
        }
        if (result.Minimum is not null && result.Maximum is not null && result.Minimum > result.Maximum)
            throw Invalid(ownerType, $"member '{member.Member.Name}' has unordered Inspector bounds");
        result.Step = step?.Step ?? (IsInteger(valueType) ? 1.0 : 0.1);
        result.TextLines = multiline?.Lines ?? 1;

        if (referenceGraph && IsGraphObjectSlot(valueType))
        {
            RegisterReferenceChoices(ownerType, valueType, memberPath, result, context, depth + 1);
            return result;
        }

        if (IsAssetObject(valueType, out Type? referencedType))
        {
            StableAssetTypeIdAttribute? referencedId =
                referencedType.GetCustomAttribute<StableAssetTypeIdAttribute>(false);
            if (typeof(ScriptableObject).IsAssignableFrom(referencedType))
            {
                result.ExpectedAssetType = ManagedDataAssetTypeId;
                result.ExpectedManagedType = referencedId?.Id.ToString("D");
            }
            else
            {
                result.ExpectedAssetType = referencedId?.Id.ToString("D") ??
                    throw Invalid(ownerType,
                                  $"asset field '{member.Member.Name}' targets a type without StableAssetTypeId");
            }
            return result;
        }

        if (valueType.IsArray)
        {
            if (!valueType.IsSZArray)
                throw Invalid(ownerType, $"member '{member.Member.Name}' uses a non-SZ array");
            result.Children.Add(DescribeElement(ownerType, stableId, valueType.GetElementType()!, context, depth + 1,
                                                graphContext));
            return result;
        }
        if (IsList(valueType, out Type? elementType))
        {
            result.Children.Add(DescribeElement(ownerType, stableId, elementType, context, depth + 1, graphContext));
            return result;
        }
        if (IsDictionary(valueType, out Type? keyType, out Type? dictionaryValueType))
        {
            ValidateDictionaryKeyType(ownerType, keyType, memberPath);
            result.Children.Add(DescribeDictionaryPart(ownerType, stableId, "Key", keyType, context, depth + 1,
                                                       graphContext));
            result.Children.Add(DescribeDictionaryPart(ownerType, stableId, "Value", dictionaryValueType, context,
                                                       depth + 1, graphContext));
            return result;
        }
        if (result.Kind == 11)
        {
            DescribeNestedType(ownerType, valueType, context, depth + 1, stableId, result.Children, graphContext);
            return result;
        }
        return result;
    }

    private static PropertyDocument DescribeElement(Type ownerType, Guid parentId, Type elementType,
                                                    DiscoveryContext context, int depth, bool graphContext = false)
    {
        if (typeof(Entity).IsAssignableFrom(elementType) || typeof(Component).IsAssignableFrom(elementType))
            throw Invalid(ownerType, "persistent managed assets cannot reference scene objects");
        Guid elementId = ManagedStableIdentity.Derive(parentId, "element");
        var result = new PropertyDocument
        {
            StableFieldId = elementId.ToString("D"),
            Name = "Element",
            DisplayName = "Element",
            ManagedTypeName = elementType.FullName ?? elementType.Name,
            Kind = PropertyKind(elementType),
            Header = string.Empty,
            Tooltip = string.Empty,
            ReferenceGraph = graphContext && IsGraphNodeSlot(elementType),
        };
        if (result.ReferenceGraph && IsGraphObjectSlot(elementType))
        {
            RegisterReferenceChoices(ownerType, elementType, $"{ownerType.FullName}.Element", result, context,
                                     depth + 1);
            return result;
        }
        if (IsAssetObject(elementType, out Type? referencedType))
        {
            StableAssetTypeIdAttribute? referencedId =
                referencedType.GetCustomAttribute<StableAssetTypeIdAttribute>(false);
            if (typeof(ScriptableObject).IsAssignableFrom(referencedType))
            {
                result.ExpectedAssetType = ManagedDataAssetTypeId;
                result.ExpectedManagedType = referencedId?.Id.ToString("D");
            }
            else
            {
                result.ExpectedAssetType = referencedId?.Id.ToString("D") ??
                    throw Invalid(ownerType, "an asset element targets a type without StableAssetTypeId");
            }
        }
        else if (elementType.IsArray)
        {
            if (!elementType.IsSZArray)
                throw Invalid(ownerType, "nested non-SZ arrays are unsupported");
            result.Children.Add(DescribeElement(ownerType, elementId, elementType.GetElementType()!, context,
                                                depth + 1, graphContext));
        }
        else if (IsList(elementType, out Type? nestedElement))
        {
            result.Children.Add(DescribeElement(ownerType, elementId, nestedElement, context, depth + 1,
                                                graphContext));
        }
        else if (IsDictionary(elementType, out Type? keyType, out Type? valueType))
        {
            ValidateDictionaryKeyType(ownerType, keyType, $"{ownerType.FullName}.Element");
            result.Children.Add(DescribeDictionaryPart(ownerType, elementId, "Key", keyType, context, depth + 1,
                                                       graphContext));
            result.Children.Add(DescribeDictionaryPart(ownerType, elementId, "Value", valueType, context, depth + 1,
                                                       graphContext));
        }
        else if (result.Kind == 11)
        {
            DescribeNestedType(ownerType, elementType, context, depth + 1, elementId, result.Children, graphContext);
        }
        return result;
    }

    private static PropertyDocument DescribeDictionaryPart(Type ownerType, Guid parentId, string name, Type valueType,
                                                            DiscoveryContext context, int depth,
                                                            bool graphContext = false)
    {
        Guid stableId = ManagedStableIdentity.Derive(parentId, name.ToLowerInvariant());
        var result = new PropertyDocument
        {
            StableFieldId = stableId.ToString("D"),
            Name = name,
            DisplayName = name,
            ManagedTypeName = valueType.FullName ?? valueType.Name,
            Kind = PropertyKind(valueType),
            Header = string.Empty,
            Tooltip = string.Empty,
            ReferenceGraph = graphContext && IsGraphNodeSlot(valueType),
        };
        if (result.ReferenceGraph && IsGraphObjectSlot(valueType))
        {
            RegisterReferenceChoices(ownerType, valueType, $"{ownerType.FullName}.{name}", result, context,
                                     depth + 1);
            return result;
        }
        if (IsAssetObject(valueType, out Type? referencedType))
        {
            StableAssetTypeIdAttribute? referencedId =
                referencedType.GetCustomAttribute<StableAssetTypeIdAttribute>(false);
            result.ExpectedAssetType = typeof(ScriptableObject).IsAssignableFrom(referencedType)
                ? ManagedDataAssetTypeId
                : referencedId?.Id.ToString("D") ??
                  throw Invalid(ownerType, $"{ownerType.FullName}.{name}", valueType,
                                "an asset dictionary value targets a type without StableAssetTypeId");
            if (typeof(ScriptableObject).IsAssignableFrom(referencedType))
                result.ExpectedManagedType = referencedId?.Id.ToString("D");
        }
        else if (valueType.IsArray)
        {
            if (!valueType.IsSZArray)
                throw Invalid(ownerType, $"{ownerType.FullName}.{name}", valueType,
                              "nested non-SZ arrays are unsupported");
            result.Children.Add(DescribeElement(ownerType, stableId, valueType.GetElementType()!, context, depth + 1,
                                                graphContext));
        }
        else if (IsList(valueType, out Type? elementType))
        {
            result.Children.Add(DescribeElement(ownerType, stableId, elementType, context, depth + 1, graphContext));
        }
        else if (IsDictionary(valueType, out Type? keyType, out Type? dictionaryValue))
        {
            ValidateDictionaryKeyType(ownerType, keyType, $"{ownerType.FullName}.{name}");
            result.Children.Add(DescribeDictionaryPart(ownerType, stableId, "Key", keyType, context, depth + 1,
                                                       graphContext));
            result.Children.Add(DescribeDictionaryPart(ownerType, stableId, "Value", dictionaryValue, context,
                                                       depth + 1, graphContext));
        }
        else if (result.Kind == 11)
        {
            DescribeNestedType(ownerType, valueType, context, depth + 1, stableId, result.Children, graphContext);
        }
        return result;
    }

    private static void DescribeNestedType(Type ownerType, Type valueType, DiscoveryContext context, int depth,
                                           Guid parentId, List<PropertyDocument> destination,
                                           bool graphContext = false)
    {
        if (!valueType.IsDefined(typeof(SerializableAttribute), false) &&
            !valueType.IsDefined(typeof(SerializableTypeAttribute), false))
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' requires Serializable");
        if (valueType.IsAbstract || valueType.IsInterface)
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' cannot be abstract or an interface");
        if (!context.ActiveTypes.Add(valueType))
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' creates a cyclic graph");
        try
        {
            foreach (SerializableMember child in SerializableMembers(valueType, stopAtScriptableObject: false))
                destination.Add(DescribeMember(ownerType, child, context, depth, parentId, graphContext));
        }
        finally
        {
            context.ActiveTypes.Remove(valueType);
        }
    }

    private static SerializableMember[] SerializableMembers(Type type, bool stopAtScriptableObject)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type;
             current is not null && current != typeof(object) &&
             (!stopAtScriptableObject || current != typeof(ScriptableObject));
             current = current.BaseType)
        {
            hierarchy.Push(current);
        }

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
                if (result.Count >= MaximumFieldsPerType)
                {
                    string owner = type.FullName ?? type.Name;
                    throw Invalid(type, $"{field.DeclaringType?.FullName ?? owner}.{field.Name}", field.FieldType,
                                  $"serialized types cannot exceed {MaximumFieldsPerType} serialized fields",
                                  field.Name);
                }
                result.Add(new SerializableMember(field, field.FieldType, CanWrite: true));
            }
        }
        return result.ToArray();
    }

    private static int PropertyKind(Type type)
    {
        if ((typeof(IDictionary).IsAssignableFrom(type) ||
             type.GetInterfaces().Any(candidate =>
                 candidate.IsGenericType && candidate.GetGenericTypeDefinition() == typeof(IDictionary<,>))) &&
            !IsDictionary(type, out _, out _))
        {
            throw Invalid(type, "only exact Dictionary<TKey, TValue> fields are supported");
        }
        if (type == typeof(bool))
            return 0;
        if (type == typeof(sbyte) || type == typeof(short) || type == typeof(int) || type == typeof(long) ||
            type == typeof(char))
            return 1;
        if (type == typeof(byte) || type == typeof(ushort) || type == typeof(uint) || type == typeof(ulong))
            return 2;
        if (type == typeof(float) || type == typeof(double) || type == typeof(decimal))
            return 3;
        if (type == typeof(string) || type == typeof(Guid))
            return 4;
        if (type.IsEnum)
            return 5;
        if (type == typeof(Vector2))
            return 6;
        if (type == typeof(Vector3))
            return 7;
        if (type == typeof(Vector4))
            return 8;
        if (type == typeof(Quaternion))
            return 9;
        if (type == typeof(Color))
            return 10;
        if (type.IsArray)
            return 12;
        if (IsList(type, out _))
            return 13;
        if (IsAssetObject(type, out _))
            return 14;
        if (IsDictionary(type, out _, out _))
            return 15;
        if (typeof(Entity).IsAssignableFrom(type) || typeof(Component).IsAssignableFrom(type))
            throw new InvalidOperationException("persistent managed assets cannot reference scene objects");
        return 11;
    }

    private static bool IsInteger(Type type) =>
        type == typeof(sbyte) || type == typeof(byte) || type == typeof(short) || type == typeof(ushort) ||
        type == typeof(int) || type == typeof(uint) || type == typeof(long) || type == typeof(ulong) ||
        type == typeof(char);

    private static bool IsNumeric(Type type) =>
        IsInteger(type) || type == typeof(float) || type == typeof(double) || type == typeof(decimal);

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

    private static bool IsAssetObject(Type type, out Type referencedType)
    {
        if (typeof(Asset).IsAssignableFrom(type))
        {
            referencedType = type;
            return true;
        }
        referencedType = null!;
        return false;
    }

    private static void ValidateDictionaryKeyType(Type ownerType, Type valueType, string path)
    {
        if (valueType == typeof(string) || valueType == typeof(bool) || valueType == typeof(char) ||
            valueType == typeof(sbyte) || valueType == typeof(byte) || valueType == typeof(short) ||
            valueType == typeof(ushort) || valueType == typeof(int) || valueType == typeof(uint) ||
            valueType == typeof(long) || valueType == typeof(ulong) || valueType == typeof(Guid) || valueType.IsEnum)
        {
            return;
        }
        throw Invalid(ownerType, $"{path}[key]", valueType,
                      "dictionary keys must be strings, booleans, characters, integers, enums, or GUIDs");
    }

    private static bool IsGraphObjectSlot(Type type) =>
        !type.IsValueType && type != typeof(string) && !typeof(EngineObject).IsAssignableFrom(type) &&
        !type.IsArray && !IsList(type, out _) && !IsDictionary(type, out _, out _);

    private static bool IsGraphNodeSlot(Type type) =>
        IsGraphObjectSlot(type) || type.IsArray || IsList(type, out _) || IsDictionary(type, out _, out _);

    private static void RegisterReferenceChoices(Type ownerType, Type declaredType, string path,
                                                 PropertyDocument property, DiscoveryContext context, int depth)
    {
        if (depth > MaximumDepth)
            throw Invalid(ownerType, path, declaredType, "reference type discovery exceeds the supported depth");
        context.SerializedTypeRegistry ??= ManagedReferenceGraphCodec.TypeRegistry(ownerType, path);
        Type[] choices = ReferenceTypeChoices(ownerType, declaredType, path, context.SerializedTypeRegistry);

        foreach (Type choice in choices)
        {
            StableSerializedTypeIdAttribute stable =
                choice.GetCustomAttribute<StableSerializedTypeIdAttribute>(false)!;
            property.ReferenceTypeChoices.Add(stable.Id.ToString("D"));
            if (context.ReferenceTypes.ContainsKey(stable.Id))
                continue;

            var descriptor = new ReferenceTypeDocument
            {
                StableTypeId = stable.Id.ToString("D"),
                FullName = choice.FullName ?? choice.Name,
                DisplayName = SplitName(choice.Name),
            };
            context.ReferenceTypes.Add(stable.Id, descriptor);
            if (!context.ActiveReferenceTypes.Add(choice))
                continue;
            try
            {
                foreach (SerializableMember member in ReferenceTypeMembers(ownerType, choice, path))
                    descriptor.Properties.Add(DescribeMember(choice, member, context, depth, stable.Id,
                                                             graphContext: true));
            }
            finally
            {
                context.ActiveReferenceTypes.Remove(choice);
            }
        }
    }

    internal static void ValidateReferenceTypeChoiceLimitForTests(Type ownerType, Type declaredType, string path,
                                                                   IReadOnlyDictionary<Guid, Type> registry) =>
        _ = ReferenceTypeChoices(ownerType, declaredType, path, registry);

    private static Type[] ReferenceTypeChoices(Type ownerType, Type declaredType, string path,
                                               IReadOnlyDictionary<Guid, Type> registry)
    {
        Type[] choices = registry.Values.Where(declaredType.IsAssignableFrom)
            .OrderBy(type => type.FullName, StringComparer.Ordinal).ToArray();
        if (choices.Length > MaximumReferenceTypeChoices)
        {
            throw Invalid(ownerType, path, declaredType,
                          $"reference slots cannot expose more than {MaximumReferenceTypeChoices} concrete types");
        }
        return choices;
    }

    internal static void ValidateReferenceTypeFieldLimitForTests(Type type)
    {
        _ = ReferenceTypeMembers(type, type, type.FullName ?? type.Name);
    }

    private static SerializableMember[] ReferenceTypeMembers(Type ownerType, Type type, string path)
    {
        try
        {
            return SerializableMembers(type, stopAtScriptableObject: false);
        }
        catch (ManagedSerializationException exception)
        {
            throw exception.WithContext("metadata", ownerType.FullName ?? ownerType.Name, exception.RootField);
        }
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

    private static string SplitName(string name)
    {
        var result = new StringBuilder(name.Length + 8);
        for (int index = 0; index < name.Length; ++index)
        {
            char character = name[index];
            if (index > 0 && char.IsUpper(character) && !char.IsUpper(name[index - 1]))
                result.Append(' ');
            result.Append(character);
        }
        return result.ToString();
    }

    private static InvalidOperationException Invalid(Type type, string reason) =>
        new($"Managed asset type '{type.FullName ?? type.Name}' is invalid: {reason}.");

    private static ManagedSerializationException Invalid(Type ownerType, string path, Type declaredType, string reason,
                                                         string? rootField = null) =>
        new("KEIRE-MANAGED-SERIALIZATION-0001", path, declaredType, null,
            $"managed asset type '{ownerType.FullName ?? ownerType.Name}': {reason}", phase: "metadata",
            owner: ownerType.FullName ?? ownerType.Name, rootField: rootField ?? path);
}
