namespace Keire;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializeFieldAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field)]
public sealed class HotReloadStateAttribute : Attribute;

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class SerializableTypeAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HideInInspectorAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class RangeAttribute : Attribute
{
    public RangeAttribute(double minimum, double maximum)
    {
        if (!double.IsFinite(minimum) || !double.IsFinite(maximum) || minimum > maximum)
            throw new ArgumentOutOfRangeException(nameof(minimum), "Inspector range bounds must be finite and ordered.");
        Minimum = minimum;
        Maximum = maximum;
    }

    public readonly double Minimum;
    public readonly double Maximum;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class TooltipAttribute : Attribute
{
    public TooltipAttribute(string text) =>
        Text = string.IsNullOrWhiteSpace(text) ? throw new ArgumentException("Tooltip text is empty.", nameof(text)) : text;

    public readonly string Text;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorGroupAttribute : Attribute
{
    public InspectorGroupAttribute(string name) =>
        Name = string.IsNullOrWhiteSpace(name) ? throw new ArgumentException("Inspector group name is empty.", nameof(name)) : name;

    public readonly string Name;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class ReadOnlyInInspectorAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute(string name) : Attribute
{
    public string Name { get; } = name ?? throw new ArgumentNullException(nameof(name));
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class StableFieldIdAttribute(string id) : Attribute
{
    public Guid Id { get; } = Guid.Parse(id);
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class RequireComponentAttribute(Type componentType) : Attribute
{
    public Type ComponentType { get; } = componentType ?? throw new ArgumentNullException(nameof(componentType));
}

[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class ExecutionOrderAttribute : Attribute
{
    public ExecutionOrderAttribute(int order) => Order = order;

    public readonly int Order;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableComponentIdAttribute : Attribute
{
    public StableComponentIdAttribute(string id)
    {
        _ = Guid.Parse(id);
        var compact = id.Replace("-", string.Empty, StringComparison.Ordinal);
        High = Convert.ToUInt64(compact[..16], 16);
        Low = Convert.ToUInt64(compact[16..], 16);
    }

    public readonly ulong High;
    public readonly ulong Low;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableAssetTypeIdAttribute(string id) : Attribute
{
    public Guid Id { get; } = Guid.Parse(id);
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CreateAssetMenuAttribute(string menuName, string fileName = "") : Attribute
{
    public string MenuName { get; } = menuName ?? throw new ArgumentNullException(nameof(menuName));
    public string FileName { get; } = fileName ?? string.Empty;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HeaderAttribute(string text) : Attribute
{
    public string Text { get; } = text ?? throw new ArgumentNullException(nameof(text));
}
