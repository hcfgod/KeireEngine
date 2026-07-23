namespace Keire;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializeFieldAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HideInInspectorAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class RangeAttribute(double minimum, double maximum) : Attribute
{
    public double Minimum { get; } = minimum;
    public double Maximum { get; } = maximum;
}

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class TooltipAttribute(string text) : Attribute
{
    public string Text { get; } = text ?? throw new ArgumentNullException(nameof(text));
}

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
