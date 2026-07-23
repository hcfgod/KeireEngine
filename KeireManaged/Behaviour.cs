namespace Keire;

public readonly record struct CollisionContact(Entity Other, Vector3 Point, Vector3 Normal, float Impulse, bool Trigger);
public readonly record struct AnimationEvent(string Name, float NormalizedTime, int Integer, float Scalar, string Text);
public readonly record struct AnimationIkContext(float LayerWeight);

public abstract class Behaviour
{
    public Entity Entity { get; private set; }
    public bool Enabled { get; set; } = true;

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

    internal void Attach(Entity entity) => Entity = entity;

    // Coral enters through these non-virtual methods so gameplay callbacks retain their protected override surface.
    public void RuntimeAttach(ulong world, ulong entityHigh, ulong entityLow) =>
        Attach(new Entity(world, new EntityId(entityHigh, entityLow)));
    public void RuntimeAwake() => Awake();
    public void RuntimeEnable() => OnEnable();
    public void RuntimeStart() => Start();
    public void RuntimeFixedUpdate(float deltaSeconds) => FixedUpdate();
    public void RuntimeUpdate(float deltaSeconds) => Update();
    public void RuntimeLateUpdate() => LateUpdate();
    public void RuntimeDisable() => OnDisable();
    public void RuntimeDestroy() => OnDestroy();
    public void RuntimeBeforeReload() => OnBeforeReload();
    public void RuntimeAfterReload() => OnAfterReload();
}
