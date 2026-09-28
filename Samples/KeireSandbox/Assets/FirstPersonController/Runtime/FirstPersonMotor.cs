using Keire;

namespace Keire.FirstPerson;

public readonly record struct FirstPersonMovementSettings(
    float WalkSpeed, float SprintMultiplier, float JumpHeight, float Gravity,
    float Acceleration, float Deceleration, float AirControl, float CoyoteTime,
    float JumpBufferTime, float TerminalSpeed)
{
    public static FirstPersonMovementSettings Default => new(5, 1.6f, 1.2f, 24, 35, 45, 0.35f, 0.1f, 0.12f, 60);

    public void Validate()
    {
        if (!float.IsFinite(WalkSpeed) || WalkSpeed < 0 || !float.IsFinite(SprintMultiplier) || SprintMultiplier < 1 ||
            !float.IsFinite(JumpHeight) || JumpHeight < 0 || !float.IsFinite(Gravity) || Gravity <= 0 ||
            !float.IsFinite(Acceleration) || Acceleration < 0 || !float.IsFinite(Deceleration) || Deceleration < 0 ||
            !float.IsFinite(AirControl) || AirControl < 0 || AirControl > 1 ||
            !float.IsFinite(CoyoteTime) || CoyoteTime < 0 || !float.IsFinite(JumpBufferTime) || JumpBufferTime < 0 ||
            !float.IsFinite(TerminalSpeed) || TerminalSpeed <= 0)
            throw new ArgumentOutOfRangeException(nameof(FirstPersonMovementSettings), "FPS movement settings must be finite and within their supported ranges.");
    }
}

[Serializable]
public struct FirstPersonMotorState
{
    public Vector3 HorizontalVelocity;
    public float VerticalSpeed;
    public float CoyoteRemaining;
    public float JumpBufferRemaining;
    public bool JumpPending;
    public bool JumpInProgress;
    public bool WasGrounded;
    public bool Initialized;
}

public readonly record struct FirstPersonMotion(Vector3 Velocity, bool Grounded, bool Sprinting,
    bool Jumped, bool Landed, float LandingSpeed);

/// <summary>Deterministic locomotion policy. The caller supplies contact state and applies Velocity * dt to its motor.</summary>
public sealed class FirstPersonMotor
{
    private FirstPersonMotorState _state;
    public FirstPersonMotorState CaptureState() => _state;

    public void RestoreState(FirstPersonMotorState state)
    {
        if (!Finite(state.HorizontalVelocity) || !float.IsFinite(state.VerticalSpeed) ||
            !float.IsFinite(state.CoyoteRemaining) || state.CoyoteRemaining < 0 ||
            !float.IsFinite(state.JumpBufferRemaining) || state.JumpBufferRemaining < 0)
            throw new ArgumentException("FPS motion state must be finite.", nameof(state));
        _state = state;
    }

    public void Reset() => _state = default;
    public void StopHorizontalMovement() { _state.HorizontalVelocity = default; ClearJumpRequest(); }
    public void ClearJumpRequest() { _state.JumpPending = false; _state.JumpBufferRemaining = 0; }
    public void QueueJump(float bufferSeconds)
    {
        if (!float.IsFinite(bufferSeconds) || bufferSeconds < 0)
            throw new ArgumentOutOfRangeException(nameof(bufferSeconds));
        _state.JumpPending = true;
        _state.JumpBufferRemaining = bufferSeconds;
    }

    public FirstPersonMotion Step(float dt, Vector3 desiredDirection, bool sprintRequested,
        bool contactGrounded, Vector3 observedVelocity, FirstPersonMovementSettings settings)
    {
        settings.Validate();
        if (!float.IsFinite(dt) || dt < 0 || !Finite(desiredDirection) || !Finite(observedVelocity))
            throw new ArgumentOutOfRangeException(nameof(dt), "FPS simulation inputs must be finite and time must not be negative.");
        if (dt == 0)
            return new(_state.HorizontalVelocity + Vector3.Up * _state.VerticalSpeed, _state.WasGrounded, false, false, false, 0);

        bool grounded = contactGrounded && !_state.JumpInProgress;
        if (!contactGrounded)
            _state.JumpInProgress = false;
        bool landed = grounded && _state.Initialized && !_state.WasGrounded;
        float impact = landed ? MathF.Max(0, -_state.VerticalSpeed) : 0;
        _state.CoyoteRemaining = grounded ? settings.CoyoteTime : MathF.Max(0, _state.CoyoteRemaining - dt);
        if (_state.VerticalSpeed > 0 && observedVelocity.Y <= 0 && !contactGrounded)
            _state.VerticalSpeed = 0;
        if (grounded && _state.VerticalSpeed < 0)
            _state.VerticalSpeed = -2;
        bool jumped = _state.JumpPending && (grounded || _state.CoyoteRemaining > 0) && settings.JumpHeight > 0;
        if (jumped)
        {
            _state.VerticalSpeed = MathF.Sqrt(2 * settings.Gravity * settings.JumpHeight);
            _state.CoyoteRemaining = 0;
            _state.JumpInProgress = true;
            grounded = false;
            ClearJumpRequest();
        }
        else
        {
            _state.JumpBufferRemaining = MathF.Max(0, _state.JumpBufferRemaining - dt);
            if (_state.JumpBufferRemaining == 0)
                _state.JumpPending = false;
        }
        _state.VerticalSpeed = MathF.Max(-settings.TerminalSpeed, _state.VerticalSpeed - settings.Gravity * dt);
        desiredDirection = new(desiredDirection.X, 0, desiredDirection.Z);
        if (desiredDirection.LengthSquared > 1)
            desiredDirection = desiredDirection.Normalized;
        bool sprinting = sprintRequested && desiredDirection.LengthSquared > 0.0001f && grounded;
        Vector3 target = desiredDirection * settings.WalkSpeed * (sprinting ? settings.SprintMultiplier : 1);
        float rate = desiredDirection.LengthSquared > 0 ? settings.Acceleration : settings.Deceleration;
        float maximumChange = rate * dt * (grounded ? 1 : settings.AirControl);
        Vector3 difference = target - _state.HorizontalVelocity;
        _state.HorizontalVelocity += difference.Length <= maximumChange ? difference : difference.Normalized * maximumChange;
        _state.WasGrounded = grounded;
        _state.Initialized = true;
        return new(_state.HorizontalVelocity + Vector3.Up * _state.VerticalSpeed, grounded, sprinting, jumped, landed, impact);
    }

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
}
