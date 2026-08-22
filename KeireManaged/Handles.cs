using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;

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

public abstract class EngineObject
{
    public abstract bool IsValid { get; }
    public virtual string Name { get; set; } = string.Empty;

    public static Entity Instantiate(Prefab prefab) => prefab.Instantiate();
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation) =>
        prefab.Instantiate(position, rotation);
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity? parent) =>
        prefab.Instantiate(position, rotation, parent);
    public static Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity? parent,
                                     bool active) => prefab.Instantiate(position, rotation, parent, active);
    public static Entity Instantiate(Entity original) => original.Instantiate();

    public static void Destroy(EngineObject value)
    {
        ArgumentNullException.ThrowIfNull(value);
        switch (value)
        {
            case Entity entity:
                entity.Destroy();
                break;
            case Component component:
                component.Destroy();
                break;
            case ScriptableObject scriptable when !scriptable.IsPersistent:
                scriptable.DestroyTransient();
                break;
            default:
                throw new InvalidOperationException($"'{value.GetType().FullName}' cannot be destroyed at runtime.");
        }
    }

    public static bool DontDestroyOnLoad(Entity entity)
    {
        ArgumentNullException.ThrowIfNull(entity);
        return SceneManager.Preserve(entity);
    }
}

public abstract class Asset : EngineObject, IEquatable<Asset>
{
    private static readonly ConcurrentDictionary<(Type Type, AssetId Id), WeakReference<Asset>> Cache = new();
    private AssetId _id;

    public AssetId Id => _id;
    public bool IsPersistent => _id.IsValid;
    public override bool IsValid => _id.IsValid;

    internal void BindAsset(AssetId id)
    {
        if (_id.IsValid && _id != id)
            throw new InvalidOperationException("An asset object cannot be rebound to another asset identity.");
        _id = id;
    }

    internal static T? FromId<T>(AssetId id) where T : Asset => (T?)FromId(typeof(T), id);

    internal static Asset? FromId(Type type, AssetId id)
    {
        ArgumentNullException.ThrowIfNull(type);
        if (!id.IsValid)
            return null;
        if (!typeof(Asset).IsAssignableFrom(type) || type.IsAbstract)
            throw new ArgumentException($"'{type.FullName}' is not a concrete Kéire asset type.", nameof(type));

        var key = (type, id);
        while (true)
        {
            if (Cache.TryGetValue(key, out WeakReference<Asset>? reference))
            {
                if (reference.TryGetTarget(out Asset? value))
                    return value;
                var replacement = Create(type, id);
                if (Cache.TryUpdate(key, new WeakReference<Asset>(replacement), reference))
                    return replacement;
                continue;
            }
            var created = Create(type, id);
            if (Cache.TryAdd(key, new WeakReference<Asset>(created)))
                return created;
        }

        static Asset Create(Type assetType, AssetId assetId)
        {
            var created = (Asset)(Activator.CreateInstance(assetType, nonPublic: true) ??
                                  throw new InvalidOperationException($"Asset type '{assetType.FullName}' cannot be created."));
            created.BindAsset(assetId);
            return created;
        }
    }

    public bool Equals(Asset? other) => ReferenceEquals(this, other) ||
        (other is not null && IsPersistent && other.IsPersistent && GetType() == other.GetType() && Id == other.Id);
    public override bool Equals(object? value) => value is Asset other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(GetType(), Id);
    public static bool operator ==(Asset? left, Asset? right) => Equals(left, right);
    public static bool operator !=(Asset? left, Asset? right) => !Equals(left, right);
}

public sealed class Entity : EngineObject, IEquatable<Entity>
{
    private static readonly ConcurrentDictionary<(ulong World, EntityId Id), WeakReference<Entity>> Cache = new();
    private readonly ConcurrentDictionary<Type, WeakReference<Component>> _components = new();

    internal Entity(ulong world, EntityId id) => (World, Id) = (world, id);

    public ulong World { get; }
    public EntityId Id { get; }
    public override bool IsValid => World != 0 && Id.IsValid && NativeRuntime.EntityExists(this);

    internal static Entity? FromId(ulong world, EntityId id)
    {
        if (world == 0 || !id.IsValid)
            return null;
        var key = (world, id);
        while (true)
        {
            if (Cache.TryGetValue(key, out WeakReference<Entity>? reference))
            {
                if (reference.TryGetTarget(out Entity? value))
                    return value;
                var replacement = new Entity(world, id);
                if (Cache.TryUpdate(key, new WeakReference<Entity>(replacement), reference))
                    return replacement;
                continue;
            }
            var created = new Entity(world, id);
            if (Cache.TryAdd(key, new WeakReference<Entity>(created)))
                return created;
        }
    }

    public override string Name
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

    public IReadOnlyList<string> Tags => NativeWorld.GetEntityTags(this);
    public Entity? Parent
    {
        get => NativeRuntime.GetEntityParent(this);
        set => NativeRuntime.SetEntityParent(this, value, true);
    }
    public IReadOnlyList<Entity> Children => NativeRuntime.GetEntityChildren(this);
    public Transform Transform => GetComponent<Transform>() ??
        throw new InvalidOperationException("A live entity must always have a Transform component.");

    public bool HasTag(string tag)
    {
        EntityTag.Validate(tag, nameof(tag));
        return Tags.Contains(tag, StringComparer.Ordinal);
    }

    public bool AddTag(string tag)
    {
        EntityTag.Validate(tag, nameof(tag));
        return NativeWorld.AddEntityTag(this, tag);
    }

    public bool RemoveTag(string tag)
    {
        EntityTag.Validate(tag, nameof(tag));
        return NativeWorld.RemoveEntityTag(this, tag);
    }

    public void ClearTags() => NativeWorld.ClearEntityTags(this);

    public Component? GetComponent(Type type)
    {
        ValidateComponentQueryType(type);
        if (!type.IsInterface && !type.IsAbstract && type.IsDefined(typeof(StableComponentIdAttribute), false))
            return GetConcreteComponent(type);
        return GetComponents(type).FirstOrDefault();
    }

    public T? GetComponent<T>() where T : class => GetComponent(typeof(T)) as T;

    public bool TryGetComponent<T>([NotNullWhen(true)] out T? component) where T : class
    {
        component = GetComponent<T>();
        return component is not null;
    }

    public bool TryGetComponent(Type type, [NotNullWhen(true)] out Component? component)
    {
        component = GetComponent(type);
        return component is not null;
    }

    public Component[] GetComponents(Type type)
    {
        ValidateComponentQueryType(type);
        return ComponentType.AssignableTypes(type).Select(GetConcreteComponent).OfType<Component>().ToArray();
    }

    public T[] GetComponents<T>() where T : class => GetComponents(typeof(T)).OfType<T>().ToArray();

    public void GetComponents(Type type, List<Component> results)
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponents(type));
    }

    public void GetComponents<T>(List<T> results) where T : class
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponents<T>());
    }

    public T? GetComponentInChildren<T>(bool includeInactive = false) where T : class =>
        EnumerateHierarchy(includeInactive).Select(entity => entity.GetComponent<T>())
            .FirstOrDefault(value => value is not null);

    public Component? GetComponentInChildren(Type type, bool includeInactive = false)
    {
        ValidateComponentQueryType(type);
        return EnumerateHierarchy(includeInactive).Select(entity => entity.GetComponent(type))
            .FirstOrDefault(value => value is not null);
    }

    public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : class =>
        EnumerateHierarchy(includeInactive).SelectMany(entity => entity.GetComponents<T>()).ToArray();

    public Component[] GetComponentsInChildren(Type type, bool includeInactive = false)
    {
        ValidateComponentQueryType(type);
        return EnumerateHierarchy(includeInactive).SelectMany(entity => entity.GetComponents(type)).ToArray();
    }

    public void GetComponentsInChildren(Type type, List<Component> results, bool includeInactive = false)
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponentsInChildren(type, includeInactive));
    }

    public void GetComponentsInChildren<T>(List<T> results, bool includeInactive = false) where T : class
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponentsInChildren<T>(includeInactive));
    }

    public T? GetComponentInParent<T>(bool includeInactive = false) where T : class =>
        EnumerateParents(includeInactive).Select(entity => entity.GetComponent<T>())
            .FirstOrDefault(value => value is not null);

    public Component? GetComponentInParent(Type type, bool includeInactive = false)
    {
        ValidateComponentQueryType(type);
        return EnumerateParents(includeInactive).Select(entity => entity.GetComponent(type))
            .FirstOrDefault(value => value is not null);
    }

    public T[] GetComponentsInParent<T>(bool includeInactive = false) where T : class =>
        EnumerateParents(includeInactive).SelectMany(entity => entity.GetComponents<T>()).ToArray();

    public Component[] GetComponentsInParent(Type type, bool includeInactive = false)
    {
        ValidateComponentQueryType(type);
        return EnumerateParents(includeInactive).SelectMany(entity => entity.GetComponents(type)).ToArray();
    }

    public void GetComponentsInParent(Type type, List<Component> results, bool includeInactive = false)
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponentsInParent(type, includeInactive));
    }

    public void GetComponentsInParent<T>(List<T> results, bool includeInactive = false) where T : class
    {
        ArgumentNullException.ThrowIfNull(results);
        results.Clear();
        results.AddRange(GetComponentsInParent<T>(includeInactive));
    }

    public Component AddComponent(Type type)
    {
        ValidateComponentQueryType(type);
        if (type.IsAbstract || type.IsInterface)
            throw new ArgumentException("Only a concrete component type can be added.", nameof(type));
        ComponentTypeId componentType = ComponentType.Of(type);
        if (!NativeRuntime.AddComponent(this, componentType))
            throw new InvalidOperationException($"The runtime rejected component '{type.FullName}'.");
        return GetConcreteComponent(type) ??
               throw new InvalidOperationException($"Component '{type.FullName}' was added but could not be bound.");
    }

    public T AddComponent<T>() where T : Component => (T)AddComponent(typeof(T));
    public bool HasComponent<T>() where T : class => GetComponent<T>() is not null;
    public bool HasComponent(ComponentTypeId type) => NativeRuntime.ComponentExists(this, type);

    public bool RemoveComponent<T>() where T : Component => RemoveComponent(typeof(T));
    public bool RemoveComponent(Type type)
    {
        ComponentTypeId componentType = ComponentType.Of(type);
        bool removed = NativeRuntime.RemoveComponent(this, componentType);
        if (removed)
            _components.TryRemove(type, out _);
        return removed;
    }

    public void SetParent(Entity? parent, bool preserveWorldTransform = true) =>
        NativeRuntime.SetEntityParent(this, parent, preserveWorldTransform);

    public Entity? FindChild(string name, bool recursive = false)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        foreach (Entity child in Children)
        {
            if (string.Equals(child.Name, name, StringComparison.Ordinal))
                return child;
            if (recursive && child.FindChild(name, true) is { } nested)
                return nested;
        }
        return null;
    }

    public Entity? Find(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Entity? current = this;
        foreach (string segment in path.Split('/', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (segment == ".")
                continue;
            current = segment == ".." ? current?.Parent : current?.FindChild(segment);
            if (current is null)
                return null;
        }
        return current;
    }

    public Entity Instantiate() => NativeRuntime.CloneEntity(this);
    public void Destroy() => NativeRuntime.DestroyEntity(this);

    internal Component? GetConcreteComponent(Type type)
    {
        ComponentTypeId componentType = ComponentType.Of(type);
        if (!NativeRuntime.ComponentExists(this, componentType))
            return null;
        if (typeof(Behaviour).IsAssignableFrom(type))
            return BehaviourRegistry.TryGet(this, componentType, out Behaviour? behaviour) ? behaviour : null;
        if (_components.TryGetValue(type, out WeakReference<Component>? reference) && reference.TryGetTarget(out Component? value))
            return value;
        var created = (Component)(Activator.CreateInstance(type, BindingFlags.Instance | BindingFlags.Public |
            BindingFlags.NonPublic, binder: null, args: [this], culture: null) ??
            throw new InvalidOperationException($"Component type '{type.FullName}' requires an Entity constructor."));
        _components[type] = new WeakReference<Component>(created);
        return created;
    }

    private IEnumerable<Entity> EnumerateHierarchy(bool includeInactive)
    {
        var stack = new Stack<Entity>();
        stack.Push(this);
        while (stack.Count > 0)
        {
            Entity current = stack.Pop();
            if (ReferenceEquals(current, this) || includeInactive || current.ActiveInHierarchy)
                yield return current;
            IReadOnlyList<Entity> children = current.Children;
            for (int index = children.Count - 1; index >= 0; --index)
                stack.Push(children[index]);
        }
    }

    private IEnumerable<Entity> EnumerateParents(bool includeInactive)
    {
        for (Entity? current = this; current is not null; current = current.Parent)
            if (ReferenceEquals(current, this) || includeInactive || current.ActiveInHierarchy)
                yield return current;
    }

    private static void ValidateComponentQueryType(Type type)
    {
        ArgumentNullException.ThrowIfNull(type);
        if (!type.IsInterface && !typeof(Component).IsAssignableFrom(type))
            throw new ArgumentException($"'{type.FullName}' is not a component type.", nameof(type));
    }

    public bool Equals(Entity? other) => other is not null && World == other.World && Id == other.Id;
    public override bool Equals(object? value) => value is Entity other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(World, Id);
    public static bool operator ==(Entity? left, Entity? right) => Equals(left, right);
    public static bool operator !=(Entity? left, Entity? right) => !Equals(left, right);
}

public abstract class Component : EngineObject, IEquatable<Component>
{
    protected Component() => Entity = null!;
    protected Component(Entity entity) => Entity = entity ?? throw new ArgumentNullException(nameof(entity));

    public Entity Entity { get; internal set; }
    public Transform Transform => Entity.Transform;
    public override string Name { get => Entity.Name; set => Entity.Name = value; }
    public ComponentTypeId Type => ComponentType.Of(GetType());
    public override bool IsValid => Entity.IsValid && NativeRuntime.ComponentExists(Entity, Type);
    public virtual bool Enabled
    {
        get => IsValid && NativeRuntime.GetComponentEnabled(Entity, Type);
        set => NativeRuntime.SetComponentEnabled(Entity, Type, value);
    }
    public bool IsActiveAndEnabled => Enabled && Entity.ActiveInHierarchy;

    protected bool GetBuiltinBoolean(string key) => NativeRuntime.GetBuiltinComponentProperty(this, key).Integer != 0;
    protected long GetBuiltinInteger(string key) => NativeRuntime.GetBuiltinComponentProperty(this, key).Integer;
    protected float GetBuiltinScalar(string key) => (float)NativeRuntime.GetBuiltinComponentProperty(this, key).Scalar;
    protected Vector2 GetBuiltinVector2(string key)
    {
        Vector4 value = NativeRuntime.GetBuiltinComponentProperty(this, key).Vector;
        return new Vector2(value.X, value.Y);
    }
    protected Vector3 GetBuiltinVector3(string key)
    {
        Vector4 value = NativeRuntime.GetBuiltinComponentProperty(this, key).Vector;
        return new Vector3(value.X, value.Y, value.Z);
    }
    protected Vector4 GetBuiltinVector4(string key) => NativeRuntime.GetBuiltinComponentProperty(this, key).Vector;
    protected Color GetBuiltinColor(string key)
    {
        Vector4 value = NativeRuntime.GetBuiltinComponentProperty(this, key).Vector;
        return new Color(value.X, value.Y, value.Z, value.W);
    }
    protected string GetBuiltinText(string key) => NativeRuntime.GetBuiltinComponentText(this, key);
    protected T? GetBuiltinAsset<T>(string key) where T : Asset
    {
        NativeBuiltinProperty value = NativeRuntime.GetBuiltinComponentProperty(this, key);
        return Asset.FromId<T>(new AssetId(value.High, value.Low));
    }
    protected Entity? GetBuiltinEntity(string key)
    {
        NativeBuiltinProperty value = NativeRuntime.GetBuiltinComponentProperty(this, key);
        return Entity.FromId(Entity.World, new EntityId(value.High, value.Low));
    }
    protected void SetBuiltinBoolean(string key, bool value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty { Kind = NativeBuiltinPropertyKind.Boolean, Integer = value ? 1 : 0 });
    protected void SetBuiltinInteger(string key, long value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty { Kind = NativeBuiltinPropertyKind.Integer, Integer = value });
    protected void SetBuiltinScalar(string key, float value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty { Kind = NativeBuiltinPropertyKind.Scalar, Scalar = value });
    protected void SetBuiltinVector2(string key, Vector2 value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty
        {
            Kind = NativeBuiltinPropertyKind.Vector2,
            Vector = new Vector4(value.X, value.Y, 0.0f, 0.0f)
        });
    protected void SetBuiltinVector3(string key, Vector3 value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty
        {
            Kind = NativeBuiltinPropertyKind.Vector3,
            Vector = new Vector4(value.X, value.Y, value.Z, 0.0f)
        });
    protected void SetBuiltinVector4(string key, Vector4 value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty { Kind = NativeBuiltinPropertyKind.Vector4, Vector = value });
    protected void SetBuiltinColor(string key, Color value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty
        {
            Kind = NativeBuiltinPropertyKind.Color,
            Vector = new Vector4(value.Red, value.Green, value.Blue, value.Alpha)
        });
    protected void SetBuiltinText(string key, string value) => NativeRuntime.SetBuiltinComponentText(this, key, value);
    protected void SetBuiltinAsset(string key, Asset? value) => NativeRuntime.SetBuiltinComponentProperty(
        this, key, new NativeBuiltinProperty
        {
            Kind = NativeBuiltinPropertyKind.Asset,
            High = value?.Id.High ?? 0,
            Low = value?.Id.Low ?? 0
        });
    protected void SetBuiltinEntity(string key, Entity? value)
    {
        if (value is not null && value.World != Entity.World)
            throw new ArgumentException("A component can reference only an entity in its own runtime world.", nameof(value));
        NativeRuntime.SetBuiltinComponentProperty(this, key, new NativeBuiltinProperty
        {
            Kind = NativeBuiltinPropertyKind.Entity,
            High = value?.Id.High ?? 0,
            Low = value?.Id.Low ?? 0
        });
    }

    public T? GetComponent<T>() where T : class => Entity.GetComponent<T>();
    public Component? GetComponent(Type type) => Entity.GetComponent(type);
    public bool TryGetComponent<T>([NotNullWhen(true)] out T? component) where T : class =>
        Entity.TryGetComponent(out component);
    public bool TryGetComponent(Type type, [NotNullWhen(true)] out Component? component) =>
        Entity.TryGetComponent(type, out component);
    public T[] GetComponents<T>() where T : class => Entity.GetComponents<T>();
    public Component[] GetComponents(Type type) => Entity.GetComponents(type);
    public void GetComponents<T>(List<T> results) where T : class => Entity.GetComponents(results);
    public void GetComponents(Type type, List<Component> results) => Entity.GetComponents(type, results);
    public T? GetComponentInChildren<T>(bool includeInactive = false) where T : class =>
        Entity.GetComponentInChildren<T>(includeInactive);
    public Component? GetComponentInChildren(Type type, bool includeInactive = false) =>
        Entity.GetComponentInChildren(type, includeInactive);
    public T[] GetComponentsInChildren<T>(bool includeInactive = false) where T : class =>
        Entity.GetComponentsInChildren<T>(includeInactive);
    public Component[] GetComponentsInChildren(Type type, bool includeInactive = false) =>
        Entity.GetComponentsInChildren(type, includeInactive);
    public void GetComponentsInChildren<T>(List<T> results, bool includeInactive = false) where T : class =>
        Entity.GetComponentsInChildren(results, includeInactive);
    public void GetComponentsInChildren(Type type, List<Component> results, bool includeInactive = false) =>
        Entity.GetComponentsInChildren(type, results, includeInactive);
    public T? GetComponentInParent<T>(bool includeInactive = false) where T : class =>
        Entity.GetComponentInParent<T>(includeInactive);
    public Component? GetComponentInParent(Type type, bool includeInactive = false) =>
        Entity.GetComponentInParent(type, includeInactive);
    public T[] GetComponentsInParent<T>(bool includeInactive = false) where T : class =>
        Entity.GetComponentsInParent<T>(includeInactive);
    public Component[] GetComponentsInParent(Type type, bool includeInactive = false) =>
        Entity.GetComponentsInParent(type, includeInactive);
    public void GetComponentsInParent<T>(List<T> results, bool includeInactive = false) where T : class =>
        Entity.GetComponentsInParent(results, includeInactive);
    public void GetComponentsInParent(Type type, List<Component> results, bool includeInactive = false) =>
        Entity.GetComponentsInParent(type, results, includeInactive);
    public void Destroy() => Entity.RemoveComponent(GetType());

    public bool Equals(Component? other) => other is not null && Entity == other.Entity && Type == other.Type;
    public override bool Equals(object? value) => value is Component other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Entity, Type);
}

[StableComponentId("4b454952-4554-5241-4e53-464f524d0001")]
public sealed class Transform : Component
{
    internal Transform(Entity entity) : base(entity) { }

    public Vector3 LocalPosition { get => NativeRuntime.GetLocalPosition(Entity); set => NativeRuntime.SetLocalPosition(Entity, value); }
    public Quaternion LocalRotation { get => NativeRuntime.GetLocalRotation(Entity); set => NativeRuntime.SetLocalRotation(Entity, value); }
    public Vector3 LocalScale { get => NativeRuntime.GetLocalScale(Entity); set => NativeRuntime.SetLocalScale(Entity, value); }
    public Vector3 Position
    {
        get => NativeRuntime.GetWorldPosition(Entity);
        set { ValidateFinite(value, nameof(value)); NativeRuntime.SetWorldPosition(Entity, value); }
    }
    public Quaternion Rotation
    {
        get => NativeRuntime.GetWorldRotation(Entity);
        set { ValidateRotation(value, nameof(value)); NativeRuntime.SetWorldRotation(Entity, value.Normalized); }
    }
    public Vector3 PresentationPosition => NativeRuntime.GetPresentationWorldPosition(Entity);
    public Quaternion PresentationRotation => NativeRuntime.GetPresentationWorldRotation(Entity);
    public Vector3 Forward => Rotation * Vector3.Forward;
    public Vector3 Right => Rotation * Vector3.Right;
    public Vector3 Up => Rotation * Vector3.Up;
    public void Translate(Vector3 translation, bool worldSpace = false) =>
        Position = worldSpace ? Position + translation : Position + (Rotation * translation);
    public void Rotate(Quaternion rotation, bool worldSpace = false)
    {
        ValidateRotation(rotation, nameof(rotation));
        Rotation = (worldSpace ? rotation * Rotation : Rotation * rotation).Normalized;
    }
    public void ResetPresentationInterpolation() => NativeRuntime.ResetPresentationInterpolation(Entity);

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
            throw new ArgumentException("Transform rotations must be finite and nonzero.", parameter);
    }
}

internal static class EntityTag
{
    internal static void Validate(string tag, string parameter)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(tag, parameter);
        if (tag.Length > 64 || !IsAsciiLetter(tag[0]) ||
            tag.Any(static value => !IsAsciiLetterOrDigit(value) && value is not '_' and not '-' and not '.'))
            throw new ArgumentException("Entity tags must be 1..64 byte ASCII identifiers beginning with a letter.", parameter);
    }
    private static bool IsAsciiLetter(char value) => value is >= 'A' and <= 'Z' or >= 'a' and <= 'z';
    private static bool IsAsciiLetterOrDigit(char value) => IsAsciiLetter(value) || value is >= '0' and <= '9';
}

public static class ComponentType
{
    private static readonly ConcurrentDictionary<Type, ComponentTypeId> Types = new();

    public static ComponentTypeId Of<T>() => Of(typeof(T));
    public static ComponentTypeId Of(Type type)
    {
        ArgumentNullException.ThrowIfNull(type);
        return Types.GetOrAdd(type, Resolve);
    }

    internal static IEnumerable<Type> AssignableTypes(Type requested)
    {
        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type[] candidates;
            try { candidates = assembly.GetTypes(); }
            catch (ReflectionTypeLoadException exception) { candidates = exception.Types.OfType<Type>().ToArray(); }
            foreach (Type candidate in candidates)
                if (!candidate.IsAbstract && typeof(Component).IsAssignableFrom(candidate) &&
                    requested.IsAssignableFrom(candidate) && candidate.IsDefined(typeof(StableComponentIdAttribute), false))
                    _ = Types.GetOrAdd(candidate, Resolve);
        }
        return Types.Keys.Where(requested.IsAssignableFrom)
            .OrderBy(type => Types[type].High).ThenBy(type => Types[type].Low).ThenBy(type => type.FullName, StringComparer.Ordinal);
    }

    internal static Type? FromId(ComponentTypeId id, Type requested) =>
        AssignableTypes(requested).FirstOrDefault(type => Of(type) == id);

    private static ComponentTypeId Resolve(Type managedType)
    {
        StableComponentIdAttribute id = managedType.GetCustomAttributes(typeof(StableComponentIdAttribute), false)
            .Cast<StableComponentIdAttribute>().SingleOrDefault() ??
            throw new InvalidOperationException($"Managed component '{managedType.FullName}' does not declare StableComponentId.");
        return new ComponentTypeId(id.High, id.Low);
    }
}
