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

    [HotReloadState] private float _verticalSpeed;
    [HotReloadState] private float _pitch;
    [HotReloadState] private bool _captured = true;
    private CharacterController _motor = null!;
    private InputActionContext? _context;
    private InputAction? _move, _look, _gamepadLook, _jump, _sprint, _escape;
    private IDisposable? _capture;
    private Vector2 _movement;
    private bool _sprinting, _jumpQueued;

    protected override void OnEnable()
    {
        Release();
        _motor = Entity.Parent?.GetComponent<CharacterController>() ??
            throw new InvalidOperationException("The FPS camera must be a child of an entity with a Character Controller.");
        if (_inputActions is not { IsValid: true })
            throw new InvalidOperationException("Assign the supplied FirstPersonInput asset to the FPS controller.");
        try
        {
            _context = _inputActions.CreateContext();
            _move = _context.FindAction("Player/Move");
            _look = _context.FindAction("Player/Look");
            _gamepadLook = _context.FindAction("Player/GamepadLook");
            _jump = _context.FindAction("Player/Jump");
            _sprint = _context.FindAction("Player/Sprint");
            _escape = _context.FindAction("Player/Escape");
            _context.Enable();
            if (_captured)
                _capture = Cursor.RequestCapture();
        }
        catch
        {
            Release();
            throw;
        }
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();
    protected override void OnAfterReload()
    {
        if (IsActiveAndEnabled)
            OnEnable();
    }

    protected override void Update()
    {
        if (!Cursor.VisibilityRequested && _escape?.WasPressedThisFrame == true)
        {
            _captured = !_captured;
            _capture?.Dispose();
            _capture = _captured ? Cursor.RequestCapture() : null;
        }
        bool acceptsInput = _captured && Cursor.Locked && !Cursor.VisibilityRequested;
        _movement = acceptsInput ? _move?.ReadValue<Vector2>() ?? default : default;
        if (_movement.LengthSquared > 1.0f)
            _movement = _movement.Normalized;
        _sprinting = acceptsInput && _sprint?.IsPressed == true;
        if (!acceptsInput)
        {
            _jumpQueued = false;
            return;
        }
        _jumpQueued |= _jump?.WasPressedThisFrame == true;
        // Mouse delta is a per-frame displacement; stick input is a rate in degrees per second.
        Vector2 look = (_look?.ReadValue<Vector2>() ?? default) * MathF.Max(0, _mouseSensitivity);
        look += (_gamepadLook?.ReadValue<Vector2>() ?? default) *
            (MathF.Max(0, _gamepadLookSpeed) * MathF.Max(0, Time.DeltaTime));
        _motor.Entity.Transform.Rotate(Quaternion.Euler(0, look.X));
        _pitch = Math.Clamp(_pitch + look.Y * (_invertY ? 1 : -1), -89.0f, 89.0f);
        Entity.Transform.LocalRotation = Quaternion.Euler(_pitch, 0);
    }

    protected override void FixedUpdate()
    {
        float dt = Time.FixedDeltaTime;
        if (dt <= 0 || !_motor.IsValid)
            return;
        bool grounded = _motor.Grounded;
        // Move queues this tick's request. State describes the previous completed physics step.
        if (_verticalSpeed > 0 && _motor.Velocity.Y <= 0 && !grounded)
            _verticalSpeed = 0;
        if (grounded && _verticalSpeed < 0)
            _verticalSpeed = -2.0f;
        if (_jumpQueued && grounded)
            _verticalSpeed = MathF.Sqrt(2 * MathF.Max(0, _gravity) * MathF.Max(0, _jumpHeight));
        _jumpQueued = false;
        _verticalSpeed = MathF.Max(-60, _verticalSpeed - MathF.Max(0, _gravity) * dt);
        Transform body = _motor.Entity.Transform;
        Vector3 direction = body.Right * _movement.X + body.Forward * _movement.Y;
        float speed = MathF.Max(0, _walkSpeed) * (_sprinting ? MathF.Max(1, _sprintMultiplier) : 1);
        _motor.Move((direction * speed + Vector3.Up * _verticalSpeed) * dt);
    }

    private void Release()
    {
        _context?.Dispose();
        _context = null;
        _capture?.Dispose();
        _capture = null;
        _move = _look = _gamepadLook = _jump = _sprint = _escape = null;
        _movement = default;
        _sprinting = _jumpQueued = false;
    }
}
