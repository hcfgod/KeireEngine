namespace Keire;

public sealed class ManagedSerializationException : InvalidOperationException
{
    internal ManagedSerializationException(string code, string fieldPath, Type declaredType, Type? runtimeType,
                                           string reason, Exception? innerException = null,
                                           string phase = "validate", string? owner = null,
                                           string? rootField = null, string? serializedTypeId = null,
                                           int? objectId = null)
        : base(FormatMessage(code, fieldPath, declaredType, runtimeType, reason), innerException)
    {
        Code = code;
        FieldPath = fieldPath;
        DeclaredType = declaredType;
        RuntimeType = runtimeType;
        Reason = reason;
        Phase = phase;
        Owner = owner ?? TypeName(declaredType);
        RootField = rootField ?? fieldPath;
        SerializedTypeId = serializedTypeId;
        ObjectId = objectId;
    }

    public string Code { get; }
    public string Phase { get; private set; }
    public string Owner { get; private set; }
    public string RootField { get; private set; }
    public string FieldPath { get; }
    public Type DeclaredType { get; }
    public Type? RuntimeType { get; }
    public string? SerializedTypeId { get; private set; }
    public int? ObjectId { get; private set; }
    public string Reason { get; }

    internal ManagedSerializationException WithContext(string phase, string owner, string rootField,
                                                       string? serializedTypeId = null, int? objectId = null)
    {
        Phase = phase;
        Owner = owner;
        RootField = rootField;
        if (serializedTypeId is not null)
            SerializedTypeId = serializedTypeId;
        if (objectId is not null)
            ObjectId = objectId;
        return this;
    }

    internal ManagedSerializationException WithGraphNode(string phase, string? serializedTypeId, int objectId)
    {
        Phase = phase;
        if (!string.IsNullOrWhiteSpace(serializedTypeId))
            SerializedTypeId = serializedTypeId;
        ObjectId = objectId;
        return this;
    }

    private static string FormatMessage(string code, string fieldPath, Type declaredType, Type? runtimeType,
                                        string reason) =>
        $"{code}: Managed field '{fieldPath}' declared as '{TypeName(declaredType)}'" +
        (runtimeType is null ? string.Empty : $" with runtime type '{TypeName(runtimeType)}'") +
        $" is invalid: {reason}.";

    private static string TypeName(Type type) => type.FullName ?? type.Name;
}
