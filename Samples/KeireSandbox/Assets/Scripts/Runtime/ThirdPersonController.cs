using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000010")]
[ExecutionOrder(-300)]
public sealed class ThirdPersonController : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000011")]
    [Range(0.5, 12.0), Tooltip("Maximum character movement speed in metres per second.")]
    private float _maximumSpeed = 5.5f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000012")]
    [Range(0.0, 1.0)]
    private float _footstepVolume = 0.65f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000013")]
    private AudioClip? _footstepClip;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000014")]
    [Range(0.0, 40.0), Tooltip("How quickly locomotion parameters catch up with physical movement.")]
    private float _animationSharpness = 14.0f;

    [HotReloadState]
    private float _animationSpeed;

    [HotReloadState]
    private bool _moving;

    private CharacterController _motor = null!;
    private Animator _animator = null!;

    protected override void Awake()
    {
        ResolveMotor();
        Log.Info($"Third-person animation controller attached to {Entity.Name}.");
    }

    protected override void OnEnable() => ResolveMotor();

    protected override void FixedUpdate()
    {
        CharacterControllerState state = _motor.State;
        float targetSpeed = MathF.Sqrt(
            (state.Velocity.X * state.Velocity.X) + (state.Velocity.Z * state.Velocity.Z));
        targetSpeed = Math.Clamp(targetSpeed, 0.0f, MathF.Max(0.0f, _maximumSpeed));
        float deltaTime = MathF.Min(MathF.Max(0.0f, Time.FixedDeltaTime), 0.05f);
        float blend = 1.0f - MathF.Exp(-MathF.Max(0.0f, _animationSharpness) * deltaTime);
        _animationSpeed += (targetSpeed - _animationSpeed) * blend;
        _moving = _moving ? _animationSpeed > 0.12f : _animationSpeed > 0.22f;
        _animator.SetFloat("Speed", _animationSpeed);
        _animator.SetBool("Moving", _moving);
        _animator.SetFloat("VerticalSpeed", state.Velocity.Y);
        _animator.SetBool("Grounded", state.Grounded);
        _animator.SetBool("Falling", !state.Grounded && state.Velocity.Y < -0.5f);
    }

    protected override void OnAnimationEvent(AnimationEvent animationEvent)
    {
        if (animationEvent.Name == "Footstep" && _footstepClip is not null)
            Audio.Play(Entity, _footstepClip, new AudioPlaybackOptions { Gain = _footstepVolume });
    }

    protected override void OnCollisionEnter(CollisionContact contact) =>
        Debug.DrawLine(contact.Point, contact.Point + contact.Normal, new Color(1.0f, 0.6f, 0.1f), 0.25f);

    protected override void OnBeforeReload() => Log.Info("Saving controller state for managed reload.");
    protected override void OnAfterReload()
    {
        ResolveMotor();
        Log.Info("Controller state restored after managed reload.");
    }

    private void ResolveMotor()
    {
        _motor = GetComponentInParent<CharacterController>() ??
            throw new InvalidOperationException(
                $"{nameof(ThirdPersonController)} requires a Character Controller on its Entity or an ancestor.");
        _animator = GetComponent<Animator>() ??
            throw new InvalidOperationException($"{nameof(ThirdPersonController)} requires an Animator.");
    }
}
