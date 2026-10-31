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
    // Zero represents the legacy +Y decomposition in saved states.
    public Vector3 PreviousUp;
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
            (state.PreviousUp != Vector3.Zero && !Unit(state.PreviousUp)) ||
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
        return Step(dt, desiredDirection, sprintRequested, contactGrounded, observedVelocity, settings,
            Vector3.Up, settings.Gravity);
    }

    /// <summary>
    /// Steps in an explicit unit-up frame using nonnegative effective gravity. The caller owns gravity
    /// sampling, body orientation and contact state. Grounded tangent-frame transport is caller policy;
    /// changing up preserves inertial velocity, not support-relative heading. Zero gravity does not create a jump impulse.
    /// </summary>
    public FirstPersonMotion Step(float dt, Vector3 desiredDirection, bool sprintRequested,
        bool contactGrounded, Vector3 observedVelocity, FirstPersonMovementSettings settings,
        Vector3 up, float effectiveGravity)
    {
        settings.Validate();
        if (!float.IsFinite(dt) || dt < 0 || !Finite(desiredDirection) || !Finite(observedVelocity) ||
            !float.IsFinite(desiredDirection.LengthSquared) || !float.IsFinite(observedVelocity.LengthSquared))
        {
            throw new ArgumentOutOfRangeException(nameof(dt), "FPS inputs must be finite and representable; time must not be negative.");
        }
        if (!Unit(up))
        {
            throw new ArgumentOutOfRangeException(nameof(up), "FPS up must be a finite unit vector.");
        }
        if (!float.IsFinite(effectiveGravity) || effectiveGravity < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(effectiveGravity));
        }

        // Work transactionally: overflow or invalid input must not partially consume jump/state data.
        FirstPersonMotorState state = _state;
        Vector3 previousUp = state.PreviousUp == Vector3.Zero ? Vector3.Up : state.PreviousUp;
        Vector3 velocity = state.HorizontalVelocity + previousUp * state.VerticalSpeed;
        if (dt == 0)
        {
            return new(velocity, state.WasGrounded, false, false, false, 0);
        }
        if (previousUp != up)
        {
            state.VerticalSpeed = Vector3.Dot(velocity, up);
            state.HorizontalVelocity = velocity - up * state.VerticalSpeed;
        }
        state.PreviousUp = up;

        bool grounded = contactGrounded && !state.JumpInProgress;
        if (!contactGrounded)
        {
            state.JumpInProgress = false;
        }
        bool landed = grounded && state.Initialized && !state.WasGrounded;
        float impact = landed ? MathF.Max(0, -state.VerticalSpeed) : 0;
        state.CoyoteRemaining = grounded ? settings.CoyoteTime : MathF.Max(0, state.CoyoteRemaining - dt);
        if (state.VerticalSpeed > 0 && Vector3.Dot(observedVelocity, up) <= 0 && !contactGrounded)
        {
            state.VerticalSpeed = 0;
        }
        if (grounded && state.VerticalSpeed < 0 && effectiveGravity > 0)
        {
            state.VerticalSpeed = -2;
        }
        bool jumped = state.JumpPending && (grounded || state.CoyoteRemaining > 0) &&
            settings.JumpHeight > 0 && effectiveGravity > 0;
        if (jumped)
        {
            state.VerticalSpeed = (float)Math.Sqrt(2.0 * effectiveGravity * settings.JumpHeight);
            state.CoyoteRemaining = 0;
            state.JumpInProgress = true;
            grounded = false;
            state.JumpPending = false;
            state.JumpBufferRemaining = 0;
        }
        else
        {
            state.JumpBufferRemaining = MathF.Max(0, state.JumpBufferRemaining - dt);
            if (state.JumpBufferRemaining == 0)
            {
                state.JumpPending = false;
            }
        }
        if (effectiveGravity > 0)
        {
            state.VerticalSpeed = MathF.Max(-settings.TerminalSpeed, state.VerticalSpeed - effectiveGravity * dt);
        }
        desiredDirection -= up * Vector3.Dot(desiredDirection, up);
        if (desiredDirection.LengthSquared > 1)
        {
            desiredDirection = desiredDirection.Normalized;
        }
        bool sprinting = sprintRequested && desiredDirection.LengthSquared > 0.0001f && grounded;
        Vector3 target = desiredDirection * settings.WalkSpeed * (sprinting ? settings.SprintMultiplier : 1);
        float rate = desiredDirection.LengthSquared > 0 ? settings.Acceleration : settings.Deceleration;
        float maximumChange = rate * dt * (grounded ? 1 : settings.AirControl);
        Vector3 difference = target - state.HorizontalVelocity;
        state.HorizontalVelocity += difference.Length <= maximumChange ? difference : difference.Normalized * maximumChange;
        velocity = state.HorizontalVelocity + up * state.VerticalSpeed;
        if (!float.IsFinite(maximumChange) || !float.IsFinite(difference.LengthSquared) ||
            !Finite(velocity) || !float.IsFinite(velocity.LengthSquared))
        {
            throw new ArgumentOutOfRangeException(nameof(settings), "FPS integration exceeds the supported numeric range.");
        }
        state.WasGrounded = grounded;
        state.Initialized = true;
        _state = state;
        return new(velocity, grounded, sprinting, jumped, landed, impact);
    }

    private static bool Unit(Vector3 value) => Finite(value) && MathF.Abs(value.LengthSquared - 1) <= 0.00001f;

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
}
