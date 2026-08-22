using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000030")]
[ExecutionOrder(-200)]
public sealed class FirstPersonCamera : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000031")]
    [Range(0.5, 12.0), Tooltip("Maximum character movement speed in metres per second.")]
    private float _movementSpeed = 6.5f;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000032")]
    private float _lookSensitivity = 2.12f;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000033")]
    private float _maximumPitch = 89.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000034")]
    [Range(0.0, 40.0), Tooltip("How quickly the motor accelerates toward requested movement.")]
    private float _movementSharpness = 12.0f;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000035")]
    private bool _invertHorizontal;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000036")]
    private bool _invertVertical;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000037")]
    private float _lookSharpness = 30.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000038")]
    [Range(1.0, 3.0)]
    private float _sprintMultiplier = 1.65f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000039")]
    [Range(0.0, 1.0)]
    private float _airControl = 0.28f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003a")]
    [Range(0.0, 60.0)]
    private float _gravity = 24.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003b")]
    [Range(0.0, 4.0)]
    private float _jumpHeight = 1.15f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003c")]
    [Range(0.0, 0.5)]
    private float _coyoteTime = 0.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003d")]
    [Range(0.0, 0.5)]
    private float _jumpBufferTime = 0.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003e")]
    [Range(0.0, 10.0)]
    private float _groundStickSpeed = 2.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-00000000003f")]
    [Tooltip("Fixed world-space camera offset from the Character Controller.")]
    private Vector3 _cameraOffset = new(0.0f, 1.5f, -3.25f);

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000130")]
    [Range(-89.0, 89.0)]
    private float _cameraPitch = -20.0f;

    [SerializeField, HideInInspector, StableFieldId("73616e64-626f-4078-8000-000000000131")]
    [Range(-180.0, 180.0)]
    private float _cameraYaw = 180.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000132")]
    [Range(0.0, 40.0), Tooltip("How quickly the fixed camera catches up with the player.")]
    private float _cameraFollowSharpness = 18.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000133")]
    [Range(0.0, 40.0), Tooltip("How quickly the character faces its movement direction.")]
    private float _turnSharpness = 14.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000134")]
    [Range(0.0, 40.0), Tooltip("How quickly movement comes to rest after input is released.")]
    private float _decelerationSharpness = 18.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000135")]
    [Range(0.0, 0.5), Tooltip("Grounded time required before foot IK blends back in after landing.")]
    private float _footIkLandingDelay = 0.12f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000136")]
    [Range(0.0, 40.0), Tooltip("How quickly foot IK blends back in after landing.")]
    private float _footIkSharpness = 8.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000137")]
    [Tooltip("World-space point on the Character Controller that the fixed camera watches.")]
    private Vector3 _cameraTargetOffset = new(0.0f, 0.6f, 0.0f);

    [HotReloadState]
    private Vector3 _horizontalVelocity;

    [HotReloadState]
    private float _verticalVelocity;

    [HotReloadState]
    private float _coyoteRemaining;

    [HotReloadState]
    private float _jumpBufferRemaining;

    [HotReloadState]
    private float _motorYaw;

    [HotReloadState]
    private Vector3 _cameraPosition;

    [HotReloadState]
    private bool _cameraInitialized;

    [HotReloadState]
    private float _footGroundingWeight = 1.0f;

    [HotReloadState]
    private float _groundedDuration;

    [HotReloadState]
    private bool _captureEnabled = true;

    private IDisposable? _cursorCapture;
    private bool _uiVisible;
    private bool _acceptsInput;
    private bool _sprinting;
    private Vector2 _moveInput;

    [HotReloadState]
    private Entity _motorEntity;

    [HotReloadState]
    private Animator? _animator;

    protected override void Awake()
    {
        PreserveLegacyOrbitSettings();
        ResolveCharacter();
        DetachCameraFromMotor();
        Vector3 forward = _motorEntity.Transform.Forward;
        _motorYaw = RadiansToDegrees(MathF.Atan2(forward.X, forward.Z));
        SnapCamera();
        Debug.Log($"Static follow controller attached to Character Controller '{_motorEntity.Name}'.");
    }

    protected override void OnEnable()
    {
        ResolveCharacter();
        DetachCameraFromMotor();
        SubscribeUiVisibility();
        SetCaptureEnabled(_captureEnabled);
        if (!_cameraInitialized)
            SnapCamera();
    }

    protected override void OnDisable()
    {
        RestoreFootGrounding();
        UnsubscribeUiVisibility();
        ReleaseCapture();
        Cursor.Unlock();
        Cursor.Show();
    }

    protected override void OnBeforeReload()
    {
        RestoreFootGrounding();
        UnsubscribeUiVisibility();
        ReleaseCapture();
    }

    protected override void OnAfterReload()
    {
        ResolveCharacter();
        DetachCameraFromMotor();
        SubscribeUiVisibility();
        SetCaptureEnabled(_captureEnabled);
        SnapCamera();
    }

    protected override void Update()
    {
        if (!_uiVisible && !Cursor.VisibilityRequested && Input.Pressed("Escape"))
            SetCaptureEnabled(!_captureEnabled);

        _acceptsInput = _captureEnabled && !_uiVisible && !Cursor.VisibilityRequested;
        _moveInput = _acceptsInput ? Input.Axis2D("Move") : default;
        if (_moveInput.LengthSquared > 1.0f)
            _moveInput = _moveInput.Normalized;
        _sprinting = _acceptsInput && Input.Held("Sprint");
        if (_acceptsInput && Input.Pressed("Jump"))
        {
            _jumpBufferRemaining = MathF.Max(0.0f, _jumpBufferTime);
            BeginAirbornePresentation();
        }
    }

    protected override void FixedUpdate()
    {
        float deltaTime = MathF.Min(MathF.Max(0.0f, Time.FixedDeltaTime), 0.05f);
        if (deltaTime <= 0.0f || !_motorEntity.IsValid)
            return;

        CharacterController? motor = _motorEntity.GetComponent<CharacterController>();
        if (motor is null || !motor.IsValid)
            return;

        Vector2 move = _acceptsInput ? _moveInput : default;
        float speed = _movementSpeed * (_sprinting ? _sprintMultiplier : 1.0f);
        Vector3 cameraForward = CameraForward();
        Vector3 cameraRight = Vector3.Cross(Vector3.Up, cameraForward).Normalized;
        Vector3 desiredDirection = (cameraRight * move.X) + (cameraForward * move.Y);
        Vector3 desiredVelocity = desiredDirection * speed;
        bool grounded = motor.Grounded;
        float control = grounded ? 1.0f : Math.Clamp(_airControl, 0.0f, 1.0f);
        float sharpness = desiredDirection.LengthSquared > 0.0001f ? _movementSharpness : _decelerationSharpness;
        float movementBlend = ExponentialBlend(sharpness * control, deltaTime);
        _horizontalVelocity += (desiredVelocity - _horizontalVelocity) * movementBlend;

        if (desiredDirection.LengthSquared > 0.0001f)
        {
            float targetYaw = RadiansToDegrees(MathF.Atan2(desiredDirection.X, desiredDirection.Z));
            _motorYaw = SmoothAngle(_motorYaw, targetYaw, ExponentialBlend(_turnSharpness, deltaTime));
            Transform motorTransform = _motorEntity.Transform;
            motorTransform.Rotation = Quaternion.Euler(0.0f, _motorYaw);
        }

        if (grounded)
        {
            _coyoteRemaining = MathF.Max(0.0f, _coyoteTime);
            if (_verticalVelocity < 0.0f)
                _verticalVelocity = -MathF.Max(0.0f, _groundStickSpeed);
        }
        else
        {
            _coyoteRemaining = MathF.Max(0.0f, _coyoteRemaining - deltaTime);
        }

        bool jumped = _jumpBufferRemaining > 0.0f && _coyoteRemaining > 0.0f;
        if (jumped)
        {
            _verticalVelocity = MathF.Sqrt(2.0f * MathF.Max(0.0f, _gravity) * MathF.Max(0.0f, _jumpHeight));
            _jumpBufferRemaining = 0.0f;
            _coyoteRemaining = 0.0f;
        }
        else
        {
            _jumpBufferRemaining = MathF.Max(0.0f, _jumpBufferRemaining - deltaTime);
        }
        _verticalVelocity -= MathF.Max(0.0f, _gravity) * deltaTime;

        Vector3 frameVelocity = _horizontalVelocity + (Vector3.Up * _verticalVelocity);
        motor.Move(frameVelocity * deltaTime);
        bool animationGrounded = grounded && !jumped && _verticalVelocity <= 0.0f;
        UpdateAnimationState(animationGrounded, jumped);
        UpdateFootGrounding(animationGrounded, jumped, deltaTime);
    }

    protected override void LateUpdate()
    {
        if (!_motorEntity.IsValid)
            return;

        Vector3 desiredPosition = _motorEntity.Transform.Position + _cameraOffset;
        float deltaTime = MathF.Min(MathF.Max(0.0f, Time.DeltaTime), 0.1f);
        if (!_cameraInitialized)
        {
            _cameraPosition = desiredPosition;
            _cameraInitialized = true;
        }
        else
        {
            _cameraPosition = Vector3.Lerp(
                _cameraPosition, desiredPosition, ExponentialBlend(_cameraFollowSharpness, deltaTime));
        }

        Transform cameraTransform = Entity.Transform;
        cameraTransform.Position = _cameraPosition;
        cameraTransform.Rotation = CameraRotation();
    }

    private void ResolveCharacter()
    {
        if (_motorEntity is not null && _motorEntity.IsValid &&
            _motorEntity.GetComponent<CharacterController>() is not null)
        {
            _animator = FindAnimator(_motorEntity);
            return;
        }
        _motorEntity = Entity.Parent!;
        if (_motorEntity is null || !_motorEntity.IsValid ||
            _motorEntity.GetComponent<CharacterController>() is null)
        {
            throw new InvalidOperationException(
                $"{nameof(FirstPersonCamera)} requires its camera Entity to be parented to an enabled Character Controller.");
        }
        _animator = FindAnimator(_motorEntity);
    }

    private void DetachCameraFromMotor()
    {
        if (Entity.Parent == _motorEntity)
            Entity.SetParent(default, true);
    }

    private static Animator? FindAnimator(Entity root)
    {
        if (root.TryGetComponent(out Animator? animator))
            return animator;
        foreach (Entity child in root.Children)
        {
            Animator? nested = FindAnimator(child);
            if (nested is not null)
                return nested;
        }
        return null;
    }

    private void SnapCamera()
    {
        _cameraPosition = _motorEntity.Transform.Position + _cameraOffset;
        _cameraInitialized = true;
        Transform cameraTransform = Entity.Transform;
        cameraTransform.Position = _cameraPosition;
        cameraTransform.Rotation = CameraRotation();
    }

    private Vector3 CameraForward()
    {
        Vector3 direction = _cameraTargetOffset - _cameraOffset;
        direction = new Vector3(direction.X, 0.0f, direction.Z);
        return direction.LengthSquared > 0.0001f ? direction.Normalized : Vector3.Forward;
    }

    private Quaternion CameraRotation()
    {
        Vector3 direction = (_cameraTargetOffset - _cameraOffset).Normalized;
        float horizontalLength = MathF.Sqrt((direction.X * direction.X) + (direction.Z * direction.Z));
        float pitch = RadiansToDegrees(MathF.Atan2(-direction.Y, horizontalLength));
        float yaw = RadiansToDegrees(MathF.Atan2(direction.X, direction.Z));
        return Quaternion.Euler(pitch, yaw);
    }

    private void BeginAirbornePresentation()
    {
        if (_animator is null || !_animator.IsValid)
            return;
        _groundedDuration = 0.0f;
        _footGroundingWeight = 0.0f;
        _animator.SetFootGroundingWeight(0.0f);
        _animator.SetBool("Grounded", false);
        _animator.SetBool("Falling", false);
    }

    private void UpdateAnimationState(bool grounded, bool jumped)
    {
        if (_animator is null || !_animator.IsValid)
            return;
        _animator.SetFloat("VerticalSpeed", _verticalVelocity);
        _animator.SetBool("Grounded", grounded);
        _animator.SetBool("Falling", !grounded && !jumped && _verticalVelocity < -0.5f);
    }

    private void UpdateFootGrounding(bool settledGrounded, bool jumped, float deltaTime)
    {
        if (_animator is null || !_animator.IsValid)
            return;

        if (!settledGrounded || jumped)
        {
            _groundedDuration = 0.0f;
            _footGroundingWeight = 0.0f;
        }
        else
        {
            _groundedDuration += deltaTime;
            float target = _groundedDuration >= MathF.Max(0.0f, _footIkLandingDelay) ? 1.0f : 0.0f;
            _footGroundingWeight +=
                (target - _footGroundingWeight) * ExponentialBlend(_footIkSharpness, deltaTime);
        }
        _animator.SetFootGroundingWeight(Math.Clamp(_footGroundingWeight, 0.0f, 1.0f));
    }

    private void RestoreFootGrounding()
    {
        if (_animator is { IsValid: true })
            _animator.SetFootGroundingWeight(1.0f);
        _footGroundingWeight = 1.0f;
        _groundedDuration = 0.0f;
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
        if (!visible)
            return;
        _moveInput = default;
        _sprinting = false;
        _jumpBufferRemaining = 0.0f;
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

    private void PreserveLegacyOrbitSettings()
    {
        _ = _lookSensitivity;
        _ = _maximumPitch;
        _ = _invertHorizontal;
        _ = _invertVertical;
        _ = _lookSharpness;
        _ = _cameraPitch;
        _ = _cameraYaw;
    }

    private static float ExponentialBlend(float sharpness, float deltaTime) =>
        1.0f - MathF.Exp(-MathF.Max(0.0f, sharpness) * deltaTime);

    private static float SmoothAngle(float current, float target, float blend)
    {
        float delta = ((target - current + 540.0f) % 360.0f) - 180.0f;
        return current + (delta * Math.Clamp(blend, 0.0f, 1.0f));
    }

    private static float RadiansToDegrees(float radians) => radians * (180.0f / MathF.PI);
}
