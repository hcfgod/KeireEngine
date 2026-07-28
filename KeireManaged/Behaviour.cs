using System.Collections.Concurrent;
using System.Reflection;

namespace Keire;

public readonly record struct CollisionContact(Entity Other, Vector3 Point, Vector3 Normal, float Impulse, bool Trigger);
public readonly record struct AnimationEvent(string Name, float NormalizedTime, int Integer, float Scalar, string Text);
public readonly record struct AnimationIkContext(float LayerWeight);

internal static class BehaviourRegistry
{
    private readonly record struct Key(ulong World, EntityId Entity, ComponentTypeId Component);
    private static readonly ConcurrentDictionary<Key, WeakReference<Behaviour>> Instances = new();

    internal static void Register(Behaviour behaviour)
    {
        var key = new Key(behaviour.Entity.World, behaviour.Entity.Id, ComponentType.Of(behaviour.GetType()));
        Instances[key] = new WeakReference<Behaviour>(behaviour);
    }

    internal static void Unregister(Behaviour behaviour)
    {
        if (!behaviour.Entity.Id.IsValid)
            return;
        Instances.TryRemove(
            new Key(behaviour.Entity.World, behaviour.Entity.Id, ComponentType.Of(behaviour.GetType())), out _);
    }

    internal static bool TryGet<T>(Entity entity, out T? behaviour) where T : Behaviour
    {
        var key = new Key(entity.World, entity.Id, ComponentType.Of<T>());
        if (Instances.TryGetValue(key, out WeakReference<Behaviour>? reference) &&
            reference.TryGetTarget(out Behaviour? value) && value is T typed)
        {
            behaviour = typed;
            return true;
        }
        Instances.TryRemove(key, out _);
        behaviour = null;
        return false;
    }

    internal static bool TryGet(Entity entity, ComponentTypeId component, out Behaviour? behaviour)
    {
        var key = new Key(entity.World, entity.Id, component);
        if (Instances.TryGetValue(key, out WeakReference<Behaviour>? reference) &&
            reference.TryGetTarget(out Behaviour? value))
        {
            behaviour = value;
            return true;
        }
        Instances.TryRemove(key, out _);
        behaviour = null;
        return false;
    }
}

public abstract class Behaviour
{
    private BehaviourSynchronizationContext _synchronizationContext = new();

    public Entity Entity { get; private set; }
    public bool Enabled { get; set; } = true;
    public CancellationToken LifetimeToken => _synchronizationContext.LifetimeToken;

    [NonSerialized, HideInInspector]
    public string RuntimeSerializedState = "{\"version\":1,\"fields\":[]}";

    [NonSerialized, HideInInspector]
    public string RuntimeStateWarnings = string.Empty;

    [NonSerialized, HideInInspector]
    public uint RuntimeCallbackMask;

    private const uint FixedUpdateCallback = 1U << 0;
    private const uint UpdateCallback = 1U << 1;
    private const uint LateUpdateCallback = 1U << 2;

    protected virtual void Awake() { }
    protected virtual void OnEnable() { }
    protected virtual void Start() { }
    protected virtual void FixedUpdate() { }
    protected virtual void Update() { }
    protected virtual void LateUpdate() { }
    protected virtual void OnDisable() { }
    protected virtual void OnDestroy() { }
    protected virtual void OnCollisionEnter(CollisionContact contact) { }
    protected virtual void OnCollisionStay(CollisionContact contact) { }
    protected virtual void OnCollisionExit(CollisionContact contact) { }
    protected virtual void OnTriggerEnter(CollisionContact contact) { }
    protected virtual void OnTriggerStay(CollisionContact contact) { }
    protected virtual void OnTriggerExit(CollisionContact contact) { }
    protected virtual void OnAnimationEvent(AnimationEvent animationEvent) { }
    protected virtual void OnAnimatorIk(AnimationIkContext context) { }
    protected virtual void OnBeforeReload() { }
    protected virtual void OnAfterReload() { }

    internal void Attach(Entity entity)
    {
        Entity = entity;
        _synchronizationContext = new BehaviourSynchronizationContext();
        RuntimeCallbackMask = DetectRuntimeCallbacks();
        BehaviourRegistry.Register(this);
    }

    private uint DetectRuntimeCallbacks()
    {
        var result = 0U;
        for (var type = GetType(); type is not null && type != typeof(Behaviour); type = type.BaseType)
        {
            foreach (var method in type.GetMethods(BindingFlags.Instance | BindingFlags.NonPublic |
                                                   BindingFlags.DeclaredOnly))
            {
                if (method.GetParameters().Length != 0)
                    continue;
                if (method.Name == nameof(FixedUpdate))
                    result |= FixedUpdateCallback;
                else if (method.Name == nameof(Update))
                    result |= UpdateCallback;
                else if (method.Name == nameof(LateUpdate))
                    result |= LateUpdateCallback;
            }
        }
        return result;
    }

    private void InvokeWithContext(Action callback, bool pump = false)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(_synchronizationContext);
        try
        {
            if (pump)
                _synchronizationContext.Pump();
            callback();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    // Coral enters through these non-virtual methods so gameplay callbacks retain their protected override surface.
    public void RuntimeAttach(ulong world, ulong entityHigh, ulong entityLow) =>
        Attach(new Entity(world, new EntityId(entityHigh, entityLow)));
    public uint RuntimeGetCallbackMask() => RuntimeCallbackMask;
    public void RuntimeAwake() => InvokeWithContext(Awake);
    public void RuntimeEnable()
    {
        if (_synchronizationContext.IsCancelled)
            _synchronizationContext = new BehaviourSynchronizationContext();
        InvokeWithContext(OnEnable);
    }
    public void RuntimeStart() => InvokeWithContext(Start);
    public void RuntimeFixedUpdate(float deltaSeconds) => InvokeWithContext(FixedUpdate, true);
    public void RuntimeUpdate(float deltaSeconds)
    {
        UiButton.DispatchNativeClicks();
        if ((RuntimeCallbackMask & UpdateCallback) != 0)
            InvokeWithContext(Update, true);
    }
    public void RuntimeLateUpdate() => InvokeWithContext(LateUpdate, true);
    public void RuntimeAnimationEvent(string name, float normalizedTime, int integer, float scalar, string text) =>
        InvokeWithContext(() => OnAnimationEvent(new AnimationEvent(name, normalizedTime, integer, scalar, text)),
            true);
    public void RuntimePhysicsContact(byte phase, byte trigger, ulong otherHigh, ulong otherLow, Vector3 point,
                                      Vector3 normal, float impulse)
    {
        var contact = new CollisionContact(new Entity(Entity.World, new EntityId(otherHigh, otherLow)), point, normal,
                                           impulse, trigger != 0);
        InvokeWithContext(() =>
        {
            if (trigger != 0)
            {
                if (phase == 0)
                    OnTriggerEnter(contact);
                else if (phase == 1)
                    OnTriggerStay(contact);
                else
                    OnTriggerExit(contact);
            }
            else if (phase == 0)
                OnCollisionEnter(contact);
            else if (phase == 1)
                OnCollisionStay(contact);
            else
                OnCollisionExit(contact);
        }, true);
    }
    public void RuntimeDisable()
    {
        _synchronizationContext.Cancel();
        InvokeWithContext(OnDisable);
    }
    public void RuntimeDestroy()
    {
        _synchronizationContext.Cancel();
        try
        {
            InvokeWithContext(OnDestroy);
        }
        finally
        {
            BehaviourRegistry.Unregister(this);
        }
    }
    public void RuntimeBeforeReload()
    {
        _synchronizationContext.Cancel();
        try
        {
            InvokeWithContext(OnBeforeReload);
        }
        finally
        {
            BehaviourRegistry.Unregister(this);
        }
    }
    public void RuntimeAfterReload() => InvokeWithContext(OnAfterReload);
    public void RuntimeResumeAfterFailedReload()
    {
        _synchronizationContext = new BehaviourSynchronizationContext();
        BehaviourRegistry.Register(this);
    }
    public void RuntimeCaptureReloadState() =>
        RuntimeSerializedState = ManagedStateSerializer.Capture(this, RuntimeSerializedState, true);
    public void RuntimeCapturePersistentState() =>
        RuntimeSerializedState = ManagedStateSerializer.Capture(this, RuntimeSerializedState, false);
    public void RuntimeRestoreReloadState() =>
        RuntimeStateWarnings = ManagedStateSerializer.Restore(this, RuntimeSerializedState, true);
    public void RuntimeRestorePersistentState() =>
        RuntimeStateWarnings = ManagedStateSerializer.Restore(this, RuntimeSerializedState, false);
}
