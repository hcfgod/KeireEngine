using Keire;

namespace Keire.FirstPerson;

public readonly record struct FirstPersonInputFrame(Vector2 Movement, Vector2 MouseDelta, Vector2 LookStick,
    bool JumpPressed, bool SprintHeld, bool ToggleCursor);

/// <summary>Input sources return per-frame deltas and edge-triggered requests. Injected sources remain caller-owned.</summary>
public interface IFirstPersonInputSource
{
    FirstPersonInputFrame Read();
}

public sealed class FirstPersonActionInput : IFirstPersonInputSource, IDisposable
{
    private InputActionContext? _context;
    private readonly InputAction _move, _look, _stick, _jump, _sprint, _escape;
    public FirstPersonActionInput(InputActionAsset asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var context = asset.CreateContext();
        try
        {
            _move = context.FindAction("Player/Move") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/Move action. Restore FirstPersonInput or add this action in the Input editor.");
            _look = context.FindAction("Player/Look") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/Look action. Restore FirstPersonInput or add this action in the Input editor.");
            _stick = context.FindAction("Player/GamepadLook") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/GamepadLook action. Restore FirstPersonInput or add this action in the Input editor.");
            _jump = context.FindAction("Player/Jump") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/Jump action. Restore FirstPersonInput or add this action in the Input editor.");
            _sprint = context.FindAction("Player/Sprint") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/Sprint action. Restore FirstPersonInput or add this action in the Input editor.");
            _escape = context.FindAction("Player/Escape") ?? throw new InvalidOperationException("FPS input asset is missing the required Player/Escape action. Restore FirstPersonInput or add this action in the Input editor.");
            context.Enable();
            _context = context;
        }
        catch { context.Dispose(); throw; }
    }

    public FirstPersonInputFrame Read() => _context is null ? default : new(
        _move.ReadValue<Vector2>(), _look.ReadValue<Vector2>(), _stick.ReadValue<Vector2>(),
        _jump.WasPressedThisFrame, _sprint.IsPressed, _escape.WasPressedThisFrame);

    public void Dispose() { _context?.Dispose(); _context = null; }
}
