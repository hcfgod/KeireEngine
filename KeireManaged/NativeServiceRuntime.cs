using System.ComponentModel;

namespace Keire;

public readonly struct NativeAbiValue
{
    private readonly object? _value;

    private NativeAbiValue(NativeValueKind kind, object? value)
    {
        Kind = kind;
        _value = value;
    }

    public NativeValueKind Kind { get; }

    public static NativeAbiValue From(bool value) => new(NativeValueKind.Boolean, value);
    public static NativeAbiValue From(sbyte value) => new(NativeValueKind.SignedInteger, (long)value);
    public static NativeAbiValue From(short value) => new(NativeValueKind.SignedInteger, (long)value);
    public static NativeAbiValue From(int value) => new(NativeValueKind.SignedInteger, (long)value);
    public static NativeAbiValue From(long value) => new(NativeValueKind.SignedInteger, value);
    public static NativeAbiValue From(byte value) => new(NativeValueKind.UnsignedInteger, (ulong)value);
    public static NativeAbiValue From(ushort value) => new(NativeValueKind.UnsignedInteger, (ulong)value);
    public static NativeAbiValue From(uint value) => new(NativeValueKind.UnsignedInteger, (ulong)value);
    public static NativeAbiValue From(ulong value) => new(NativeValueKind.UnsignedInteger, value);
    public static NativeAbiValue From(float value) => FromFloatingPoint(value);
    public static NativeAbiValue From(double value) => FromFloatingPoint(value);
    public static NativeAbiValue From(string value) => new(NativeValueKind.Utf8String,
        value ?? throw new ArgumentNullException(nameof(value)));
    public static NativeAbiValue From(Vector2 value) => new(NativeValueKind.Vector2, value);
    public static NativeAbiValue From(Vector3 value) => new(NativeValueKind.Vector3, value);
    public static NativeAbiValue From(Vector4 value) => new(NativeValueKind.Vector4, value);
    public static NativeAbiValue From(Quaternion value) => new(NativeValueKind.Quaternion, value);
    public static NativeAbiValue From(Color value) => new(NativeValueKind.Color, value);
    public static NativeAbiValue From(Guid value) => new(NativeValueKind.StableId, value);
    public static NativeAbiValue From(EntityId value) => new(NativeValueKind.EntityId, value);
    public static NativeAbiValue From(AssetId value) => new(NativeValueKind.AssetId, value);
    public static NativeAbiValue From(ComponentTypeId value) => new(NativeValueKind.ComponentTypeId, value);

    public static NativeAbiValue FromBuffer<T>(ReadOnlySpan<T> values, int maximumElements)
    {
        if (maximumElements is < 1 or > 65_536)
            throw new ArgumentOutOfRangeException(nameof(maximumElements));
        if (values.Length > maximumElements)
            throw new ArgumentOutOfRangeException(nameof(values),
                $"Native buffers cannot exceed their {maximumElements}-element contract bound.");
        if (!NativeServiceRuntime.IsSupportedBufferElement(typeof(T)))
            throw new NotSupportedException($"Native buffer element type '{typeof(T).FullName}' is unsupported.");
        return new NativeAbiValue(NativeValueKind.BoundedBuffer, values.ToArray());
    }

    internal object? BoxedValue => _value;

    private static NativeAbiValue FromFloatingPoint(double value)
    {
        if (!double.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value));
        return new NativeAbiValue(NativeValueKind.FloatingPoint, value);
    }
}

internal readonly record struct NativeInvocationResult(NativeAbiValue Value, NativeCallError? Error);
internal delegate NativeInvocationResult NativeServiceInvoker(Guid serviceId, Guid methodId,
                                                               NativeAbiValue[] arguments);

[EditorBrowsable(EditorBrowsableState.Never)]
public static class NativeServiceRuntime
{
    private static readonly object Gate = new();
    private static NativeServiceInvoker? s_invoker;
    private static ulong s_generation;
    private static int s_ownerThread;

    public static T Invoke<T>(Guid serviceId, Guid methodId, ReadOnlySpan<NativeAbiValue> arguments)
    {
        NativeInvocationResult result = InvokeCore(serviceId, methodId, arguments);
        if (result.Error is NativeCallError error)
            throw new InvalidOperationException($"Native service call failed ({error.Code}): {error.Message}");
        return ConvertValue<T>(result.Value);
    }

    public static void Invoke(Guid serviceId, Guid methodId, ReadOnlySpan<NativeAbiValue> arguments)
    {
        NativeInvocationResult result = InvokeCore(serviceId, methodId, arguments);
        if (result.Error is NativeCallError error)
            throw new InvalidOperationException($"Native service call failed ({error.Code}): {error.Message}");
        if (result.Value.Kind != NativeValueKind.Void)
            throw new InvalidOperationException("A void native service method returned a value.");
    }

    public static NativeCallResult<T> InvokeResult<T>(Guid serviceId, Guid methodId,
                                                       ReadOnlySpan<NativeAbiValue> arguments)
    {
        NativeInvocationResult result = InvokeCore(serviceId, methodId, arguments);
        return result.Error is NativeCallError error
            ? NativeCallResult<T>.Failure(error.Code, error.Message)
            : NativeCallResult<T>.Success(ConvertValue<T>(result.Value));
    }

    internal static void Install(ulong generation, NativeServiceInvoker invoker)
    {
        ArgumentNullException.ThrowIfNull(invoker);
        if (generation == 0)
            throw new ArgumentOutOfRangeException(nameof(generation));
        lock (Gate)
        {
            s_generation = generation;
            s_ownerThread = Environment.CurrentManagedThreadId;
            s_invoker = invoker;
        }
    }

    internal static void Remove(ulong generation)
    {
        lock (Gate)
        {
            if (s_generation != generation)
                return;
            s_generation = 0;
            s_ownerThread = 0;
            s_invoker = null;
        }
    }

    internal static bool IsSupportedBufferElement(Type type) =>
        type == typeof(bool) || type == typeof(sbyte) || type == typeof(byte) || type == typeof(short) ||
        type == typeof(ushort) || type == typeof(int) || type == typeof(uint) || type == typeof(long) ||
        type == typeof(ulong) || type == typeof(float) || type == typeof(double) || type == typeof(Vector2) ||
        type == typeof(Vector3) || type == typeof(Vector4) || type == typeof(Quaternion) || type == typeof(Color) ||
        type == typeof(Guid) || type == typeof(EntityId) || type == typeof(AssetId) ||
        type == typeof(ComponentTypeId);

    private static NativeInvocationResult InvokeCore(Guid serviceId, Guid methodId,
                                                      ReadOnlySpan<NativeAbiValue> arguments)
    {
        if (serviceId == Guid.Empty || methodId == Guid.Empty)
            throw new ArgumentException("Native service and method IDs must be valid.");
        NativeServiceInvoker invoker;
        lock (Gate)
        {
            invoker = s_invoker ?? throw new InvalidOperationException(
                "Native source-module bindings are unavailable for the active managed generation.");
            if (s_ownerThread != Environment.CurrentManagedThreadId)
                throw new InvalidOperationException("Native service calls are managed-owner-thread-affine by default.");
        }
        return invoker(serviceId, methodId, arguments.ToArray());
    }

    private static T ConvertValue<T>(NativeAbiValue value)
    {
        object? boxed = value.BoxedValue;
        Type target = typeof(T);
        object converted = target switch
        {
            _ when target == typeof(bool) && value.Kind == NativeValueKind.Boolean => boxed!,
            _ when target == typeof(sbyte) && value.Kind == NativeValueKind.SignedInteger =>
                checked((sbyte)(long)boxed!),
            _ when target == typeof(short) && value.Kind == NativeValueKind.SignedInteger =>
                checked((short)(long)boxed!),
            _ when target == typeof(int) && value.Kind == NativeValueKind.SignedInteger =>
                checked((int)(long)boxed!),
            _ when target == typeof(long) && value.Kind == NativeValueKind.SignedInteger => boxed!,
            _ when target == typeof(byte) && value.Kind == NativeValueKind.UnsignedInteger =>
                checked((byte)(ulong)boxed!),
            _ when target == typeof(ushort) && value.Kind == NativeValueKind.UnsignedInteger =>
                checked((ushort)(ulong)boxed!),
            _ when target == typeof(uint) && value.Kind == NativeValueKind.UnsignedInteger =>
                checked((uint)(ulong)boxed!),
            _ when target == typeof(ulong) && value.Kind == NativeValueKind.UnsignedInteger => boxed!,
            _ when target == typeof(float) && value.Kind == NativeValueKind.FloatingPoint => (float)(double)boxed!,
            _ when target == typeof(double) && value.Kind == NativeValueKind.FloatingPoint => boxed!,
            _ when target == typeof(string) && value.Kind == NativeValueKind.Utf8String => boxed!,
            _ when target == typeof(Vector2) && value.Kind == NativeValueKind.Vector2 => boxed!,
            _ when target == typeof(Vector3) && value.Kind == NativeValueKind.Vector3 => boxed!,
            _ when target == typeof(Vector4) && value.Kind == NativeValueKind.Vector4 => boxed!,
            _ when target == typeof(Quaternion) && value.Kind == NativeValueKind.Quaternion => boxed!,
            _ when target == typeof(Color) && value.Kind == NativeValueKind.Color => boxed!,
            _ when target == typeof(Guid) && value.Kind == NativeValueKind.StableId => boxed!,
            _ when target == typeof(EntityId) && value.Kind == NativeValueKind.EntityId => boxed!,
            _ when target == typeof(AssetId) && value.Kind == NativeValueKind.AssetId => boxed!,
            _ when target == typeof(ComponentTypeId) && value.Kind == NativeValueKind.ComponentTypeId => boxed!,
            _ => throw new InvalidOperationException(
                $"Native ABI value '{value.Kind}' cannot be converted to '{target.FullName}'.")
        };
        return (T)converted;
    }
}
