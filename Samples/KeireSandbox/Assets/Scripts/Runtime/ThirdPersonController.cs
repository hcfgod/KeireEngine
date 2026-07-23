using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000010")]
[ExecutionOrder(-100)]
public sealed class ThirdPersonController : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000011")]
    [Range(0.5, 12.0), Tooltip("Maximum character movement speed in metres per second.")]
    private float _maximumSpeed = 5.5f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000012")]
    [Range(0.0, 1.0)]
    private float _footstepVolume = 0.65f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000013")]
    private AssetReference<object> _footstepClip;

    protected override void Awake() => Log.Info($"Third-person controller attached to {Entity.Name}.");

    protected override void FixedUpdate()
    {
        var speed = Math.Clamp(Math.Abs(Input.Axis("Move")), 0.0f, 1.0f) * _maximumSpeed;
        Animator.SetFloat(Entity, "Speed", speed);
        Animator.SetBool(Entity, "Moving", speed > 0.05f);
    }

    protected override void OnAnimationEvent(AnimationEvent animationEvent)
    {
        if (animationEvent.Name == "Footstep" && _footstepClip.IsValid)
            Audio.Play(Entity, _footstepClip.Id, _footstepVolume);
    }

    protected override void OnCollisionEnter(CollisionContact contact) =>
        Debug.DrawLine(contact.Point, contact.Point + contact.Normal, new Color(1.0f, 0.6f, 0.1f), 0.25f);

    protected override void OnBeforeReload() => Log.Info("Saving controller state for managed reload.");
    protected override void OnAfterReload() => Log.Info("Controller state restored after managed reload.");
}
