using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f001")]
[RequireComponent(typeof(CharacterController))]
public sealed class ActionMotor : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f101")]
    private InputActionAsset? _actions = null;

    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f102"), Min(0.0)]
    private float _speed = 4.0f;

    private InputActionContext? _context;
    private InputAction? _move;
    private CharacterController? _motor;

    protected override void OnEnable() => Bind();
    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Bind();
    }

    private void Bind()
    {
        Release();
        _motor = GetComponent<CharacterController>();
        if (_actions is not { IsValid: true })
            return;
        _context = _actions.CreateContext();
        _move = _context.FindAction("Player/Move");
        _move?.Enable();
        if (_move is null)
            Debug.Warn("ActionMotor needs a Player/Move Axis2D action.");
    }

    private void Release()
    {
        _context?.Dispose();
        _context = null;
        _move = null;
        _motor = null;
    }

    protected override void FixedUpdate()
    {
        if (_motor is not { IsValid: true })
            return;
        Vector2 input = _move?.ReadValue<Vector2>() ?? Vector2.Zero;
        Vector3 direction = Transform.Right * input.X + Transform.Forward * input.Y;
        if (direction.LengthSquared > 1.0f)
            direction = direction.Normalized;
        if (!_motor.Move(direction * (_speed * Time.FixedDeltaTime)))
            Debug.Warn("Character movement was rejected.");
    }
}
