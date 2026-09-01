using System.Reflection;
using System.Text.Json;

namespace Keire;

public enum NativeThreadAffinity : byte
{
    AnyThread,
    ManagedOwnerThread,
    MainThread
}

public enum NativeValueKind : byte
{
    Void,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Utf8String,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    Color,
    StableId,
    EntityId,
    AssetId,
    ComponentTypeId,
    BoundedBuffer,
    StructuredResult
}

[AttributeUsage(AttributeTargets.Interface, Inherited = false)]
public sealed class NativeServiceContractAttribute : Attribute
{
    public NativeServiceContractAttribute(string stableId, uint abiVersion = 1)
    {
        if (!Guid.TryParse(stableId, out Guid parsed) || parsed == Guid.Empty)
            throw new ArgumentException("Native service contract IDs must be non-empty GUIDs.", nameof(stableId));
        if (abiVersion == 0)
            throw new ArgumentOutOfRangeException(nameof(abiVersion));
        StableId = parsed;
        AbiVersion = abiVersion;
    }

    public Guid StableId { get; }
    public uint AbiVersion { get; }
}

[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class NativeMethodAttribute : Attribute
{
    public NativeMethodAttribute(string stableId, uint abiVersion = 1,
                                 NativeThreadAffinity threadAffinity = NativeThreadAffinity.ManagedOwnerThread)
    {
        if (!Guid.TryParse(stableId, out Guid parsed) || parsed == Guid.Empty)
            throw new ArgumentException("Native method IDs must be non-empty GUIDs.", nameof(stableId));
        if (abiVersion == 0)
            throw new ArgumentOutOfRangeException(nameof(abiVersion));
        StableId = parsed;
        AbiVersion = abiVersion;
        ThreadAffinity = threadAffinity;
    }

    public Guid StableId { get; }
    public uint AbiVersion { get; }
    public NativeThreadAffinity ThreadAffinity { get; }
}

[AttributeUsage(AttributeTargets.Parameter, Inherited = false)]
public sealed class NativeBufferAttribute : Attribute
{
    public NativeBufferAttribute(int maximumElements)
    {
        if (maximumElements is < 1 or > 65_536)
            throw new ArgumentOutOfRangeException(nameof(maximumElements));
        MaximumElements = maximumElements;
    }

    public int MaximumElements { get; }
}

public readonly record struct NativeCallError
{
    public NativeCallError(string code, string message)
    {
        if (string.IsNullOrWhiteSpace(code) || code.Length > 128)
            throw new ArgumentException("Native error codes must contain 1-128 visible characters.", nameof(code));
        if (string.IsNullOrWhiteSpace(message) || message.Length > 4_096)
            throw new ArgumentException("Native error messages must contain 1-4,096 visible characters.", nameof(message));
        Code = code;
        Message = message;
    }

    public string Code { get; }
    public string Message { get; }
}

public readonly struct NativeCallResult<T>
{
    private NativeCallResult(T? value, NativeCallError? error)
    {
        Value = value;
        Error = error;
    }

    public bool IsSuccess => Error is null;
    public T? Value { get; }
    public NativeCallError? Error { get; }

    public static NativeCallResult<T> Success(T value) => new(value, null);
    public static NativeCallResult<T> Failure(string code, string message) => new(default, new(code, message));
}

public sealed record NativeParameterDescriptor(string Name, string ManagedType, NativeValueKind Kind,
                                               NativeValueKind ElementKind, int MaximumElements);
public sealed record NativeMethodDescriptor(Guid StableId, uint AbiVersion, string Name,
                                            NativeThreadAffinity ThreadAffinity,
                                            NativeValueKind ReturnKind, NativeValueKind StructuredResultKind,
                                            IReadOnlyList<NativeParameterDescriptor> Parameters);
public sealed record NativeServiceDescriptor(Guid StableId, uint AbiVersion, string ManagedType,
                                             IReadOnlyList<NativeMethodDescriptor> Methods);

internal static class NativeServiceCatalog
{
    internal static IReadOnlyList<NativeServiceDescriptor> Discover(IEnumerable<Type> exactAllowedTypes)
    {
        ArgumentNullException.ThrowIfNull(exactAllowedTypes);
        var result = new List<NativeServiceDescriptor>();
        var serviceIds = new Dictionary<Guid, Type>();
        foreach (Type type in exactAllowedTypes.Distinct().OrderBy(type => type.FullName, StringComparer.Ordinal))
        {
            NativeServiceContractAttribute? contract =
                type.GetCustomAttribute<NativeServiceContractAttribute>(false);
            if (contract is null)
                continue;
            if (!type.IsInterface || type.ContainsGenericParameters)
                throw Invalid(type, "native service contracts must be closed interfaces");
            if (!serviceIds.TryAdd(contract.StableId, type))
            {
                throw Invalid(type,
                    $"service ID '{contract.StableId:D}' is already used by '{serviceIds[contract.StableId].FullName}'");
            }

            var methods = new List<NativeMethodDescriptor>();
            var methodIds = new Dictionary<Guid, MethodInfo>();
            foreach (MethodInfo method in type.GetMethods().OrderBy(method => method.MetadataToken))
            {
                if (method.IsStatic || method.IsGenericMethodDefinition || method.IsSpecialName)
                    throw Invalid(type, $"method '{method.Name}' must be a non-generic instance method");
                NativeMethodAttribute attribute = method.GetCustomAttribute<NativeMethodAttribute>(false) ??
                    throw Invalid(type, $"method '{method.Name}' requires NativeMethod");
                if (!methodIds.TryAdd(attribute.StableId, method))
                {
                    throw Invalid(type,
                        $"method ID '{attribute.StableId:D}' is shared by '{methodIds[attribute.StableId].Name}' and " +
                        $"'{method.Name}'");
                }
                NativeValueKind returnKind = ValueKind(method.ReturnType, method, isReturn: true,
                                                       out Type? structuredResultType);
                NativeValueKind structuredResultKind = structuredResultType is null
                    ? NativeValueKind.Void
                    : ValueKind(structuredResultType, method, isReturn: true, out _);
                var parameters = new List<NativeParameterDescriptor>();
                foreach (ParameterInfo parameter in method.GetParameters())
                {
                    if (parameter.IsOut || parameter.ParameterType.IsByRef || parameter.IsOptional)
                        throw Invalid(type, $"method '{method.Name}' cannot use ref, out, or optional parameters");
                    NativeValueKind kind = ValueKind(parameter.ParameterType, parameter, isReturn: false,
                                                     out Type? elementType);
                    NativeBufferAttribute? buffer = parameter.GetCustomAttribute<NativeBufferAttribute>(false);
                    if (kind == NativeValueKind.BoundedBuffer && buffer is null)
                    {
                        throw Invalid(type,
                            $"buffer parameter '{method.Name}.{parameter.Name}' requires NativeBuffer");
                    }
                    if (kind != NativeValueKind.BoundedBuffer && buffer is not null)
                    {
                        throw Invalid(type,
                            $"NativeBuffer can only annotate array, span, or memory parameters");
                    }
                    NativeValueKind elementKind = elementType is null
                        ? NativeValueKind.Void
                        : ValueKind(elementType, parameter, isReturn: true, out _);
                    parameters.Add(new NativeParameterDescriptor(
                        parameter.Name ?? $"arg{parameter.Position}",
                        elementType is null
                            ? parameter.ParameterType.FullName ?? parameter.ParameterType.Name
                            : elementType.FullName ?? elementType.Name,
                        kind, elementKind, buffer?.MaximumElements ?? 0));
                }
                methods.Add(new NativeMethodDescriptor(attribute.StableId, attribute.AbiVersion, method.Name,
                    attribute.ThreadAffinity, returnKind, structuredResultKind, parameters));
            }
            methods.Sort((left, right) => left.StableId.CompareTo(right.StableId));
            result.Add(new NativeServiceDescriptor(contract.StableId, contract.AbiVersion,
                type.FullName ?? type.Name, methods));
        }
        result.Sort((left, right) => left.StableId.CompareTo(right.StableId));
        return result;
    }

    internal static string Export(IEnumerable<Type> exactAllowedTypes) =>
        JsonSerializer.Serialize(Discover(exactAllowedTypes));

    private static NativeValueKind ValueKind(Type type, ICustomAttributeProvider provider, bool isReturn,
                                             out Type? elementType)
    {
        elementType = null;
        if (type == typeof(void) && isReturn)
            return NativeValueKind.Void;
        if (type == typeof(bool))
            return NativeValueKind.Boolean;
        if (type == typeof(sbyte) || type == typeof(short) || type == typeof(int) || type == typeof(long))
            return NativeValueKind.SignedInteger;
        if (type == typeof(byte) || type == typeof(ushort) || type == typeof(uint) || type == typeof(ulong))
            return NativeValueKind.UnsignedInteger;
        if (type == typeof(float) || type == typeof(double))
            return NativeValueKind.FloatingPoint;
        if (type == typeof(string))
            return NativeValueKind.Utf8String;
        if (type == typeof(Vector2))
            return NativeValueKind.Vector2;
        if (type == typeof(Vector3))
            return NativeValueKind.Vector3;
        if (type == typeof(Vector4))
            return NativeValueKind.Vector4;
        if (type == typeof(Quaternion))
            return NativeValueKind.Quaternion;
        if (type == typeof(Color))
            return NativeValueKind.Color;
        if (type == typeof(Guid))
            return NativeValueKind.StableId;
        if (type == typeof(EntityId))
            return NativeValueKind.EntityId;
        if (type == typeof(AssetId))
            return NativeValueKind.AssetId;
        if (type == typeof(ComponentTypeId))
            return NativeValueKind.ComponentTypeId;
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(NativeCallResult<>))
        {
            Type resultType = type.GetGenericArguments()[0];
            if (ValueKind(resultType, provider, true, out _) is NativeValueKind.Void or
                NativeValueKind.BoundedBuffer or NativeValueKind.StructuredResult)
            {
                throw new InvalidOperationException(
                    $"Native structured result type '{resultType.FullName}' is unsupported.");
            }
            elementType = resultType;
            return NativeValueKind.StructuredResult;
        }
        if (!isReturn && TryBufferElement(type, out elementType))
        {
            NativeValueKind elementKind = ValueKind(elementType, provider, true, out _);
            if (elementKind is NativeValueKind.Void or NativeValueKind.BoundedBuffer or NativeValueKind.StructuredResult)
            {
                throw new InvalidOperationException($"Native buffer element type '{elementType.FullName}' is unsupported.");
            }
            return NativeValueKind.BoundedBuffer;
        }
        throw new InvalidOperationException(
            $"Native ABI type '{type.FullName ?? type.Name}' is unsupported; raw pointers and unmanaged ownership " +
            "cannot cross service contracts.");
    }

    private static bool TryBufferElement(Type type, out Type elementType)
    {
        if (type.IsSZArray)
        {
            elementType = type.GetElementType()!;
            return true;
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() is Type definition &&
            (definition == typeof(ReadOnlySpan<>) || definition == typeof(Span<>) ||
             definition == typeof(ReadOnlyMemory<>) || definition == typeof(Memory<>)))
        {
            elementType = type.GetGenericArguments()[0];
            return true;
        }
        elementType = null!;
        return false;
    }

    private static InvalidOperationException Invalid(Type contract, string reason) =>
        new($"Native service contract '{contract.FullName}' is invalid: {reason}.");
}
