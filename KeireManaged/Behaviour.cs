namespace Keire;

public readonly record struct CollisionContact(Entity Other, Vector3 Point, Vector3 Normal, float Impulse, bool Trigger);
public readonly record struct AnimationEvent(string Name, float NormalizedTime, int Integer, float Scalar, string Text);
public readonly record struct AnimationIkContext(float LayerWeight);

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
    public void RuntimeAwake() => InvokeWithContext(Awake);
    public void RuntimeEnable()
    {
        if (_synchronizationContext.IsCancelled)
            _synchronizationContext = new BehaviourSynchronizationContext();
        InvokeWithContext(OnEnable);
    }
    public void RuntimeStart() => InvokeWithContext(Start);
    public void RuntimeFixedUpdate(float deltaSeconds) => InvokeWithContext(FixedUpdate, true);
    public void RuntimeUpdate(float deltaSeconds) => InvokeWithContext(Update, true);
    public void RuntimeLateUpdate() => InvokeWithContext(LateUpdate, true);
    public void RuntimeDisable()
    {
        _synchronizationContext.Cancel();
        InvokeWithContext(OnDisable);
    }
    public void RuntimeDestroy()
    {
        _synchronizationContext.Cancel();
        InvokeWithContext(OnDestroy);
    }
    public void RuntimeBeforeReload()
    {
        _synchronizationContext.Cancel();
        InvokeWithContext(OnBeforeReload);
    }
    public void RuntimeAfterReload() => InvokeWithContext(OnAfterReload);
    public void RuntimeResumeAfterFailedReload() =>
        _synchronizationContext = new BehaviourSynchronizationContext();
    public void RuntimeCaptureReloadState() =>
        RuntimeSerializedState = ManagedStateSerializer.Capture(this, RuntimeSerializedState, true);
    public void RuntimeCapturePersistentState() =>
        RuntimeSerializedState = ManagedStateSerializer.Capture(this, RuntimeSerializedState, false);
    public void RuntimeRestoreReloadState() =>
        RuntimeStateWarnings = ManagedStateSerializer.Restore(this, RuntimeSerializedState, true);
    public void RuntimeRestorePersistentState() =>
        RuntimeStateWarnings = ManagedStateSerializer.Restore(this, RuntimeSerializedState, false);
}
