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

    public ComponentHandle GetComponent(ComponentTypeId type) => RuntimeBridge.Current.GetComponent(this, type);
}

public readonly record struct ComponentHandle(Entity Entity, ComponentTypeId Type)
{
    public bool IsValid => Entity.IsValid && Type.IsValid && RuntimeBridge.Current.ComponentExists(this);
}

public readonly record struct AssetReference<T>(AssetId Id) where T : class
{
    public bool IsValid => Id.IsValid;
}
