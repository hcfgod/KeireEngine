using Keire;

namespace ScriptingExamples;

// Assign a target entity, then replace the three bone names with one chain from your imported skeleton.
[StableComponentId("f3cf2475-eaef-40ba-a384-e181206f0471")]
public sealed class CreatureLimbGoal : Behaviour
{
    [SerializeField, StableFieldId("a3da62df-1a99-4f61-919b-5bd260892ef3")]
    private Entity? _target = null;
    private Animator? _animator;
    private LimbIkRig? _rig;
    private static readonly LimbId FrontLeft = new(1);

    protected override void OnEnable()
    {
        _animator = GetComponent<Animator>();
        if (_animator is not null)
            _rig = new LimbIkRig(_animator, "CreatureLimbGoal", new[]
            {
                new LimbIkDefinition(FrontLeft, "Front left", new[] { "LegRoot", "LegMiddle", "LegTip" })
            });
    }

    protected override void OnAnimatorIk(AnimationIkContext context)
    {
        if (_rig is null)
            return;
        if (_target is not { IsValid: true })
        {
            _rig.Clear(FrontLeft);
            return;
        }
        // Fixed evaluations use simulation coordinates; rendered evaluations use the presented transform.
        var position = context.IsFixedUpdate ? _target.Transform.Position : _target.Transform.PresentationPosition;
        var space = context.IsFixedUpdate ? AnimatorIkSpace.World : AnimatorIkSpace.PresentationWorld;
        // Pole is a point in the same coordinate space, not a direction from the world origin.
        _rig.SetTarget(FrontLeft, new(position, position + Vector3.Up, 1, space));
    }

    protected override void OnDisable()
    {
        if (_animator is { IsValid: true })
            _rig?.ClearAll();
        _rig = null;
        _animator = null;
    }
}
