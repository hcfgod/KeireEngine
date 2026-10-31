using Keire;
using Keire.FirstPerson;

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new Exception(message);
    }
}

static void Near(Vector3 actual, Vector3 expected, string message)
{
    Require((actual - expected).Length < 0.0002f, $"{message}: {actual} != {expected}");
}

var settings = FirstPersonMovementSettings.Default;
foreach (var up in new[] { Vector3.Up, Vector3.Right, -Vector3.Up, new Vector3(1, 1, 1).Normalized })
{
    Vector3 right = Vector3.Cross(up, Vector3.Forward).Normalized;
    Vector3 forward = Vector3.Cross(right, up);
    Vector3 Rotate(Vector3 value) => right * value.X + up * value.Y + forward * value.Z;
    var legacy = new FirstPersonMotor();
    var oriented = new FirstPersonMotor();
    Vector3 observed = Vector3.Zero;
    for (int tick = 0; tick < 80; ++tick)
    {
        if (tick == 4 || tick == 49)
        {
            legacy.QueueJump(0.12f);
            oriented.QueueJump(0.12f);
        }
        bool grounded = tick < 5 || tick >= 50;
        if (tick == 10)
        {
            observed = new(observed.X, 0, observed.Z);
        }
        Vector3 direction = tick < 30 ? new Vector3(0.4f, 0.3f, 1) : Vector3.Zero;
        var baseline = legacy.Step(0.016f, direction, true, grounded, observed, settings);
        var rotated = oriented.Step(0.016f, Rotate(direction), true, grounded, Rotate(observed), settings, up, settings.Gravity);
        Near(rotated.Velocity, Rotate(baseline.Velocity), $"rotational equivariance tick {tick}");
        Require(rotated.Grounded == baseline.Grounded && rotated.Jumped == baseline.Jumped &&
            rotated.Landed == baseline.Landed && rotated.Sprinting == baseline.Sprinting, "contact events differ");
        Require(MathF.Abs(rotated.LandingSpeed - baseline.LandingSpeed) < 0.0002f, "landing impact differs");
        observed = baseline.Velocity;
    }
}

var motor = new FirstPersonMotor();
var drifting = settings with { AirControl = 0 };
var legacyState = new FirstPersonMotorState { HorizontalVelocity = new(3, 0, 4), VerticalSpeed = 2, Initialized = true };
motor.RestoreState(legacyState);
var initialVelocity = new Vector3(3, 2, 4);
var changed = motor.Step(0.1f, Vector3.Zero, false, false, initialVelocity, drifting, Vector3.Right, 0);
Near(changed.Velocity, initialVelocity, "changing up must preserve inertial drift in zero gravity");
changed = motor.Step(0.1f, Vector3.Zero, false, false, initialVelocity, drifting, -Vector3.Up, 0);
Near(changed.Velocity, initialVelocity, "opposite up must preserve inertial drift");
// A frame change must not turn inertial zero-G drift into terminal-speed braking.
var fastDrift = new FirstPersonMotor();
fastDrift.RestoreState(new FirstPersonMotorState
{
    HorizontalVelocity = new Vector3(100, 0, 0),
    PreviousUp = Vector3.Up,
    Initialized = true
});
var fastVelocity = new Vector3(100, 0, 0);
for (int step = 0; step < 2; ++step)
{
    Near(fastDrift.Step(0.1f, Vector3.Zero, false, false, fastVelocity, drifting, -Vector3.Right, 0).Velocity,
        fastVelocity, "zero-G terminal cap changed inertial drift");
}
var fastCheckpoint = fastDrift.CaptureState();
Require(fastCheckpoint.VerticalSpeed == -100, "zero-G state lost speed beyond terminal cap");
var fastRestored = new FirstPersonMotor();
fastRestored.RestoreState(fastCheckpoint);
Near(fastRestored.Step(0.1f, Vector3.Zero, false, false, fastVelocity, drifting, -Vector3.Right, 0).Velocity,
    fastVelocity, "restored zero-G drift was capped");
Near(fastRestored.Step(0.1f, Vector3.Zero, false, false, fastVelocity, drifting, Vector3.Up, 0).Velocity,
    fastVelocity, "restored zero-G frame change lost momentum");
var checkpoint = motor.CaptureState();
var restored = new FirstPersonMotor();
restored.RestoreState(checkpoint);
Near(restored.Step(0, Vector3.Zero, false, false, Vector3.Zero, settings, Vector3.Up, 0).Velocity,
    initialVelocity, "zero dt reconstructs persisted frame without mutation");
Require(restored.CaptureState().Equals(checkpoint), "zero dt changed state");

foreach (var invalidUp in new[] { Vector3.Zero, new Vector3(0, 2, 0), new Vector3(float.NaN, 0, 0) })
{
    try
    {
        restored.Step(0.1f, Vector3.Zero, false, false, Vector3.Zero, settings, invalidUp, 1);
        throw new Exception("invalid up accepted");
    }
    catch (ArgumentOutOfRangeException) { }
    Require(restored.CaptureState().Equals(checkpoint), "invalid up mutated state");
}
foreach (float invalidGravity in new[] { -1f, float.NaN, float.PositiveInfinity })
{
    try
    {
        restored.Step(0.1f, Vector3.Zero, false, false, Vector3.Zero, settings, Vector3.Up, invalidGravity);
        throw new Exception("invalid gravity accepted");
    }
    catch (ArgumentOutOfRangeException) { }
    Require(restored.CaptureState().Equals(checkpoint), "invalid gravity mutated state");
}

var invalidState = checkpoint;
invalidState.PreviousUp = new Vector3(0, 2, 0);
try
{
    restored.RestoreState(invalidState);
    throw new Exception("invalid saved up accepted");
}
catch (ArgumentException) { }
Require(restored.CaptureState().Equals(checkpoint), "invalid restore mutated state");
foreach (float invalidTime in new[] { -1f, float.NaN, float.PositiveInfinity })
{
    try
    {
        restored.Step(invalidTime, Vector3.Zero, false, false, Vector3.Zero, settings, Vector3.Up, 1);
        throw new Exception("invalid timestep accepted");
    }
    catch (ArgumentOutOfRangeException) { }
    Require(restored.CaptureState().Equals(checkpoint), "invalid timestep mutated state");
}
motor.Reset();
motor.QueueJump(0.1f);
Require(!motor.Step(0.016f, Vector3.Zero, false, true, Vector3.Zero, settings, Vector3.Right, 0).Jumped,
    "zero gravity created a jump");
var coyote = new FirstPersonMotor();
coyote.Step(0.016f, Vector3.Zero, false, true, Vector3.Zero, settings, Vector3.Right, settings.Gravity);
coyote.QueueJump(0.1f);
Require(coyote.Step(0.016f, Vector3.Zero, false, false, Vector3.Zero, settings, Vector3.Right, settings.Gravity).Jumped,
    "rotated coyote jump failed");
var terminal = new FirstPersonMotor();
var fall = terminal.Step(100, Vector3.Zero, false, false, Vector3.Zero, settings, -Vector3.Up, settings.Gravity);
Near(fall.Velocity, Vector3.Up * settings.TerminalSpeed, "rotated terminal speed");
Console.WriteLine("PASS: actual managed assembly motor rotation, contacts, jump buffer, ceiling, coyote, terminal speed, frame persistence, zero gravity and input rejection.");
