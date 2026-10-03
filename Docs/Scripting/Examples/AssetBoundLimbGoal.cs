using Keire;

namespace ScriptingExamples;

// Assign a saved rig to this Animator in Rigging Studio, then copy its stable limb ID below.
// Target and pole are separate scene entities; the rig asset owns bones and bend limits.
[StableComponentId("f85d734e-cada-479f-8f76-7204a4e88ed4")]
public sealed class AssetBoundLimbGoal : Behaviour
{
    [SerializeField, StableFieldId("964d4ab5-5117-44a4-9bda-fa4a33ea1f92")]
    private uint _limbId = 1;
    [SerializeField, StableFieldId("57e5707f-feb6-42e4-859a-0ce8846b26b1")]
    private Entity? _target = null;
    [SerializeField, StableFieldId("6559c0d4-6c6b-48cf-9ce7-72f9d57d2297")]
    private Entity? _pole = null;
    private Animator? _animator;
    private LimbId _activeLimb;

    protected override void OnEnable() => _animator = GetComponent<Animator>();

    protected override void OnAnimatorIk(AnimationIkContext context)
    {
        if (_animator is not { IsValid: true })
            return;
        var selected = new LimbId(_limbId);
        if (selected != _activeLimb)
        {
            if (_activeLimb.IsValid)
                _animator.ClearLimbIK(_activeLimb);
            _activeLimb = selected;
        }
        if (!_activeLimb.IsValid)
            return; // Zero leaves the limb unassigned.
        if (_target is not { IsValid: true } || _pole is not { IsValid: true })
        {
            _animator.ClearLimbIK(_activeLimb);
            return;
        }
        var target = context.IsFixedUpdate ? _target.Transform.Position : _target.Transform.PresentationPosition;
        var pole = context.IsFixedUpdate ? _pole.Transform.Position : _pole.Transform.PresentationPosition;
        var space = context.IsFixedUpdate ? AnimatorIkSpace.World : AnimatorIkSpace.PresentationWorld;
        _animator.SetLimbIK(_activeLimb, target, pole, space: space);
    }

    protected override void OnDisable()
    {
        if (_animator is { IsValid: true } && _activeLimb.IsValid)
            _animator.ClearLimbIK(_activeLimb);
        _activeLimb = default;
        _animator = null;
    }
}
