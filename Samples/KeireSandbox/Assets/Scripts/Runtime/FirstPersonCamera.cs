using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000030")]
[ExecutionOrder(10)]
public sealed class FirstPersonCamera : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000031")]
    private float _movementSpeed = 6.5f;

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

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000038")]
    private float _sprintMultiplier = 1.65f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000039")]
    private float _airControl = 0.28f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003a")]
    private float _gravity = 24.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003b")]
    private float _jumpHeight = 1.15f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003c")]
    private float _coyoteTime = 0.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003d")]
    private float _jumpBufferTime = 0.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003e")]
    private float _groundStickSpeed = 2.0f;

    [HotReloadState]
    private float _yaw;

    [HotReloadState]
    private float _pitch;

    [HotReloadState]
    private float _targetYaw;

    [HotReloadState]
    private float _targetPitch;

    [HotReloadState]
    private Vector3 _horizontalVelocity;

    [HotReloadState]
    private float _verticalVelocity;

    [HotReloadState]
    private float _coyoteRemaining;

    [HotReloadState]
    private float _jumpBufferRemaining;

    [HotReloadState]
    private bool _escapeWasDown;

    [HotReloadState]
    private bool _captureEnabled = true;

    private IDisposable? _cursorCapture;
    private bool _uiVisible;
    private Entity _motorEntity;

    protected override void Awake()
    {
        ResolveMotor();
        _targetYaw = _yaw;
        _targetPitch = _pitch;
        Debug.Log($"{nameof(FirstPersonCamera)} attached to Character Controller '{_motorEntity.Name}'.");
    }

    protected override void OnEnable()
    {
        ResolveMotor();
        SubscribeUiVisibility();
        SetCaptureEnabled(_captureEnabled);
    }

    protected override void OnDisable()
    {
        UnsubscribeUiVisibility();
        ReleaseCapture();
        Cursor.Unlock();
        Cursor.Show();
    }

    protected override void OnBeforeReload()
    {
        UnsubscribeUiVisibility();
        ReleaseCapture();
    }

    protected override void OnAfterReload()
    {
        ResolveMotor();
        SubscribeUiVisibility();
        SetCaptureEnabled(_captureEnabled);
    }

    protected override void Update()
    {
        bool escapeDown = Input.Button("Escape");
        if (!_uiVisible && !Cursor.VisibilityRequested && escapeDown && !_escapeWasDown)
            SetCaptureEnabled(!_captureEnabled);
        _escapeWasDown = escapeDown;

        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f || !_motorEntity.IsValid)
            return;

        bool acceptsInput = _captureEnabled && !_uiVisible && !Cursor.VisibilityRequested;
        if (acceptsInput)
            UpdateLook(deltaTime);

        var motor = _motorEntity.CharacterController;
        if (!motor.IsValid)
            return;

        Vector2 move = acceptsInput ? Input.Axis2D("Move") : default;
        if (move.LengthSquared > 1.0f)
            move = move.Normalized;

        float yawRadians = DegreesToRadians(_yaw);
        Vector3 right = new(MathF.Cos(yawRadians), 0.0f, -MathF.Sin(yawRadians));
        Vector3 forward = new(MathF.Sin(yawRadians), 0.0f, MathF.Cos(yawRadians));
        float speed = _movementSpeed * (acceptsInput && Input.Held("Sprint") ? _sprintMultiplier : 1.0f);
        Vector3 desiredVelocity = (right * move.X + forward * move.Y) * speed;
        float control = motor.Grounded ? 1.0f : Math.Clamp(_airControl, 0.0f, 1.0f);
        float movementBlend = 1.0f - MathF.Exp(-MathF.Max(0.0f, _movementSharpness) * control * deltaTime);
        _horizontalVelocity += (desiredVelocity - _horizontalVelocity) * movementBlend;

        if (motor.Grounded)
        {
            _coyoteRemaining = MathF.Max(0.0f, _coyoteTime);
            if (_verticalVelocity < 0.0f)
                _verticalVelocity = -MathF.Max(0.0f, _groundStickSpeed);
        }
        else
        {
            _coyoteRemaining = MathF.Max(0.0f, _coyoteRemaining - deltaTime);
        }

        if (acceptsInput && Input.Pressed("Jump"))
            _jumpBufferRemaining = MathF.Max(0.0f, _jumpBufferTime);
        else
            _jumpBufferRemaining = MathF.Max(0.0f, _jumpBufferRemaining - deltaTime);

        if (_jumpBufferRemaining > 0.0f && _coyoteRemaining > 0.0f)
        {
            _verticalVelocity = MathF.Sqrt(2.0f * MathF.Max(0.0f, _gravity) * MathF.Max(0.0f, _jumpHeight));
            _jumpBufferRemaining = 0.0f;
            _coyoteRemaining = 0.0f;
        }
        _verticalVelocity -= MathF.Max(0.0f, _gravity) * deltaTime;

        Vector3 frameVelocity = _horizontalVelocity + Vector3.Up * _verticalVelocity;
        motor.Move(frameVelocity * deltaTime);
    }

    private void UpdateLook(float deltaTime)
    {
        Vector2 look = Input.Axis2D("Look");
        float horizontalLook = look.X * (_invertHorizontal ? -1.0f : 1.0f);
        float verticalLook = look.Y * (_invertVertical ? -1.0f : 1.0f);
        _targetYaw += horizontalLook * _lookSensitivity;
        _targetPitch =
            Math.Clamp(_targetPitch + (verticalLook * _lookSensitivity), -_maximumPitch, _maximumPitch);
        float lookBlend = 1.0f - MathF.Exp(-MathF.Max(0.0f, _lookSharpness) * deltaTime);
        _yaw += (_targetYaw - _yaw) * lookBlend;
        _pitch += (_targetPitch - _pitch) * lookBlend;

        TransformHandle motorTransform = _motorEntity.Transform;
        motorTransform.LocalRotation = Quaternion.Euler(0.0f, _yaw);
        Vector3 recoil = WeaponController.CameraRecoil;
        TransformHandle cameraTransform = Entity.Transform;
        cameraTransform.LocalRotation = Quaternion.Euler(_pitch + recoil.X, recoil.Y);
    }

    private void ResolveMotor()
    {
        _motorEntity = Entity.Parent;
        if (!_motorEntity.IsValid || !_motorEntity.CharacterController.IsValid)
            throw new InvalidOperationException(
                $"{nameof(FirstPersonCamera)} requires its camera Entity to be parented to an enabled Character Controller.");
    }

    private void SubscribeUiVisibility()
    {
        UIController.VisibilityChanged -= HandleUiVisibilityChanged;
        UIController.VisibilityChanged += HandleUiVisibilityChanged;
        HandleUiVisibilityChanged(UIController.IsAnyUiVisible);
    }

    private void UnsubscribeUiVisibility()
    {
        UIController.VisibilityChanged -= HandleUiVisibilityChanged;
        _uiVisible = false;
    }

    private void HandleUiVisibilityChanged(bool visible)
    {
        _uiVisible = visible;
        if (visible)
            _horizontalVelocity = default;
    }

    private void SetCaptureEnabled(bool enabled)
    {
        _captureEnabled = enabled;
        if (enabled)
            _cursorCapture ??= Cursor.RequestCapture();
        else
            ReleaseCapture();
    }

    private void ReleaseCapture()
    {
        _cursorCapture?.Dispose();
        _cursorCapture = null;
    }

    private static float DegreesToRadians(float degrees) => degrees * (MathF.PI / 180.0f);
}
