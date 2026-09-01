namespace Keire;

public static class ManagedSerialization
{
    public static void ValidateValue(object? value, Type declaredType, string path,
                                     bool preserveReferences = false)
    {
        ArgumentNullException.ThrowIfNull(declaredType);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ManagedObjectSerializer.ValidateSerializableValue(value, declaredType, path, preserveReferences);
    }
}
