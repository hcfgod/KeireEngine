using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000030")]
[ExecutionOrder(10)]
public sealed class FirstPersonCamera : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000031")]
    private float _movementSpeed = 12.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000032")]
    private float _lookSensitivity = 2.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000033")]
    private float _maximumPitch = 89.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000034")]
    private float _movementSharpness = 14.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000035")]
    private bool _invertHorizontal;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000036")]
    private bool _invertVertical;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000037")]
    private float _lookSharpness = 30.0f;

    [HotReloadState]
    private float _yaw;

    [HotReloadState]
    private float _pitch;

    [HotReloadState]
    private float _targetYaw;

    [HotReloadState]
    private float _targetPitch;

    [HotReloadState]
    private Vector3 _velocity;

    [HotReloadState]
    private bool _escapeWasDown;

    protected override void Awake()
    {
        _targetYaw = _yaw;
        _targetPitch = _pitch;
        Cursor.Lock();
        Cursor.Hide();
        Debug.Log($"{nameof(FirstPersonCamera)} attached.");
    }

    protected override void OnDisable()
    {
        Cursor.Unlock();
        Cursor.Show();
    }

    protected override void Update()
    {
        bool escapeDown = Input.Button("Escape");
        if (escapeDown && !_escapeWasDown)
        {
            if (Cursor.Locked)
            {
                Cursor.Unlock();
                Cursor.Show();
            }
            else
            {
                Cursor.Lock();
                Cursor.Hide();
            }
        }
        _escapeWasDown = escapeDown;

        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f)
            return;
        if (!Cursor.Locked)
        {
            _velocity = default;
            return;
        }

        Vector2 look = Input.Axis2D("Look");
        float horizontalLook = look.X * (_invertHorizontal ? -1.0f : 1.0f);
        float verticalLook = look.Y * (_invertVertical ? -1.0f : 1.0f);
        _targetYaw += horizontalLook * _lookSensitivity;
        _targetPitch =
            Math.Clamp(_targetPitch + (verticalLook * _lookSensitivity), -_maximumPitch, _maximumPitch);
        float lookBlend = 1.0f - MathF.Exp(-MathF.Max(0.0f, _lookSharpness) * deltaTime);
        _yaw += (_targetYaw - _yaw) * lookBlend;
        _pitch += (_targetPitch - _pitch) * lookBlend;

        var transform = Entity.Transform;
        Vector3 recoil = WeaponController.CameraRecoil;
        transform.LocalRotation = CreateRotation(_pitch + recoil.X, _yaw + recoil.Y);

        Vector2 move = Input.Axis2D("Move");
        float magnitude = MathF.Sqrt((move.X * move.X) + (move.Y * move.Y));
        if (magnitude > 1.0f)
            move = new Vector2(move.X / magnitude, move.Y / magnitude);

        float yawRadians = DegreesToRadians(_yaw);
        Vector3 right = new(MathF.Cos(yawRadians), 0.0f, -MathF.Sin(yawRadians));
        Vector3 forward = new(MathF.Sin(yawRadians), 0.0f, MathF.Cos(yawRadians));
        Vector3 desiredVelocity = (right * move.X + forward * move.Y) * _movementSpeed;
        float blend = 1.0f - MathF.Exp(-MathF.Max(0.0f, _movementSharpness) * deltaTime);
        _velocity += (desiredVelocity - _velocity) * blend;
        transform.LocalPosition = transform.LocalPosition + (_velocity * deltaTime);
    }

    private static Quaternion CreateRotation(float pitch, float yaw)
    {
        float halfPitch = DegreesToRadians(pitch) * 0.5f;
        float halfYaw = DegreesToRadians(yaw) * 0.5f;
        float sinPitch = MathF.Sin(halfPitch);
        float cosPitch = MathF.Cos(halfPitch);
        float sinYaw = MathF.Sin(halfYaw);
        float cosYaw = MathF.Cos(halfYaw);
        return new Quaternion(
            sinPitch * cosYaw,
            cosPitch * sinYaw,
            -sinPitch * sinYaw,
            cosPitch * cosYaw);
    }

    private static float DegreesToRadians(float degrees) => degrees * (MathF.PI / 180.0f);
}
