using System.Collections;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Keire;

internal static class ManagedObjectSerializer
{
    private const int MaximumDepth = 32;
    private const int MaximumFieldsPerType = 1_024;

    private interface ISerializableMember
    {
        string Name { get; }
        Type ValueType { get; }
        bool PreserveReferences { get; }
        object? GetValue(object owner);
        void SetValue(object owner, object? value);
    }

    private sealed class SerializableField(FieldInfo fieldInfo) : ISerializableMember
    {
        public string Name => fieldInfo.Name;
        public Type ValueType => fieldInfo.FieldType;
        public bool PreserveReferences => fieldInfo.IsDefined(typeof(SerializeReferenceAttribute), true);
        public object? GetValue(object owner) => fieldInfo.GetValue(owner);
        public void SetValue(object owner, object? value) => fieldInfo.SetValue(owner, value);
    }

    private sealed class CloneContext
    {
        public readonly HashSet<object> Active = new(ReferenceEqualityComparer.Instance);
        public readonly Dictionary<object, object> Clones = new(ReferenceEqualityComparer.Instance);
        public int Depth;
    }

    private sealed record StagedMember(ISerializableMember Member, object? Value, string Path);

    private static readonly ConcurrentDictionary<(Type Type, bool StopAtScriptableObject), ISerializableMember[]>
        Members = new();

    public static T Clone<T>(T source) where T : ScriptableObject
    {
        ArgumentNullException.ThrowIfNull(source);
        Type runtimeType = source.GetType();
        if (runtimeType.IsAbstract)
            throw Unsupported(runtimeType, runtimeType.FullName ?? runtimeType.Name,
                              "abstract assets cannot be cloned");

        var clone = (ScriptableObject)RuntimeHelpers.GetUninitializedObject(runtimeType);
        clone.InitializeCloneRuntimeState();
        clone.Name = source.Name;

        var context = new CloneContext();
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            CopyMembers(source, clone, runtimeType, context, runtimeType.FullName ?? runtimeType.Name,
                        stopAtScriptableObject: true);
        }
        finally
        {
            context.Active.Remove(source);
        }
        return (T)clone;
    }

    public static void CopySerializedState(ScriptableObject source, ScriptableObject destination)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(destination);
        Type runtimeType = source.GetType();
        if (destination.GetType() != runtimeType)
            throw new InvalidOperationException(
                $"Managed data state cannot migrate from '{runtimeType.FullName}' to " +
                $"'{destination.GetType().FullName}'.");
        if (ReferenceEquals(source, destination))
            return;

        string path = runtimeType.FullName ?? runtimeType.Name;
        var context = new CloneContext();
        context.Active.Add(source);
        context.Clones.Add(source, destination);
        List<StagedMember> staged;
        try
        {
            staged = StageMembers(source, runtimeType, context, path, stopAtScriptableObject: true);
        }
        finally
        {
            context.Active.Remove(source);
        }

        string previousName = destination.Name;
        var applied = new List<(ISerializableMember Member, object? Previous)>(staged.Count);
        try
        {
            foreach (StagedMember value in staged)
            {
                object? previous = value.Member.GetValue(destination);
                value.Member.SetValue(destination, value.Value);
                applied.Add((value.Member, previous));
            }
            destination.Name = source.Name;
        }
        catch (Exception exception)
        {
            for (int index = applied.Count - 1; index >= 0; --index)
            {
                try
                {
                    applied[index].Member.SetValue(destination, applied[index].Previous);
                }
                catch
                {
                    // The original assignment failure is the actionable diagnostic.
                }
            }
            destination.Name = previousName;
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0004", path, runtimeType, runtimeType,
                "the transactional state commit failed and the previous values were restored", exception,
                phase: "commit", owner: runtimeType.FullName ?? runtimeType.Name,
                rootField: staged.FirstOrDefault()?.Member.Name ?? runtimeType.Name);
        }
    }

    internal static T CloneSerializableValueForTests<T>(T source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return (T)CloneValue(source, typeof(T), new CloneContext(), typeof(T).FullName ?? typeof(T).Name)!;
    }

    internal static void ValidateSerializableValue(object? value, Type declaredType, string path)
    {
        _ = CloneValue(value, declaredType, new CloneContext(), path);
    }

    internal static void ValidateFieldLimitForTests(Type type) => _ = GetMembers(type, false);

    private static void CopyMembers(object source, object destination, Type type, CloneContext context, string path,
                                    bool stopAtScriptableObject, bool preserveReferences = false)
    {
        foreach (ISerializableMember member in GetMembers(type, stopAtScriptableObject))
        {
            string memberPath = $"{path}.{member.Name}";
            object? value;
            try
            {
                value = member.GetValue(source);
            }
            catch (Exception exception)
            {
                throw new InvalidOperationException(
                    $"Managed data member '{memberPath}' could not be read.", exception);
            }

            object? clone = CloneValue(value, member.ValueType, context, memberPath,
                                       preserveReferences || member.PreserveReferences);
            try
            {
                member.SetValue(destination, clone);
            }
            catch (Exception exception)
            {
                throw new InvalidOperationException($"Managed data member '{memberPath}' could not be restored.",
                                                    exception);
            }
        }
    }

    private static List<StagedMember> StageMembers(object source, Type type, CloneContext context, string path,
                                                   bool stopAtScriptableObject, bool preserveReferences = false)
    {
        var staged = new List<StagedMember>();
        foreach (ISerializableMember member in GetMembers(type, stopAtScriptableObject))
        {
            string memberPath = $"{path}.{member.Name}";
            object? value;
            try
            {
                value = member.GetValue(source);
            }
            catch (Exception exception)
            {
                throw new ManagedSerializationException(
                    "KEIRE-MANAGED-SERIALIZATION-0004", memberPath, member.ValueType, null,
                    "the field could not be read before the transactional commit", exception,
                    phase: "capture", owner: type.FullName ?? type.Name, rootField: member.Name);
            }
            staged.Add(new StagedMember(
                member,
                CloneValue(value, member.ValueType, context, memberPath,
                           preserveReferences || member.PreserveReferences),
                memberPath));
        }
        return staged;
    }

    private static object? CloneValue(object? value, Type declaredType, CloneContext context, string path,
                                      bool preserveReferences = false)
    {
        bool countsTowardDepth = !typeof(EngineObject).IsAssignableFrom(declaredType) &&
                                 !IsImmutableValue(declaredType) && !declaredType.IsEnum;
        if (countsTowardDepth)
            ++context.Depth;
        try
        {
            if (context.Depth > MaximumDepth)
                throw Unsupported(declaredType, path, $"values cannot exceed {MaximumDepth} nested levels");
            return CloneValueCore(value, declaredType, context, path, preserveReferences);
        }
        finally
        {
            if (countsTowardDepth)
                --context.Depth;
        }
    }

    private static object? CloneValueCore(object? value, Type declaredType, CloneContext context, string path,
                                          bool preserveReferences)
    {
        if (typeof(EngineObject).IsAssignableFrom(declaredType))
            return value;

        if (IsImmutableValue(declaredType))
            return value;

        if (declaredType.IsEnum)
            return value;

        if (declaredType.IsArray)
        {
            ValidateArrayType(declaredType, path, preserveReferences);
            return value is null ? null : CloneArray((Array)value, declaredType, context, path, preserveReferences);
        }

        if (IsList(declaredType, out Type? elementType))
        {
            ValidateElementType(elementType, $"{path}[]", preserveReferences);
            return value is null ? null :
                CloneList((IList)value, declaredType, elementType, context, path, preserveReferences);
        }

        if (IsDictionary(declaredType, out Type? keyType, out Type? valueType))
        {
            ValidateDictionaryKeyType(keyType, path);
            ValidateElementType(valueType, $"{path}[value]", preserveReferences);
            return value is null ? null :
                CloneDictionary((IDictionary)value, declaredType, keyType, valueType, context, path,
                                preserveReferences);
        }

        if (value is null)
        {
            if (!preserveReferences)
                ValidateSerializableShape(declaredType, path);
            return null;
        }

        Type runtimeType = value.GetType();
        if (runtimeType != declaredType && !preserveReferences)
            throw Unsupported(declaredType, path, runtimeType,
                              "polymorphic inline values require SerializeReference");

        Type serializedType = preserveReferences ? runtimeType : declaredType;
        if (!declaredType.IsAssignableFrom(serializedType))
            throw Unsupported(declaredType, path, runtimeType, "the runtime type is not assignable to the field");
        ValidateSerializableShape(serializedType, path, preserveReferences);

        if (!serializedType.IsValueType)
        {
            if (context.Clones.TryGetValue(value, out object? existing))
            {
                if (!preserveReferences && context.Active.Contains(value))
                    throw Unsupported(declaredType, path, runtimeType,
                                      "cyclic inline object graphs require SerializeReference");
                return existing;
            }
        }

        object clone = serializedType.IsValueType
            ? Activator.CreateInstance(serializedType)!
            : RuntimeHelpers.GetUninitializedObject(serializedType);
        if (!serializedType.IsValueType)
        {
            context.Active.Add(value);
            context.Clones.Add(value, clone);
        }
        try
        {
            CopyMembers(value, clone, serializedType, context, path, stopAtScriptableObject: false,
                        preserveReferences);
        }
        finally
        {
            if (!serializedType.IsValueType)
                context.Active.Remove(value);
        }
        return clone;
    }

    private static Array CloneArray(Array source, Type arrayType, CloneContext context, string path,
                                    bool preserveReferences)
    {
        ValidateArrayType(arrayType, path, preserveReferences);
        if (context.Clones.TryGetValue(source, out object? existing))
        {
            if (!preserveReferences && context.Active.Contains(source))
                throw Unsupported(arrayType, path, source.GetType(),
                                  "cyclic inline object graphs require SerializeReference");
            return (Array)existing;
        }

        Type elementType = arrayType.GetElementType()!;
        var clone = Array.CreateInstance(elementType, source.Length);
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            for (int index = 0; index < source.Length; ++index)
                clone.SetValue(CloneValue(source.GetValue(index), elementType, context, $"{path}[{index}]",
                                          preserveReferences), index);
        }
        finally
        {
            context.Active.Remove(source);
        }
        return clone;
    }

    private static IList CloneList(IList source, Type listType, Type elementType, CloneContext context, string path,
                                   bool preserveReferences)
    {
        if (context.Clones.TryGetValue(source, out object? existing))
        {
            if (!preserveReferences && context.Active.Contains(source))
                throw Unsupported(listType, path, source.GetType(),
                                  "cyclic inline object graphs require SerializeReference");
            return (IList)existing;
        }

        var clone = (IList)Activator.CreateInstance(listType)!;
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            for (int index = 0; index < source.Count; ++index)
                clone.Add(CloneValue(source[index], elementType, context, $"{path}[{index}]", preserveReferences));
        }
        finally
        {
            context.Active.Remove(source);
        }
        return clone;
    }

    private static IDictionary CloneDictionary(IDictionary source, Type dictionaryType, Type keyType, Type valueType,
                                               CloneContext context, string path, bool preserveReferences)
    {
        ValidateDefaultDictionaryComparer(source, dictionaryType, keyType, path);
        if (context.Clones.TryGetValue(source, out object? existing))
        {
            if (!preserveReferences && context.Active.Contains(source))
                throw Unsupported(dictionaryType, path, source.GetType(),
                                  "cyclic inline object graphs require SerializeReference");
            return (IDictionary)existing;
        }

        var clone = (IDictionary)Activator.CreateInstance(dictionaryType)!;
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            foreach (DictionaryEntry entry in source)
            {
                string keyPath = $"{path}[{FormatDictionaryKey(entry.Key)}]";
                object key = CloneValue(entry.Key, keyType, context, $"{keyPath}.Key", preserveReferences) ??
                    throw Unsupported(keyType, $"{keyPath}.Key", "dictionary keys cannot be null");
                object? item = CloneValue(entry.Value, valueType, context, keyPath, preserveReferences);
                clone.Add(key, item);
            }
        }
        finally
        {
            context.Active.Remove(source);
        }
        return clone;
    }

    private static ISerializableMember[] GetMembers(Type type, bool stopAtScriptableObject) =>
        Members.GetOrAdd((type, stopAtScriptableObject),
                         key => DiscoverMembers(key.Type, key.StopAtScriptableObject));

    private static ISerializableMember[] DiscoverMembers(Type type, bool stopAtScriptableObject)
    {
        var members = new List<(int Depth, int Token, ISerializableMember Member)>();
        int depth = 0;
        for (Type? current = type;
             current is not null && current != typeof(object) &&
             (!stopAtScriptableObject || current != typeof(ScriptableObject));
             current = current.BaseType, ++depth)
        {
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                       BindingFlags.DeclaredOnly;
            foreach (FieldInfo field in current.GetFields(flags))
            {
                bool serialized = field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
                                  field.IsDefined(typeof(SerializeReferenceAttribute), true);
                if (!serialized || field.IsStatic || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(CompilerGeneratedAttribute), false))
                    continue;
                if (members.Count >= MaximumFieldsPerType)
                {
                    string owner = type.FullName ?? type.Name;
                    throw Unsupported(field.FieldType, $"{field.DeclaringType?.FullName ?? owner}.{field.Name}",
                                      $"serialized types cannot exceed {MaximumFieldsPerType} serialized fields")
                        .WithContext("validate", owner, field.Name);
                }
                members.Add((depth, field.MetadataToken, new SerializableField(field)));
            }

        }

        return members.OrderByDescending(member => member.Depth).ThenBy(member => member.Token)
            .Select(member => member.Member).ToArray();
    }

    private static void ValidateSerializableShape(Type type, string path, bool preserveReferences = false)
    {
        if (!type.IsDefined(typeof(SerializableAttribute), false) &&
            !type.IsDefined(typeof(SerializableTypeAttribute), false))
            throw Unsupported(type, path, "inline types must declare Serializable");
        if (type.IsInterface || type.IsAbstract)
            throw Unsupported(type, path, "abstract and interface inline values are not supported");
        if (!preserveReferences)
            return;

        StableSerializedTypeIdAttribute? stableId =
            type.GetCustomAttribute<StableSerializedTypeIdAttribute>(false);
        if (stableId is null || stableId.Id == Guid.Empty)
            throw Unsupported(type, path, "SerializeReference runtime types require StableSerializedTypeId");
        if (type.ContainsGenericParameters)
            throw Unsupported(type, path, "SerializeReference runtime types cannot be open generic types");
        if (type.GetConstructor(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null,
                                Type.EmptyTypes, null) is null)
            throw Unsupported(type, path, "SerializeReference runtime types require a parameterless constructor");
    }

    private static void ValidateArrayType(Type type, string path, bool preserveReferences = false)
    {
        if (!type.IsSZArray)
            throw Unsupported(type, path, "only single-dimensional zero-based arrays are supported");
        ValidateElementType(type.GetElementType()!, $"{path}[]", preserveReferences);
    }

    private static void ValidateElementType(Type type, string path, bool preserveReferences = false)
    {
        if (typeof(EngineObject).IsAssignableFrom(type) || IsImmutableValue(type) || type.IsEnum)
            return;
        if (type.IsArray)
        {
            ValidateArrayType(type, path, preserveReferences);
            return;
        }
        if (IsList(type, out Type? elementType))
        {
            ValidateElementType(elementType, $"{path}[]", preserveReferences);
            return;
        }
        if (IsDictionary(type, out Type? keyType, out Type? valueType))
        {
            ValidateDictionaryKeyType(keyType, path);
            ValidateElementType(valueType, $"{path}[value]", preserveReferences);
            return;
        }
        if (!preserveReferences)
            ValidateSerializableShape(type, path);
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
        throw Unsupported(type, $"{path}[key]",
                          "dictionary keys must be strings, booleans, characters, integers, enums, or GUIDs");
    }

    private static void ValidateDefaultDictionaryComparer(IDictionary dictionary, Type dictionaryType, Type keyType,
                                                          string path)
    {
        object? comparer = dictionaryType.GetProperty("Comparer", BindingFlags.Instance | BindingFlags.Public)?
            .GetValue(dictionary);
        object? defaultComparer = typeof(EqualityComparer<>).MakeGenericType(keyType)
            .GetProperty("Default", BindingFlags.Static | BindingFlags.Public)?.GetValue(null);
        if (!ReferenceEquals(comparer, defaultComparer))
            throw Unsupported(dictionaryType, path, dictionaryType, "custom dictionary comparers are not supported");
    }

    private static string FormatDictionaryKey(object? key) => key switch
    {
        null => "null",
        string value => $"\"{value}\"",
        _ => Convert.ToString(key, System.Globalization.CultureInfo.InvariantCulture) ?? key.ToString() ?? "?"
    };

    private static bool IsImmutableValue(Type type) =>
        type == typeof(bool) || type == typeof(sbyte) || type == typeof(byte) ||
        type == typeof(short) || type == typeof(ushort) || type == typeof(int) ||
        type == typeof(uint) || type == typeof(long) || type == typeof(ulong) ||
        type == typeof(char) || type == typeof(float) || type == typeof(double) ||
        type == typeof(decimal) || type == typeof(string) || type == typeof(Guid) ||
        type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) ||
        type == typeof(Quaternion) || type == typeof(Color);

    private static ManagedSerializationException Unsupported(Type type, string path, string reason) =>
        Unsupported(type, path, null, reason);

    private static ManagedSerializationException Unsupported(Type type, string path, Type? runtimeType,
                                                             string reason) =>
        new("KEIRE-MANAGED-SERIALIZATION-0001", path, type, runtimeType, reason);
}
