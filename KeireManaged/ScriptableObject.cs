using System.Collections.Concurrent;

namespace Keire;

public abstract class ScriptableObject
{
    private Guid _runtimeInstanceId = Guid.NewGuid();

    public Guid RuntimeInstanceId => _runtimeInstanceId;
    public string Name { get; set; } = string.Empty;

    protected virtual void OnEnable() { }
    protected virtual void OnDisable() { }
    protected virtual void OnValidate() { }

    public static T CreateInstance<T>() where T : ScriptableObject, new()
    {
        var value = new T();
        value.OnEnable();
        value.OnValidate();
        return value;
    }

    public static T Instantiate<T>(T source) where T : ScriptableObject
    {
        ArgumentNullException.ThrowIfNull(source);
        var clone = (T)source.MemberwiseClone();
        clone._runtimeInstanceId = Guid.NewGuid();
        clone.OnEnable();
        return clone;
    }

    internal void Validate() => OnValidate();
    internal void Disable() => OnDisable();
}

public static class Assets
{
    private static readonly ConcurrentDictionary<AssetId, ScriptableObject> Loaded = new();

    public static void Register<T>(AssetId id, T asset) where T : ScriptableObject
    {
        if (!id.IsValid)
            throw new ArgumentException("Asset ID must be valid.", nameof(id));
        ArgumentNullException.ThrowIfNull(asset);
        asset.Validate();
        Loaded[id] = asset;
    }

    public static T Load<T>(AssetReference<T> reference) where T : ScriptableObject
    {
        if (TryLoad(reference, out T? value))
            return value!;
        throw new InvalidOperationException($"Managed asset {reference.Id} is not loaded as {typeof(T).FullName}.");
    }

    public static bool TryLoad<T>(AssetReference<T> reference, out T? value) where T : class
    {
        if (reference.Id.IsValid && Loaded.TryGetValue(reference.Id, out ScriptableObject? asset) && asset is T typed)
        {
            value = typed;
            return true;
        }
        value = null;
        return false;
    }

    public static ValueTask<T> LoadAsync<T>(AssetReference<T> reference,
                                            CancellationToken cancellation = default)
        where T : ScriptableObject
    {
        cancellation.ThrowIfCancellationRequested();
        return ValueTask.FromResult(Load(reference));
    }

    public static bool Unload(AssetId id)
    {
        if (!Loaded.TryRemove(id, out ScriptableObject? asset))
            return false;
        asset.Disable();
        return true;
    }
}
