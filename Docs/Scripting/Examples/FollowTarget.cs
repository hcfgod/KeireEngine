using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f011")]
public sealed class FollowTarget : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f111")]
    private Entity? _target = null;

    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f112")]
    private Vector3 _offset = new(0.0f, 2.0f, -5.0f);

    protected override void LateUpdate()
    {
        if (_target is { IsValid: true })
            Transform.Position = _target.Transform.PresentationPosition + _offset;
    }
}
