using System.Collections;
using System.Reflection;
using Keire.UI;

namespace Keire.Editor;

public enum SerializedPropertyKind : byte
{
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Number,
    String,
    Enum,
    Value,
    EngineReference,
    Array,
    List,
    Dictionary,
    Object
}

public sealed class SerializedProperty
{
    private readonly SerializedObject _owner;
    private readonly Func<object, object?> _getter;
    private readonly Action<object, object?> _setter;
    private readonly FieldInfo? _field;
    private IReadOnlyList<SerializedProperty> _children = [];
    private object? _stagedValue;
    private bool _changed;

    internal SerializedProperty(SerializedObject owner, FieldInfo field)
        : this(owner, field.Name,
            field.GetCustomAttribute<InspectorNameAttribute>()?.Name ?? SplitName(field.Name),
            $"{field.DeclaringType?.FullName ?? field.DeclaringType?.Name}.{field.Name}",
            field.GetCustomAttribute<StableFieldIdAttribute>()?.Id, field.FieldType,
            field.IsInitOnly || field.IsLiteral || field.IsDefined(typeof(ReadOnlyInInspectorAttribute), true),
            field.IsDefined(typeof(SerializeReferenceAttribute), true), field.GetValue, field.SetValue, field)
    {
    }

    internal SerializedProperty(SerializedObject owner, string name, string displayName, string path, Guid? stableId,
                                Type declaredType, bool readOnly, bool preserveReferences,
                                Func<object, object?> getter, Action<object, object?> setter, FieldInfo? field = null)
    {
        _owner = owner;
        _field = field;
        _getter = getter;
        _setter = setter;
        Name = name;
        DisplayName = displayName;
        Path = path;
        StableId = stableId;
        DeclaredType = declaredType;
        IsReadOnly = readOnly;
        PreserveReferences = preserveReferences;
    }

    public string Name { get; }
    public string DisplayName { get; }
    public string Path { get; }
    public Guid? StableId { get; }
    public Type DeclaredType { get; }
    public bool IsReadOnly { get; }
    public bool IsMixedValue
    {
        get
        {
            object?[] values = Values().ToArray();
            return values.Skip(1).Any(value => !Equals(value, values[0]));
        }
    }
    public SerializedPropertyKind Kind => PropertyKind(DeclaredType);
    public IReadOnlyList<SerializedProperty> Children => _children;

    public object? BoxedValue
    {
        get => _changed ? _stagedValue : Values().FirstOrDefault();
        set
        {
            _owner.ThrowIfInvalid();
            if (IsReadOnly)
                throw new InvalidOperationException($"Serialized property '{Path}' is read-only.");
            if (value is not null && !DeclaredType.IsInstanceOfType(value))
                throw new ArgumentException($"Value type '{value.GetType().FullName}' is not assignable to '{Path}'.",
                                            nameof(value));
            if (value is null && DeclaredType.IsValueType && Nullable.GetUnderlyingType(DeclaredType) is null)
                throw new ArgumentNullException(nameof(value), $"Serialized property '{Path}' is not nullable.");
            _stagedValue = value;
            _changed = true;
            _owner.MarkChanged(this);
        }
    }

    internal bool Changed => _changed;
    internal bool PreserveReferences { get; }
    internal object? StagedValue => _stagedValue;
    internal object? GetValue(object target) => _getter(target);
    internal void SetValue(object target, object? value) => _setter(target, value);
    internal IEnumerable<TAttribute> GetAttributes<TAttribute>() where TAttribute : Attribute =>
        _field?.GetCustomAttributes<TAttribute>(true) ?? [];
    internal void SetChildren(IReadOnlyList<SerializedProperty> children) => _children = children;

    internal void ClearStaged()
    {
        _stagedValue = null;
        _changed = false;
    }

    private IEnumerable<object?> Values() => _owner.Targets.Select(_getter);

    private static SerializedPropertyKind PropertyKind(Type type)
    {
        Type value = Nullable.GetUnderlyingType(type) ?? type;
        if (value == typeof(bool))
            return SerializedPropertyKind.Boolean;
        if (value == typeof(sbyte) || value == typeof(short) || value == typeof(int) || value == typeof(long) ||
            value == typeof(char))
            return SerializedPropertyKind.SignedInteger;
        if (value == typeof(byte) || value == typeof(ushort) || value == typeof(uint) || value == typeof(ulong))
            return SerializedPropertyKind.UnsignedInteger;
        if (value == typeof(float) || value == typeof(double) || value == typeof(decimal))
            return SerializedPropertyKind.Number;
        if (value == typeof(string) || value == typeof(Guid))
            return SerializedPropertyKind.String;
        if (value.IsEnum)
            return SerializedPropertyKind.Enum;
        if (typeof(EngineObject).IsAssignableFrom(value))
            return SerializedPropertyKind.EngineReference;
        if (value.IsArray)
            return SerializedPropertyKind.Array;
        if (value.IsGenericType && value.GetGenericTypeDefinition() == typeof(List<>))
            return SerializedPropertyKind.List;
        if (value.IsGenericType && value.GetGenericTypeDefinition() == typeof(Dictionary<,>))
            return SerializedPropertyKind.Dictionary;
        return value.IsValueType ? SerializedPropertyKind.Value : SerializedPropertyKind.Object;
    }

    private static string SplitName(string name)
    {
        var result = new System.Text.StringBuilder(name.Length + 8);
        for (int index = 0; index < name.Length; ++index)
        {
            char character = name[index];
            if (index > 0 && char.IsUpper(character) && !char.IsUpper(name[index - 1]))
                result.Append(' ');
            result.Append(character);
        }
        return result.ToString().TrimStart('_');
    }
}

public sealed class SerializedObject : IDisposable
{
    private readonly Dictionary<string, SerializedProperty> _properties;
    private readonly Dictionary<string, SerializedProperty> _paths = new(StringComparer.Ordinal);
    private readonly HashSet<SerializedProperty> _changed = [];
    private readonly EditorExtensionLifetime? _lifetime;
    private bool _disposed;

    public SerializedObject(object target, ulong generation = 1) : this([target], generation) { }

    public SerializedObject(IReadOnlyList<object> targets, ulong generation = 1)
        : this(targets, generation, null)
    {
    }

    public SerializedObject(IReadOnlyList<object> targets, EditorExtensionLifetime lifetime)
        : this(targets, lifetime?.Generation ?? throw new ArgumentNullException(nameof(lifetime)), lifetime)
    {
    }

    private SerializedObject(IReadOnlyList<object> targets, ulong generation, EditorExtensionLifetime? lifetime)
    {
        ArgumentNullException.ThrowIfNull(targets);
        if (targets.Count == 0 || targets.Any(target => target is null))
            throw new ArgumentException("SerializedObject requires at least one non-null target.", nameof(targets));
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation));
        Type type = targets[0].GetType();
        if (targets.Any(target => target.GetType() != type))
            throw new ArgumentException("SerializedObject multi-edit targets must have the same concrete type.",
                                        nameof(targets));
        Generation = generation;
        _lifetime = lifetime;
        Targets = targets.ToArray();
        _properties = SerializableFields(type).Select(field => new SerializedProperty(this, field))
            .ToDictionary(property => property.Name, StringComparer.Ordinal);
        var active = new HashSet<object>(ReferenceEqualityComparer.Instance);
        foreach (SerializedProperty property in _properties.Values)
            Register(property, 0, active);
    }

    public ulong Generation { get; }
    public IReadOnlyList<object> Targets { get; }
    public bool HasModifiedProperties => _changed.Count != 0;
    public IEnumerable<SerializedProperty> Properties => _properties.Values.OrderBy(value => value.Path,
        StringComparer.Ordinal);

    public SerializedProperty? FindProperty(string name)
    {
        ThrowIfInvalid();
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return _properties.GetValueOrDefault(name) ?? _paths.GetValueOrDefault(name);
    }

    public void Update()
    {
        ThrowIfInvalid();
        foreach (SerializedProperty property in _changed)
            property.ClearStaged();
        _changed.Clear();
    }

    public bool ApplyModifiedProperties(string undoName)
    {
        ThrowIfInvalid();
        ArgumentException.ThrowIfNullOrWhiteSpace(undoName);
        if (_changed.Count == 0)
            return false;

        foreach (SerializedProperty property in _changed)
        {
            ManagedSerialization.ValidateValue(property.StagedValue, property.DeclaredType, property.Path,
                                               property.PreserveReferences);
        }
        var applied = new List<(object Target, SerializedProperty Property, object? Previous)>();
        try
        {
            bool committed = Undo.Perform(undoName, () =>
            {
                foreach (SerializedProperty property in _changed.OrderBy(value => value.StableId)
                             .ThenBy(value => value.Path, StringComparer.Ordinal))
                {
                    foreach (object target in Targets)
                    {
                        object? previous = property.GetValue(target);
                        property.SetValue(target, property.StagedValue);
                        applied.Add((target, property, previous));
                    }
                }
                foreach (object target in Targets)
                {
                    if (target is ScriptableObject scriptable)
                        InvokeScriptableValidation(scriptable);
                    else if (target is Behaviour behaviour)
                        InvokeBehaviourValidation(behaviour);
                }
            });
            if (!committed)
                return false;
        }
        catch
        {
            for (int index = applied.Count - 1; index >= 0; --index)
            {
                try
                {
                    applied[index].Property.SetValue(applied[index].Target, applied[index].Previous);
                }
                catch
                {
                }
            }
            throw;
        }
        foreach (SerializedProperty property in _changed)
            property.ClearStaged();
        _changed.Clear();
        return true;
    }

    public void Dispose()
    {
        _disposed = true;
        _changed.Clear();
    }

    internal void MarkChanged(SerializedProperty property) => _changed.Add(property);
    internal void ThrowIfInvalid()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _lifetime?.ThrowIfInvalid();
    }

    private void Register(SerializedProperty property, int depth, HashSet<object> active)
    {
        if (!_paths.TryAdd(property.Path, property))
            throw new InvalidOperationException($"Serialized property path '{property.Path}' is duplicated.");
        if (depth >= 32)
            return;
        object? sample = property.GetValue(Targets[0]);
        if (sample is null || property.Kind is SerializedPropertyKind.Null or SerializedPropertyKind.Boolean or
            SerializedPropertyKind.SignedInteger or SerializedPropertyKind.UnsignedInteger or
            SerializedPropertyKind.Number or SerializedPropertyKind.String or SerializedPropertyKind.Enum or
            SerializedPropertyKind.Value or SerializedPropertyKind.EngineReference)
        {
            return;
        }
        if (!sample.GetType().IsValueType && !active.Add(sample))
            return;
        try
        {
            IReadOnlyList<SerializedProperty> children = BuildChildren(property, sample);
            property.SetChildren(children);
            foreach (SerializedProperty child in children)
                Register(child, depth + 1, active);
        }
        finally
        {
            if (!sample.GetType().IsValueType)
                active.Remove(sample);
        }
    }

    private IReadOnlyList<SerializedProperty> BuildChildren(SerializedProperty parent, object sample)
    {
        var result = new List<SerializedProperty>();
        if (sample is Array array)
        {
            Type elementType = parent.DeclaredType.GetElementType()!;
            for (int index = 0; index < array.Length; ++index)
                result.Add(CollectionElement(parent, index, elementType, false));
        }
        else if (sample is IList list && parent.DeclaredType.IsGenericType &&
                 parent.DeclaredType.GetGenericTypeDefinition() == typeof(List<>))
        {
            Type elementType = parent.DeclaredType.GetGenericArguments()[0];
            for (int index = 0; index < list.Count; ++index)
                result.Add(CollectionElement(parent, index, elementType, true));
        }
        else if (sample is IDictionary dictionary && parent.DeclaredType.IsGenericType &&
                 parent.DeclaredType.GetGenericTypeDefinition() == typeof(Dictionary<,>))
        {
            Type valueType = parent.DeclaredType.GetGenericArguments()[1];
            var keys = new List<object>();
            foreach (object key in dictionary.Keys)
                keys.Add(key);
            foreach (object key in keys.OrderBy(FormatKey, StringComparer.Ordinal))
            {
                string keyPath = FormatKey(key);
                result.Add(new SerializedProperty(this, keyPath, keyPath, $"{parent.Path}[{keyPath}]", null,
                    valueType, parent.IsReadOnly, parent.PreserveReferences,
                    target => (parent.GetValue(target) as IDictionary)?[key],
                    (target, value) =>
                    {
                        IDictionary owner = parent.GetValue(target) as IDictionary ??
                            throw new InvalidOperationException($"Serialized dictionary '{parent.Path}' is null.");
                        owner[key] = value;
                    }));
            }
        }
        else
        {
            foreach (FieldInfo field in SerializableFields(parent.DeclaredType))
            {
                result.Add(new SerializedProperty(this, field.Name,
                    field.GetCustomAttribute<InspectorNameAttribute>()?.Name ?? SplitName(field.Name),
                    $"{parent.Path}.{field.Name}", field.GetCustomAttribute<StableFieldIdAttribute>()?.Id,
                    field.FieldType, parent.IsReadOnly || field.IsInitOnly || field.IsLiteral ||
                    field.IsDefined(typeof(ReadOnlyInInspectorAttribute), true),
                    field.IsDefined(typeof(SerializeReferenceAttribute), true),
                    target =>
                    {
                        object? owner = parent.GetValue(target);
                        return owner is null ? null : field.GetValue(owner);
                    },
                    (target, value) =>
                    {
                        object owner = parent.GetValue(target) ??
                            throw new InvalidOperationException($"Serialized object '{parent.Path}' is null.");
                        field.SetValue(owner, value);
                        if (parent.DeclaredType.IsValueType)
                            parent.SetValue(target, owner);
                    }, field));
            }
        }
        if (result.Count > 4_096)
            throw new InvalidOperationException("Serialized property snapshots cannot exceed 4,096 children per node.");
        return result;
    }

    private SerializedProperty CollectionElement(SerializedProperty parent, int index, Type elementType, bool list)
    {
        return new SerializedProperty(this, index.ToString(System.Globalization.CultureInfo.InvariantCulture),
            $"Element {index}", $"{parent.Path}[{index}]", null, elementType, parent.IsReadOnly,
            parent.PreserveReferences,
            target => list ? (parent.GetValue(target) as IList)?[index] :
                (parent.GetValue(target) as Array)?.GetValue(index),
            (target, value) =>
            {
                object owner = parent.GetValue(target) ??
                    throw new InvalidOperationException($"Serialized collection '{parent.Path}' is null.");
                if (list)
                    ((IList)owner)[index] = value;
                else
                    ((Array)owner).SetValue(value, index);
            });
    }

    private static string FormatKey(object value) => value is string text
        ? $"\"{text.Replace("\\", "\\\\", StringComparison.Ordinal).Replace("\"", "\\\"", StringComparison.Ordinal)}\""
        : Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? value.ToString() ?? "?";

    private static FieldInfo[] SerializableFields(Type type)
    {
        var hierarchy = new Stack<Type>();
        for (Type? current = type; current is not null && current != typeof(object); current = current.BaseType)
            hierarchy.Push(current);
        var result = new List<FieldInfo>();
        while (hierarchy.TryPop(out Type? current))
        {
            foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                           BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                         .OrderBy(value => value.MetadataToken))
            {
                if (field.IsStatic || field.IsLiteral || field.IsInitOnly ||
                    field.IsDefined(typeof(NonSerializedAttribute), false) ||
                    field.IsDefined(typeof(HideInInspectorAttribute), true))
                    continue;
                if (field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true) ||
                    field.IsDefined(typeof(SerializeReferenceAttribute), true))
                    result.Add(field);
            }
        }
        return result.ToArray();
    }

    private static void InvokeScriptableValidation(ScriptableObject target) =>
        typeof(ScriptableObject).GetMethod("Validate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(target, null);

    private static void InvokeBehaviourValidation(Behaviour target) =>
        typeof(Behaviour).GetMethod("RuntimeValidate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(target, null);

    private static string SplitName(string name)
    {
        var result = new System.Text.StringBuilder(name.Length + 8);
        for (int index = 0; index < name.Length; ++index)
        {
            char character = name[index];
            if (index > 0 && char.IsUpper(character) && !char.IsUpper(name[index - 1]))
                result.Append(' ');
            result.Append(character);
        }
        return result.ToString().TrimStart('_');
    }
}

public abstract class PropertyAttribute : Attribute
{
    public int Order { get; init; }
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomPropertyDrawerAttribute(Type targetType, bool useForChildren = false) : Attribute
{
    public Type TargetType { get; } = targetType ?? throw new ArgumentNullException(nameof(targetType));
    public bool UseForChildren { get; } = useForChildren;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomPropertyDecoratorAttribute(Type attributeType) : Attribute
{
    public Type AttributeType { get; } = Validate(attributeType);

    private static Type Validate(Type type) => typeof(PropertyAttribute).IsAssignableFrom(type)
        ? type
        : throw new ArgumentException("Property decorators must target a PropertyAttribute type.", nameof(type));
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomEditorAttribute(Type targetType, bool editorForChildClasses = false) : Attribute
{
    public Type TargetType { get; } = targetType ?? throw new ArgumentNullException(nameof(targetType));
    public bool EditorForChildClasses { get; } = editorForChildClasses;
}

[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class CanEditMultipleObjectsAttribute : Attribute;

public abstract class PropertyDrawer : EditorExtension
{
    public abstract VisualElement CreatePropertyGUI(SerializedProperty property);
}

public abstract class PropertyDecorator : EditorExtension
{
    public abstract VisualElement Decorate(SerializedProperty property, VisualElement content,
                                           PropertyAttribute attribute);
}

public abstract class Editor : EditorExtension
{
    public SerializedObject SerializedObject { get; internal set; } = null!;
    public object Target => SerializedObject.Targets[0];
    public IReadOnlyList<object> Targets => SerializedObject.Targets;

    public abstract VisualElement CreateInspectorGUI();
}

public sealed class PropertyField : BindableElement
{
    public PropertyField(SerializedProperty property)
    {
        Property = property ?? throw new ArgumentNullException(nameof(property));
        Label = property.DisplayName;
    }

    public SerializedProperty Property { get; }
    public string Label { get; set; }
}
