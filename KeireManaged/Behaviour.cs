using System.Collections.Concurrent;
using System.Reflection;

namespace Keire;

public readonly record struct CollisionContact(Entity Other, Vector3 Point, Vector3 Normal, float Impulse, bool Trigger);
public readonly record struct AnimationEvent(string Name, float NormalizedTime, int Integer, float Scalar, string Text);
public readonly record struct AnimationIkContext(float LayerWeight);
public enum ProceduralMotionEventType : byte
{
    FootLift,
    FootPlant,
    Takeoff,
    Apex,
    Land,
    StateChanged
}
public enum ProceduralFootSide : byte
{
    None,
    Left,
    Right
}
public readonly record struct ProceduralMotionEvent(ProceduralMotionEventType Type, ProceduralFootSide Foot,
                                                     ProceduralMotionState State, float Phase, float Intensity,
                                                     Vector3 ContactPosition, Vector3 ContactNormal, Entity Support,
                                                     AssetId PhysicsMaterial);

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
    private readonly CoroutineScheduler _coroutines = new();
    private bool _enabled = true;

    public Entity Entity { get; private set; }
    public bool Enabled
    {
        get
        {
            if (!Entity.Id.IsValid)
                return _enabled;
            var type = ComponentType.Of(GetType());
            return NativeRuntime.ComponentExists(Entity, type) ? NativeRuntime.GetComponentEnabled(Entity, type) : _enabled;
        }
        set
        {
            if (Entity.Id.IsValid)
            {
                var type = ComponentType.Of(GetType());
                if (NativeRuntime.ComponentExists(Entity, type))
                    NativeRuntime.SetComponentEnabled(Entity, type, value);
            }
            _enabled = value;
        }
    }
    public CancellationToken LifetimeToken => _synchronizationContext.LifetimeToken;

    protected Coroutine StartCoroutine(System.Collections.IEnumerator routine) => _coroutines.Start(routine);
    protected bool StopCoroutine(Coroutine coroutine) => coroutine.Stop();
    protected void StopAllCoroutines() => _coroutines.StopAll();

    [NonSerialized, HideInInspector]
    public string RuntimeSerializedState = "{\"version\":1,\"fields\":[]}";

    [NonSerialized, HideInInspector]
    public string RuntimeStateWarnings = string.Empty;

    [NonSerialized, HideInInspector]
    public uint RuntimeCallbackMask;

    private const uint FixedUpdateCallback = 1U << 0;
    private const uint UpdateCallback = 1U << 1;
    private const uint LateUpdateCallback = 1U << 2;
    private const uint AnimatorIkCallback = 1U << 3;
    private const uint AnimationEventCallback = 1U << 4;
    private const uint ProceduralMotionEventCallback = 1U << 5;

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
    protected virtual void OnProceduralMotionEvent(ProceduralMotionEvent motionEvent) { }
    protected virtual void OnAnimatorIk(AnimationIkContext context) { }
    protected virtual void OnBeforeReload() { }
    protected virtual void OnAfterReload() { }

    internal void Attach(Entity entity)
    {
        Entity = entity;
        _synchronizationContext = new BehaviourSynchronizationContext();
        _coroutines.UnhandledException = Debug.LogException;
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
                var parameters = method.GetParameters();
                if (method.Name == nameof(OnAnimatorIk) && parameters.Length == 1 &&
                    parameters[0].ParameterType == typeof(AnimationIkContext))
                    result |= AnimatorIkCallback;
                else if (method.Name == nameof(OnAnimationEvent) && parameters.Length == 1 &&
                         parameters[0].ParameterType == typeof(AnimationEvent))
                    result |= AnimationEventCallback;
                else if (method.Name == nameof(OnProceduralMotionEvent) && parameters.Length == 1 &&
                         parameters[0].ParameterType == typeof(ProceduralMotionEvent))
                    result |= ProceduralMotionEventCallback;
                else if (parameters.Length != 0)
                    continue;
                else if (method.Name == nameof(FixedUpdate))
                    result |= FixedUpdateCallback;
                else if (method.Name == nameof(Update))
                    result |= UpdateCallback;
                else if (method.Name == nameof(LateUpdate))
                    result |= LateUpdateCallback;
            }
        }
        return result | UpdateCallback;
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
        _enabled = true;
        if (_synchronizationContext.IsCancelled)
            _synchronizationContext = new BehaviourSynchronizationContext();
        InvokeWithContext(OnEnable);
    }
    public void RuntimeStart() => InvokeWithContext(Start);
    public void RuntimeFixedUpdate(float deltaSeconds) => InvokeWithContext(
        () =>
        {
            _coroutines.Pump(CoroutinePhase.FixedUpdate, deltaSeconds, deltaSeconds);
            FixedUpdate();
        }, true);
    public void RuntimeUpdate(float deltaSeconds)
    {
        UiButton.DispatchNativeClicks();
        InvokeWithContext(
            () =>
            {
                _coroutines.Pump(CoroutinePhase.Update, deltaSeconds, NativeRuntime.UnscaledDeltaTime);
                Update();
            }, true);
    }
    public void RuntimeLateUpdate() => InvokeWithContext(
        () =>
        {
            _coroutines.Pump(CoroutinePhase.LateUpdate, 0.0f, 0.0f);
            LateUpdate();
        }, true);
    public void RuntimeAnimationEvent(string name, float normalizedTime, int integer, float scalar, string text) =>
        InvokeWithContext(() => OnAnimationEvent(new AnimationEvent(name, normalizedTime, integer, scalar, text)),
            true);
    public void RuntimeAnimatorIk(float layerWeight) =>
        InvokeWithContext(() => OnAnimatorIk(new AnimationIkContext(layerWeight)), true);
    public void RuntimeProceduralMotionEvent(byte type, byte foot, byte state, float phase, float intensity,
                                             Vector3 contactPosition, Vector3 contactNormal, ulong supportHigh,
                                             ulong supportLow, ulong materialHigh, ulong materialLow) =>
        InvokeWithContext(
            () => OnProceduralMotionEvent(new ProceduralMotionEvent(
                (ProceduralMotionEventType)type, (ProceduralFootSide)foot, (ProceduralMotionState)state, phase,
                intensity, contactPosition, contactNormal,
                new Entity(Entity.World, new EntityId(supportHigh, supportLow)), new AssetId(materialHigh, materialLow))),
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
        _enabled = false;
        _coroutines.StopAll();
        _synchronizationContext.Cancel();
        InvokeWithContext(OnDisable);
    }
    public void RuntimeDestroy()
    {
        _coroutines.StopAll();
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
        _coroutines.StopAll();
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
        InvokeWithContext(OnAfterReload);
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
