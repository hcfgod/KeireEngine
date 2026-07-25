namespace Keire;

public readonly record struct AssetId(ulong High, ulong Low)
{
    public bool IsValid => High != 0 || Low != 0;
}

public readonly record struct EntityId(ulong High, ulong Low)
{
    public bool IsValid => High != 0 || Low != 0;
}

public readonly record struct ComponentTypeId(ulong High, ulong Low)
{
    public bool IsValid => High != 0 || Low != 0;
}

public readonly record struct Entity(ulong World, EntityId Id)
{
    public bool IsValid => World != 0 && Id.IsValid && RuntimeBridge.Current.EntityExists(this);

    public string Name
    {
        get => RuntimeBridge.Current.GetEntityName(this);
        set => RuntimeBridge.Current.SetEntityName(this, value ?? throw new ArgumentNullException(nameof(value)));
    }

    public bool Active
    {
        get => RuntimeBridge.Current.GetEntityActive(this);
        set => RuntimeBridge.Current.SetEntityActive(this, value);
    }

    public Entity Parent
    {
        get => RuntimeBridge.Current.GetEntityParent(this);
        set => RuntimeBridge.Current.SetEntityParent(this, value);
    }

    public IReadOnlyList<Entity> Children => RuntimeBridge.Current.GetEntityChildren(this);
    public TransformHandle Transform => new(this);
    public ComponentHandle GetComponent(ComponentTypeId type) => RuntimeBridge.Current.GetComponent(this, type);
    public ComponentHandle GetComponent<T>() => GetComponent(ComponentType.Of<T>());
    public bool TryGetComponent<T>(out ComponentHandle component)
    {
        component = GetComponent<T>();
        return component.IsValid;
    }
    public bool HasComponent<T>() => RuntimeBridge.Current.ComponentExists(GetComponent<T>());
    public ComponentHandle AddComponent<T>() => RuntimeBridge.Current.AddComponent(this, ComponentType.Of<T>());
    public bool RemoveComponent<T>() => RuntimeBridge.Current.RemoveComponent(this, ComponentType.Of<T>());
    public Entity Instantiate() => RuntimeBridge.Current.CloneEntity(this);
    public void Destroy() => RuntimeBridge.Current.DestroyEntity(this);
}

public readonly record struct TransformHandle(Entity Entity)
{
    public Vector3 LocalPosition
    {
        get => NativeRuntime.GetLocalPosition(Entity);
        set => NativeRuntime.SetLocalPosition(Entity, value);
    }

    public Quaternion LocalRotation
    {
        get => NativeRuntime.GetLocalRotation(Entity);
        set => NativeRuntime.SetLocalRotation(Entity, value);
    }

    public Vector3 LocalScale
    {
        get => RuntimeBridge.Current.GetLocalScale(Entity);
        set => RuntimeBridge.Current.SetLocalScale(Entity, value);
    }
}

public static class ComponentType
{
    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, ComponentTypeId> Types = new();

    public static ComponentTypeId Of<T>() => Types.GetOrAdd(typeof(T), static type =>
    {
        var id = type.GetCustomAttributes(typeof(StableComponentIdAttribute), false)
            .Cast<StableComponentIdAttribute>().SingleOrDefault() ??
            throw new InvalidOperationException(
                $"Managed component '{type.FullName}' does not declare StableComponentId.");
        return new ComponentTypeId(id.High, id.Low);
    });
}

public readonly record struct ComponentHandle(Entity Entity, ComponentTypeId Type)
{
    public bool IsValid => Entity.IsValid && Type.IsValid && RuntimeBridge.Current.ComponentExists(this);
}

public readonly record struct AssetReference<T>(AssetId Id) where T : class
{
    public bool IsValid => Id.IsValid;
}
