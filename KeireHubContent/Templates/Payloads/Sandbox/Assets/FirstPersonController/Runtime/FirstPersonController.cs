using Keire;

namespace Keire.FirstPerson;

[StableComponentId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8100")]
[ExecutionOrder(-200)]
public sealed class FirstPersonController : Behaviour
{
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8101")]
    private InputActionAsset? _inputActions = null;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8102"), Range(0.1, 20)]
    private float _walkSpeed = 5.0f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8103"), Range(1, 3)]
    private float _sprintMultiplier = 1.6f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8104"), Range(0, 5)]
    private float _jumpHeight = 1.2f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8105"), Range(0.1, 60)]
    private float _gravity = 24.0f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8106"), Range(0.01, 2)]
    private float _mouseSensitivity = 0.12f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8107"), Range(1, 360)]
    private float _gamepadLookSpeed = 150.0f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8108")]
    private bool _invertY = false;

    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8109"), Range(0, 200)]
    private float _acceleration = 35f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810a"), Range(0, 200)]
    private float _deceleration = 45f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810b"), Range(0, 1)]
    private float _airControl = 0.35f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810c"), Range(0, 0.5)]
    private float _coyoteTime = 0.1f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810d"), Range(0, 0.5)]
    private float _jumpBufferTime = 0.12f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810e"), Range(1, 200)]
    private float _terminalSpeed = 60f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a810f"), Range(-89, 0)]
    private float _minimumPitch = -89f;
    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8110"), Range(0, 89)]
    private float _maximumPitch = 89f;

    [SerializeField, StableFieldId("d7e0f0a1-bfe2-46d2-bc47-4083f77a8111")]
    private int _configurationVersion = 1;
    [HotReloadState] private FirstPersonMotorState _motionState;
    [HotReloadState] private float _pitch;
    [HotReloadState] private bool _captured = true;
    private readonly FirstPersonMotor _motion = new();
    private CharacterController _motor = null!;
    private FirstPersonActionInput? _defaultInput;
    private IDisposable? _capture;
    private Vector2 _movement;
    private bool _sprintRequested;
    private bool _lastGrounded, _lastSprinting;

    /// <summary>Optional caller-owned source for replay, AI, networking, or another input system.</summary>
    public IFirstPersonInputSource? InputSource { get; set; }
    public bool ControlsEnabled { get; set; } = true;
    public bool RequireCursorCapture { get; set; } = true;
    public bool CursorCaptured => _captured;
    public FirstPersonMotion Motion { get; private set; }
    public Vector3 Velocity => _motor is { IsValid: true } ? _motor.Velocity : default;

    public event Action? Jumped;
    public event Action<float>? Landed;
    public event Action<bool>? GroundedChanged;
    public event Action<bool>? SprintChanged;
    public event Action<bool>? CursorCaptureChanged;
    public event Action<FirstPersonMotion>? MotionUpdated;

    public FirstPersonMovementSettings MovementSettings
    {
        get => new(_walkSpeed, _sprintMultiplier, _jumpHeight, _gravity, _acceleration, _deceleration,
            _airControl, _coyoteTime, _jumpBufferTime, _terminalSpeed);
        set
        {
            value.Validate();
            (_walkSpeed, _sprintMultiplier, _jumpHeight, _gravity, _acceleration, _deceleration,
                _airControl, _coyoteTime, _jumpBufferTime, _terminalSpeed) =
                (value.WalkSpeed, value.SprintMultiplier, value.JumpHeight, value.Gravity, value.Acceleration,
                 value.Deceleration, value.AirControl, value.CoyoteTime, value.JumpBufferTime, value.TerminalSpeed);
        }
    }

    public FirstPersonLookSettings LookSettings
    {
        get => new(_mouseSensitivity, _gamepadLookSpeed, _invertY, _minimumPitch, _maximumPitch);
        set
        {
            value.Validate();
            (_mouseSensitivity, _gamepadLookSpeed, _invertY, _minimumPitch, _maximumPitch) =
                (value.MouseSensitivity, value.StickDegreesPerSecond, value.InvertY, value.MinimumPitch, value.MaximumPitch);
        }
    }

    protected override void OnEnable()
    {
        Release();
        if (_configurationVersion == 0)
        {
            // Older scenes have no values for these newly introduced fields; keep their existing speed/look tuning.
            var defaults = FirstPersonMovementSettings.Default;
            (_acceleration, _deceleration, _airControl, _coyoteTime, _jumpBufferTime, _terminalSpeed) =
                (defaults.Acceleration, defaults.Deceleration, defaults.AirControl, defaults.CoyoteTime,
                 defaults.JumpBufferTime, defaults.TerminalSpeed);
            (_minimumPitch, _maximumPitch) = (-89, 89);
            _configurationVersion = 1;
        }
        MovementSettings.Validate();
        LookSettings.Validate();
        _motor = Entity.Parent?.GetComponent<CharacterController>() ??
            throw new InvalidOperationException("The FPS camera must be a child of an entity with a Character Controller.");
        try
        {
            if (InputSource is null && _inputActions is { IsValid: true })
                _defaultInput = new FirstPersonActionInput(_inputActions);
            else if (InputSource is null)
                throw new InvalidOperationException("Assign FirstPersonInput or provide an IFirstPersonInputSource before enabling this controller.");
            _motion.RestoreState(_motionState);
            _lastGrounded = _motionState.WasGrounded;
            _lastSprinting = false;
            if (_captured && RequireCursorCapture)
                _capture = Cursor.RequestCapture();
        }
        catch { Release(); throw; }
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() { Release(); _motionState = _motion.CaptureState(); }
    protected override void OnAfterReload()
    {
        if (IsActiveAndEnabled)
            OnEnable();
    }

    public void SetCursorCaptured(bool captured)
    {
        if (_captured == captured)
            return;
        _capture?.Dispose();
        _capture = null;
        _captured = captured;
        if (captured && RequireCursorCapture && IsActiveAndEnabled)
            _capture = Cursor.RequestCapture();
        CursorCaptureChanged?.Invoke(captured);
    }

    /// <summary>Call after teleporting or respawning to clear momentum and queued jump input.</summary>
    public void ResetMotion()
    {
        _motion.Reset();
        _motionState = default;
        _movement = default;
        _sprintRequested = _lastSprinting = _lastGrounded = false;
        Motion = default;
    }

    protected override void Update()
    {
        if ((!RequireCursorCapture || !_captured) && _capture is not null)
        {
            _capture.Dispose();
            _capture = null;
        }
        else if (RequireCursorCapture && _captured && _capture is null)
            _capture = Cursor.RequestCapture();
        FirstPersonInputFrame input = (InputSource ?? _defaultInput)?.Read() ?? default;
        if (ControlsEnabled && !Cursor.VisibilityRequested && input.ToggleCursor)
            SetCursorCaptured(!_captured);
        bool acceptsInput = ControlsEnabled && (!RequireCursorCapture ||
            (_captured && Cursor.Locked && !Cursor.VisibilityRequested));
        _movement = acceptsInput ? input.Movement : default;
        _sprintRequested = acceptsInput && input.SprintHeld;
        if (!acceptsInput)
        {
            _motion.StopHorizontalMovement();
            return;
        }
        if (input.JumpPressed)
            _motion.QueueJump(_jumpBufferTime);
        var look = FirstPersonLook.Step(_pitch, input.MouseDelta, input.LookStick, MathF.Max(0, Time.DeltaTime), LookSettings);
        ApplyLook(look);
    }

    private void ApplyLook(FirstPersonLookStep look)
    {
        // Body yaw defines walking direction. Pitch belongs only to the child camera;
        // both are applied every render frame while the engine interpolates collision-resolved position.
        _motor.Entity.Transform.Rotate(Quaternion.Euler(0, look.YawDelta));
        _pitch = look.Pitch;
        Entity.Transform.LocalRotation = Quaternion.Euler(_pitch, 0);
    }

    protected override void FixedUpdate()
    {
        float dt = Time.FixedDeltaTime;
        if (dt <= 0 || !_motor.IsValid)
            return;
        if (!ControlsEnabled || (RequireCursorCapture && (!_captured || !Cursor.Locked || Cursor.VisibilityRequested)))
        {
            _movement = default;
            _sprintRequested = false;
            _motion.StopHorizontalMovement();
        }
        Transform body = _motor.Entity.Transform;
        Vector3 direction = body.Right * _movement.X + body.Forward * _movement.Y;
        Motion = _motion.Step(dt, direction, _sprintRequested, _motor.Grounded, _motor.Velocity, MovementSettings);
        _motionState = _motion.CaptureState();
        _motor.Move(Motion.Velocity * dt);
        if (Motion.Landed) Landed?.Invoke(Motion.LandingSpeed);
        if (Motion.Jumped) Jumped?.Invoke();
        if (Motion.Grounded != _lastGrounded) GroundedChanged?.Invoke(Motion.Grounded);
        if (Motion.Sprinting != _lastSprinting) SprintChanged?.Invoke(Motion.Sprinting);
        _lastGrounded = Motion.Grounded;
        _lastSprinting = Motion.Sprinting;
        MotionUpdated?.Invoke(Motion);
    }

    private void Release()
    {
        _defaultInput?.Dispose();
        _defaultInput = null;
        _capture?.Dispose();
        _capture = null;
        _movement = default;
        _sprintRequested = false;
        _motion.ClearJumpRequest();
    }
}
