using Keire;

namespace KeireSandbox;

/// <summary>Animates Material Lab sculptures without coupling the scene to a particular material or shader.</summary>
[StableComponentId("73616e64-626f-4078-8000-000000000060")]
[ExecutionOrder(40)]
public sealed class ShowcaseOrbit : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000061")]
    [Range(-180.0, 180.0), Tooltip("Yaw rotation speed in degrees per second.")]
    private float _rotationSpeed = 18.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000062")]
    [Range(0.0, 0.5), Tooltip("Vertical presentation bob in metres.")]
    private float _bobHeight = 0.08f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000063")]
    [Range(0.0, 8.0), Tooltip("Presentation bob frequency.")]
    private float _bobFrequency = 1.25f;

    [HotReloadState]
    private float _elapsed;

    private Vector3 _origin;

    protected override void Awake() => _origin = Entity.Transform.LocalPosition;

    protected override void Update()
    {
        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f)
            return;

        _elapsed += deltaTime;
        Transform transform = Entity.Transform;
        transform.LocalRotation = Quaternion.Euler(0.0f, _elapsed * _rotationSpeed);
        transform.LocalPosition = _origin + (Vector3.Up * (MathF.Sin(_elapsed * _bobFrequency) * _bobHeight));
    }
}
