namespace Keire;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializeFieldAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field, Inherited = true)]
public sealed class SerializeReferenceAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field)]
public sealed class HotReloadStateAttribute : Attribute;

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class SerializableTypeAttribute : Attribute;

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableSerializedTypeIdAttribute : Attribute
{
    public StableSerializedTypeIdAttribute(string id)
    {
        Id = Guid.Parse(id);
        string compact = id.Replace("-", string.Empty, StringComparison.Ordinal);
        High = Convert.ToUInt64(compact[..16], 16);
        Low = Convert.ToUInt64(compact[16..], 16);
    }

    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HideInInspectorAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class RangeAttribute : Attribute
{
    public RangeAttribute(double minimum, double maximum)
    {
        if (!double.IsFinite(minimum) || !double.IsFinite(maximum) || minimum >= maximum)
            throw new ArgumentOutOfRangeException(nameof(minimum), "Inspector range bounds must be finite and ordered.");
        Minimum = minimum;
        Maximum = maximum;
    }

    public readonly double Minimum;
    public readonly double Maximum;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MinAttribute : Attribute
{
    public MinAttribute(double minimum)
    {
        if (!double.IsFinite(minimum))
            throw new ArgumentOutOfRangeException(nameof(minimum), "Inspector minimums must be finite.");
        Minimum = minimum;
    }

    public readonly double Minimum;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MaxAttribute : Attribute
{
    public MaxAttribute(double maximum)
    {
        if (!double.IsFinite(maximum))
            throw new ArgumentOutOfRangeException(nameof(maximum), "Inspector maximums must be finite.");
        Maximum = maximum;
    }

    public readonly double Maximum;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorStepAttribute : Attribute
{
    public InspectorStepAttribute(double step)
    {
        if (!double.IsFinite(step) || step <= 0.0)
            throw new ArgumentOutOfRangeException(nameof(step), "Inspector drag steps must be finite and positive.");
        Step = step;
    }

    public readonly double Step;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class MultilineAttribute : Attribute
{
    public MultilineAttribute(int lines = 4)
    {
        if (lines is < 2 or > 32)
            throw new ArgumentOutOfRangeException(nameof(lines), "Multiline Inspector fields support 2..32 lines.");
        Lines = lines;
    }

    public readonly int Lines;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class InspectorNameAttribute : Attribute
{
    public InspectorNameAttribute(string name) =>
        Name = string.IsNullOrWhiteSpace(name)
            ? throw new ArgumentException("Inspector display name is empty.", nameof(name))
            : name;

    public readonly string Name;
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
public sealed class RequireComponentAttribute : Attribute
{
    public RequireComponentAttribute(Type componentType)
    {
        ComponentType = componentType ?? throw new ArgumentNullException(nameof(componentType));
        var id = global::Keire.ComponentType.Of(componentType);
        High = id.High;
        Low = id.Low;
    }

    public Type ComponentType { get; }
    public readonly ulong High;
    public readonly ulong Low;
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
        Id = Guid.Parse(id);
        var compact = id.Replace("-", string.Empty, StringComparison.Ordinal);
        High = Convert.ToUInt64(compact[..16], 16);
        Low = Convert.ToUInt64(compact[16..], 16);
    }

    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class StableAssetTypeIdAttribute : Attribute
{
    public StableAssetTypeIdAttribute(string id)
    {
        Id = Guid.Parse(id);
        string compact = id.Replace("-", string.Empty, StringComparison.Ordinal);
        High = Convert.ToUInt64(compact[..16], 16);
        Low = Convert.ToUInt64(compact[16..], 16);
    }

    public Guid Id { get; }
    public readonly ulong High;
    public readonly ulong Low;
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CreateAssetMenuAttribute(string menuName, string fileName = "") : Attribute
{
    public string MenuName { get; } = menuName ?? throw new ArgumentNullException(nameof(menuName));
    public string FileName { get; } = fileName ?? string.Empty;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HeaderAttribute : Attribute
{
    public HeaderAttribute(string text) => Value = text ?? throw new ArgumentNullException(nameof(text));

    public string Text => Value;
    public readonly string Value;
}
