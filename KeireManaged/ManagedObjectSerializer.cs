using System.Collections;
using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Keire;

internal static class ManagedObjectSerializer
{
    private const int MaximumDepth = 32;

    private interface ISerializableMember
    {
        string Name { get; }
        Type ValueType { get; }
        object? GetValue(object owner);
        void SetValue(object owner, object? value);
    }

    private sealed class SerializableField(FieldInfo fieldInfo) : ISerializableMember
    {
        public string Name => fieldInfo.Name;
        public Type ValueType => fieldInfo.FieldType;
        public object? GetValue(object owner) => fieldInfo.GetValue(owner);
        public void SetValue(object owner, object? value) => fieldInfo.SetValue(owner, value);
    }

    private sealed class CloneContext
    {
        public readonly HashSet<object> Active = new(ReferenceEqualityComparer.Instance);
        public readonly Dictionary<object, object> Clones = new(ReferenceEqualityComparer.Instance);
        public int Depth;
    }

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

        var context = new CloneContext();
        context.Active.Add(source);
        context.Clones.Add(source, destination);
        try
        {
            destination.Name = source.Name;
            CopyMembers(source, destination, runtimeType, context, runtimeType.FullName ?? runtimeType.Name,
                        stopAtScriptableObject: true);
        }
        finally
        {
            context.Active.Remove(source);
        }
    }

    internal static T CloneSerializableValueForTests<T>(T source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return (T)CloneValue(source, typeof(T), new CloneContext(), typeof(T).FullName ?? typeof(T).Name)!;
    }

    private static void CopyMembers(object source, object destination, Type type, CloneContext context, string path,
                                    bool stopAtScriptableObject)
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

            object? clone = CloneValue(value, member.ValueType, context, memberPath);
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

    private static object? CloneValue(object? value, Type declaredType, CloneContext context, string path)
    {
        ++context.Depth;
        try
        {
            if (context.Depth > MaximumDepth)
                throw Unsupported(declaredType, path, $"values cannot exceed {MaximumDepth} nested levels");
            return CloneValueCore(value, declaredType, context, path);
        }
        finally
        {
            --context.Depth;
        }
    }

    private static object? CloneValueCore(object? value, Type declaredType, CloneContext context, string path)
    {
        RejectUnsupportedContainer(declaredType, path);

        if (typeof(EngineObject).IsAssignableFrom(declaredType))
            return value;

        if (IsImmutableValue(declaredType))
            return value;

        if (declaredType.IsEnum)
            return value;

        if (declaredType.IsArray)
        {
            ValidateArrayType(declaredType, path);
            return value is null ? null : CloneArray((Array)value, declaredType, context, path);
        }

        if (IsList(declaredType, out Type? elementType))
        {
            ValidateElementType(elementType, $"{path}[]");
            return value is null ? null : CloneList((IList)value, declaredType, elementType, context, path);
        }

        if (value is null)
        {
            ValidateSerializableShape(declaredType, path);
            return null;
        }

        Type runtimeType = value.GetType();
        if (runtimeType != declaredType)
            throw Unsupported(declaredType, path,
                              $"polymorphic inline value '{runtimeType.FullName}' does not match its declared type");

        ValidateSerializableShape(declaredType, path);

        if (!declaredType.IsValueType)
        {
            if (context.Active.Contains(value))
                throw Unsupported(declaredType, path, "cyclic inline object graphs are not supported");
            if (context.Clones.TryGetValue(value, out object? existing))
                return existing;
        }

        object clone = declaredType.IsValueType
            ? Activator.CreateInstance(declaredType)!
            : RuntimeHelpers.GetUninitializedObject(declaredType);
        if (!declaredType.IsValueType)
        {
            context.Active.Add(value);
            context.Clones.Add(value, clone);
        }
        try
        {
            CopyMembers(value, clone, declaredType, context, path, stopAtScriptableObject: false);
        }
        finally
        {
            if (!declaredType.IsValueType)
                context.Active.Remove(value);
        }
        return clone;
    }

    private static Array CloneArray(Array source, Type arrayType, CloneContext context, string path)
    {
        ValidateArrayType(arrayType, path);
        if (context.Active.Contains(source))
            throw Unsupported(arrayType, path, "cyclic inline object graphs are not supported");
        if (context.Clones.TryGetValue(source, out object? existing))
            return (Array)existing;

        Type elementType = arrayType.GetElementType()!;
        var clone = Array.CreateInstance(elementType, source.Length);
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            for (int index = 0; index < source.Length; ++index)
                clone.SetValue(CloneValue(source.GetValue(index), elementType, context, $"{path}[{index}]"), index);
        }
        finally
        {
            context.Active.Remove(source);
        }
        return clone;
    }

    private static IList CloneList(IList source, Type listType, Type elementType, CloneContext context, string path)
    {
        if (context.Active.Contains(source))
            throw Unsupported(listType, path, "cyclic inline object graphs are not supported");
        if (context.Clones.TryGetValue(source, out object? existing))
            return (IList)existing;

        var clone = (IList)Activator.CreateInstance(listType)!;
        context.Active.Add(source);
        context.Clones.Add(source, clone);
        try
        {
            for (int index = 0; index < source.Count; ++index)
                clone.Add(CloneValue(source[index], elementType, context, $"{path}[{index}]"));
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
                bool serialized = field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true);
                if (!serialized || field.IsStatic || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(CompilerGeneratedAttribute), false))
                    continue;
                members.Add((depth, field.MetadataToken, new SerializableField(field)));
            }

        }

        return members.OrderByDescending(member => member.Depth).ThenBy(member => member.Token)
            .Select(member => member.Member).ToArray();
    }

    private static void RejectUnsupportedContainer(Type type, string path)
    {
        if (typeof(IDictionary).IsAssignableFrom(type) ||
            type.GetInterfaces().Any(candidate =>
                candidate.IsGenericType && candidate.GetGenericTypeDefinition() == typeof(IDictionary<,>)))
            throw Unsupported(type, path, "dictionaries are not supported");
    }

    private static void ValidateSerializableShape(Type type, string path)
    {
        if (!type.IsDefined(typeof(SerializableAttribute), false) &&
            !type.IsDefined(typeof(SerializableTypeAttribute), false))
            throw Unsupported(type, path, "inline types must declare Serializable");
        if (type.IsInterface || type.IsAbstract)
            throw Unsupported(type, path, "abstract and interface inline values are not supported");
    }

    private static void ValidateArrayType(Type type, string path)
    {
        if (!type.IsSZArray)
            throw Unsupported(type, path, "only single-dimensional zero-based arrays are supported");
        ValidateElementType(type.GetElementType()!, $"{path}[]");
    }

    private static void ValidateElementType(Type type, string path)
    {
        RejectUnsupportedContainer(type, path);
        if (typeof(EngineObject).IsAssignableFrom(type) || IsImmutableValue(type) || type.IsEnum)
            return;
        if (type.IsArray || IsList(type, out _))
            throw Unsupported(type, path, "nested collection containers are not supported");
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

    private static bool IsImmutableValue(Type type) =>
        type == typeof(bool) || type == typeof(sbyte) || type == typeof(byte) ||
        type == typeof(short) || type == typeof(ushort) || type == typeof(int) ||
        type == typeof(uint) || type == typeof(long) || type == typeof(ulong) ||
        type == typeof(char) || type == typeof(float) || type == typeof(double) ||
        type == typeof(decimal) || type == typeof(string) ||
        type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) ||
        type == typeof(Quaternion) || type == typeof(Color);

    private static InvalidOperationException Unsupported(Type type, string path, string reason) =>
        new($"Managed data member '{path}' of type '{type.FullName}' is unsupported: {reason}.");
}
