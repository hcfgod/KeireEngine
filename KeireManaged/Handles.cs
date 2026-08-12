using System.Text.Json.Serialization;

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
    public bool IsValid => World != 0 && Id.IsValid && NativeRuntime.EntityExists(this);

    public string Name
    {
        get => NativeRuntime.GetEntityName(this);
        set => NativeRuntime.SetEntityName(this, value ?? throw new ArgumentNullException(nameof(value)));
    }

    public bool Active
    {
        get => NativeRuntime.GetEntityActive(this);
        set => NativeRuntime.SetEntityActive(this, value);
    }

    public bool ActiveInHierarchy => NativeRuntime.GetEntityActiveInHierarchy(this);

    public uint Layer
    {
        get => NativeRuntime.GetEntityLayer(this);
        set => NativeRuntime.SetEntityLayer(this, value);
    }

    public Entity Parent
    {
        get => NativeRuntime.GetEntityParent(this);
        set => NativeRuntime.SetEntityParent(this, value, true);
    }

    public IReadOnlyList<Entity> Children => NativeRuntime.GetEntityChildren(this);
    public TransformHandle Transform => new(this);
    public AnimatorHandle Animator => new(this);
    public CharacterControllerHandle CharacterController => new(this);
    public RigidBodyHandle RigidBody => new(this);
    public ComponentHandle GetComponent(ComponentTypeId type) =>
        NativeRuntime.ComponentExists(this, type) ? new ComponentHandle(this, type) : default;
    public ComponentHandle GetComponent<T>() => GetComponent(ComponentType.Of<T>());
    public ComponentHandle<T> GetComponentHandle<T>() => new(this);
    public bool TryGetComponent<T>(out ComponentHandle component)
    {
        component = GetComponent<T>();
        return component.IsValid;
    }
    public bool HasComponent(ComponentTypeId type) => NativeRuntime.ComponentExists(this, type);
    public bool HasComponent<T>() => HasComponent(ComponentType.Of<T>());
    public ComponentHandle AddComponent(ComponentTypeId type) =>
        NativeRuntime.AddComponent(this, type) ? new ComponentHandle(this, type) : default;
    public ComponentHandle AddComponent<T>() => AddComponent(ComponentType.Of<T>());
    public bool RemoveComponent(ComponentTypeId type) => NativeRuntime.RemoveComponent(this, type);
    public bool RemoveComponent<T>() => RemoveComponent(ComponentType.Of<T>());
    public T? GetBehaviour<T>() where T : Behaviour => BehaviourRegistry.TryGet<T>(this, out T? value) ? value : null;
    public bool TryGetBehaviour<T>(out T? behaviour) where T : Behaviour =>
        BehaviourRegistry.TryGet(this, out behaviour);
    public AudioSourceHandle AudioSource => new(this);

    public void SetParent(Entity parent, bool preserveWorldTransform = true) =>
        NativeRuntime.SetEntityParent(this, parent, preserveWorldTransform);

    public Entity FindChild(string name, bool recursive = false)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        foreach (Entity child in Children)
        {
            if (string.Equals(child.Name, name, StringComparison.Ordinal))
                return child;
            if (recursive)
            {
                Entity nested = child.FindChild(name, true);
                if (nested.Id.IsValid)
                    return nested;
            }
        }
        return default;
    }

    public Entity Find(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Entity current = this;
        foreach (string segment in path.Split('/', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (segment == ".")
                continue;
            if (segment == "..")
            {
                current = current.Parent;
                if (!current.Id.IsValid)
                    return default;
                continue;
            }
            current = current.FindChild(segment);
            if (!current.Id.IsValid)
                return default;
        }
        return current;
    }

    public Entity Instantiate() => NativeRuntime.CloneEntity(this);
    public void Destroy() => NativeRuntime.DestroyEntity(this);
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
        get => NativeRuntime.GetLocalScale(Entity);
        set => NativeRuntime.SetLocalScale(Entity, value);
    }

    public Vector3 Position
    {
        get => NativeRuntime.GetWorldPosition(Entity);
        set
        {
            ValidateFinite(value, nameof(value));
            NativeRuntime.SetWorldPosition(Entity, value);
        }
    }
    public Quaternion Rotation
    {
        get => NativeRuntime.GetWorldRotation(Entity);
        set
        {
            ValidateRotation(value, nameof(value));
            NativeRuntime.SetWorldRotation(Entity, value.Normalized);
        }
    }
    public Vector3 Forward => Rotation * Vector3.Forward;
    public Vector3 Right => Rotation * Vector3.Right;
    public Vector3 Up => Rotation * Vector3.Up;

    public void Translate(Vector3 translation,
                          bool worldSpace = false) => Position = worldSpace ? Position + translation
                                                                            : Position + (Rotation * translation);

    public void Rotate(Quaternion rotation, bool worldSpace = false)
    {
        ValidateRotation(rotation, nameof(rotation));
        Rotation = (worldSpace ? rotation * Rotation : Rotation * rotation).Normalized;
    }

    private static void ValidateFinite(Vector3 value, string parameter)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentException("Transform vectors must be finite.", parameter);
    }

    private static void ValidateRotation(Quaternion value, string parameter)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z) ||
            !float.IsFinite(value.W) ||
            ((value.X * value.X) + (value.Y * value.Y) + (value.Z * value.Z) + (value.W * value.W)) <= 0.000000000001f)
        {
            throw new ArgumentException("Transform rotations must be finite and nonzero.", parameter);
        }
    }
}

public static class ComponentType
{
    private static readonly System.Collections.Concurrent.ConcurrentDictionary<Type, ComponentTypeId> Types = new();

    public static ComponentTypeId Of<T>() => Of(typeof(T));

    public static ComponentTypeId Of(Type type)
    {
        ArgumentNullException.ThrowIfNull(type);
        return Types.GetOrAdd(type, static managedType =>
        {
            var id = managedType.GetCustomAttributes(typeof(StableComponentIdAttribute), false)
                .Cast<StableComponentIdAttribute>().SingleOrDefault() ??
                throw new InvalidOperationException(
                    $"Managed component '{managedType.FullName}' does not declare StableComponentId.");
            return new ComponentTypeId(id.High, id.Low);
        });
    }
}

public readonly record struct ComponentHandle(Entity Entity, ComponentTypeId Type)
{
    public bool IsValid => Entity.IsValid && Type.IsValid && NativeRuntime.ComponentExists(Entity, Type);

    public bool Enabled
    {
        get => NativeRuntime.GetComponentEnabled(Entity, Type);
        set => NativeRuntime.SetComponentEnabled(Entity, Type, value);
    }

    public bool Remove() => NativeRuntime.RemoveComponent(Entity, Type);
}

public readonly record struct ComponentHandle<T>(Entity Entity)
{
    public ComponentTypeId Type => ComponentType.Of<T>();
    public bool IsValid => Entity.IsValid && NativeRuntime.ComponentExists(Entity, Type);
    public ComponentHandle Untyped => IsValid ? new ComponentHandle(Entity, Type) : default;

    public bool Enabled
    {
        get => NativeRuntime.GetComponentEnabled(Entity, Type);
        set => NativeRuntime.SetComponentEnabled(Entity, Type, value);
    }

    public bool Remove() => NativeRuntime.RemoveComponent(Entity, Type);
}

public readonly record struct AssetReference<T>(AssetId Id) where T : class
{
    public bool IsValid => Id.IsValid;
    [JsonIgnore]
    public T? Value => Assets.TryLoad(this, out T? value) ? value : null;
}
