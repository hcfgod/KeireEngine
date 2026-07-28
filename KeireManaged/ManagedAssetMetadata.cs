using System.Collections;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Keire;

internal static class ManagedAssetMetadata
{
    private const string ManagedDataAssetTypeId = "4b454952-454d-4441-5441-415353455401";
    private const int MaximumDepth = 32;

    private sealed class ExportDocument
    {
        public int SchemaVersion { get; init; } = 1;
        public List<TypeDocument> Types { get; } = [];
        public List<DiagnosticDocument> Diagnostics { get; } = [];
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
    }

    private sealed class PropertyDocument
    {
        public required string StableFieldId { get; init; }
        public required string Name { get; init; }
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
    }

    private sealed class DiagnosticDocument
    {
        public required string TypeName { get; init; }
        public required string Message { get; init; }
    }

    private sealed record SerializableMember(MemberInfo Member, Type ValueType, bool CanWrite);

    private sealed class DiscoveryContext
    {
        public HashSet<Guid> StableFieldIds { get; } = [];
        public HashSet<Type> ActiveTypes { get; } = [];
    }

    internal static string Export()
    {
        var document = new ExportDocument();
        IEnumerable<Type> candidates = AppDomain.CurrentDomain.GetAssemblies()
            .Where(assembly => !assembly.IsDynamic)
            .SelectMany(SafeTypes)
            .Where(type => type != typeof(ScriptableObject) && !type.IsAbstract &&
                           typeof(ScriptableObject).IsAssignableFrom(type))
            .OrderBy(type => type.FullName, StringComparer.Ordinal);

        foreach (Type candidate in candidates)
        {
            try
            {
                document.Types.Add(DescribeType(candidate));
            }
            catch (Exception exception)
            {
                document.Diagnostics.Add(new DiagnosticDocument
                {
                    TypeName = candidate.FullName ?? candidate.Name,
                    Message = exception.Message,
                });
            }
        }

        document.Types.Sort((left, right) => string.CompareOrdinal(left.FullName, right.FullName));
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
            result.Properties.Add(DescribeMember(type, member, context, 0));
        return result;
    }

    private static PropertyDocument DescribeMember(Type ownerType, SerializableMember member,
                                                   DiscoveryContext context, int depth)
    {
        if (depth > MaximumDepth)
            throw Invalid(ownerType, $"member '{member.Member.Name}' exceeds the supported nesting depth");
        StableFieldIdAttribute stable = member.Member.GetCustomAttribute<StableFieldIdAttribute>(true) ??
            throw Invalid(ownerType, $"serialized member '{member.Member.Name}' requires StableFieldId");
        if (!context.StableFieldIds.Add(stable.Id))
            throw Invalid(ownerType, $"stable field ID '{stable.Id:D}' is duplicated");

        Type valueType = member.ValueType;
        var result = new PropertyDocument
        {
            StableFieldId = stable.Id.ToString("D"),
            Name = member.Member.Name,
            DisplayName = SplitName(member.Member.Name.TrimStart('_')),
            ManagedTypeName = valueType.FullName ?? valueType.Name,
            Kind = PropertyKind(valueType),
            ReadOnly = !member.CanWrite || member.Member.IsDefined(typeof(ReadOnlyInInspectorAttribute), true),
            Hidden = member.Member.IsDefined(typeof(HideInInspectorAttribute), true),
            Header = member.Member.GetCustomAttribute<HeaderAttribute>(true)?.Text ?? string.Empty,
            Tooltip = member.Member.GetCustomAttribute<TooltipAttribute>(true)?.Text ?? string.Empty,
        };
        if (member.Member.GetCustomAttribute<RangeAttribute>(true) is { } range)
        {
            result.Minimum = range.Minimum;
            result.Maximum = range.Maximum;
        }

        if (IsAssetReference(valueType, out Type? referencedType))
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
                                  $"asset reference '{member.Member.Name}' targets a type without StableAssetTypeId");
            }
            return result;
        }

        if (valueType.IsArray)
        {
            if (!valueType.IsSZArray)
                throw Invalid(ownerType, $"member '{member.Member.Name}' uses a non-SZ array");
            result.Children.Add(DescribeElement(ownerType, stable.Id, valueType.GetElementType()!, context, depth + 1));
            return result;
        }
        if (IsList(valueType, out Type? elementType))
        {
            result.Children.Add(DescribeElement(ownerType, stable.Id, elementType, context, depth + 1));
            return result;
        }
        if (result.Kind == 11)
        {
            DescribeNestedType(ownerType, valueType, context, depth + 1, result.Children);
            return result;
        }
        return result;
    }

    private static PropertyDocument DescribeElement(Type ownerType, Guid parentId, Type elementType,
                                                    DiscoveryContext context, int depth)
    {
        RejectUnsupportedContainer(ownerType, elementType);
        if (typeof(ScriptableObject).IsAssignableFrom(elementType))
            throw Invalid(ownerType, "inline ScriptableObjects are unsupported; use AssetReference<T>");
        Guid elementId = DerivedId(parentId, "element");
        var result = new PropertyDocument
        {
            StableFieldId = elementId.ToString("D"),
            Name = "Element",
            DisplayName = "Element",
            ManagedTypeName = elementType.FullName ?? elementType.Name,
            Kind = PropertyKind(elementType),
            Header = string.Empty,
            Tooltip = string.Empty,
        };
        if (IsAssetReference(elementType, out Type? referencedType))
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
                    throw Invalid(ownerType, "an AssetReference element targets a type without StableAssetTypeId");
            }
        }
        else if (elementType.IsArray)
        {
            if (!elementType.IsSZArray)
                throw Invalid(ownerType, "nested non-SZ arrays are unsupported");
            result.Children.Add(DescribeElement(ownerType, elementId, elementType.GetElementType()!, context, depth + 1));
        }
        else if (IsList(elementType, out Type? nestedElement))
        {
            result.Children.Add(DescribeElement(ownerType, elementId, nestedElement, context, depth + 1));
        }
        else if (result.Kind == 11)
        {
            DescribeNestedType(ownerType, elementType, context, depth + 1, result.Children);
        }
        return result;
    }

    private static void DescribeNestedType(Type ownerType, Type valueType, DiscoveryContext context, int depth,
                                           List<PropertyDocument> destination)
    {
        if (!valueType.IsDefined(typeof(SerializableTypeAttribute), false))
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' requires SerializableType");
        if (valueType.IsAbstract || valueType.IsInterface)
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' cannot be abstract or an interface");
        if (!context.ActiveTypes.Add(valueType))
            throw Invalid(ownerType, $"inline type '{valueType.FullName}' creates a cyclic graph");
        try
        {
            foreach (SerializableMember child in SerializableMembers(valueType, stopAtScriptableObject: false))
                destination.Add(DescribeMember(ownerType, child, context, depth));
        }
        finally
        {
            context.ActiveTypes.Remove(valueType);
        }
    }

    private static IEnumerable<SerializableMember> SerializableMembers(Type type, bool stopAtScriptableObject)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type;
             current is not null && current != typeof(object) &&
             (!stopAtScriptableObject || current != typeof(ScriptableObject));
             current = current.BaseType)
        {
            hierarchy.Push(current);
        }

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
                RejectUnsupportedContainer(type, field.FieldType);
                yield return new SerializableMember(field, field.FieldType, CanWrite: true);
            }
            foreach (PropertyInfo property in current.GetProperties(flags).OrderBy(value => value.MetadataToken))
            {
                bool serialized = (property.GetMethod?.IsPublic == true && property.SetMethod?.IsPublic == true) ||
                                  property.IsDefined(typeof(SerializeFieldAttribute), true);
                if (!serialized || property.GetIndexParameters().Length != 0 || property.GetMethod is null ||
                    property.SetMethod is null)
                    continue;
                RejectUnsupportedContainer(type, property.PropertyType);
                yield return new SerializableMember(property, property.PropertyType,
                                                      CanWrite: property.SetMethod is not null);
            }
        }
    }

    private static int PropertyKind(Type type)
    {
        RejectUnsupportedContainer(type, type);
        if (type == typeof(bool))
            return 0;
        if (type == typeof(sbyte) || type == typeof(short) || type == typeof(int) || type == typeof(long) ||
            type == typeof(char))
            return 1;
        if (type == typeof(byte) || type == typeof(ushort) || type == typeof(uint) || type == typeof(ulong))
            return 2;
        if (type == typeof(float) || type == typeof(double) || type == typeof(decimal))
            return 3;
        if (type == typeof(string))
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
        if (IsAssetReference(type, out _))
            return 14;
        if (typeof(ScriptableObject).IsAssignableFrom(type))
            throw new InvalidOperationException("inline ScriptableObjects are unsupported; use AssetReference<T>");
        return 11;
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

    private static bool IsAssetReference(Type type, out Type referencedType)
    {
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(AssetReference<>))
        {
            referencedType = type.GetGenericArguments()[0];
            return true;
        }
        referencedType = null!;
        return false;
    }

    private static void RejectUnsupportedContainer(Type ownerType, Type valueType)
    {
        if (typeof(IDictionary).IsAssignableFrom(valueType) ||
            valueType.GetInterfaces().Any(candidate =>
                candidate.IsGenericType && candidate.GetGenericTypeDefinition() == typeof(IDictionary<,>)))
            throw Invalid(ownerType, $"member type '{valueType.FullName}' uses an unsupported dictionary");
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

    private static Guid DerivedId(Guid parent, string suffix)
    {
        byte[] value = SHA256.HashData(Encoding.UTF8.GetBytes($"{parent:D}/{suffix}"));
        value[6] = (byte)((value[6] & 0x0F) | 0x50);
        value[8] = (byte)((value[8] & 0x3F) | 0x80);
        return new Guid(value.AsSpan(0, 16));
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
}
