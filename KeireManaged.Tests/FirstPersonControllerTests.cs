using Keire;
using Keire.FirstPerson;

internal static class FirstPersonControllerTests
{
    internal static void Run()
    {
        var settings = FirstPersonMovementSettings.Default;
        var motor = new FirstPersonMotor();
        const float dt = 1f / 60;
        FirstPersonMotion motion = default;
        for (int i = 0; i < 120; ++i)
            motion = motor.Step(dt, new(1, 0, 1), false, true, default, settings);
        Near(new Vector2(motion.Velocity.X, motion.Velocity.Z).Length, settings.WalkSpeed, "Diagonal speed");
        Check(!motion.Landed, "Standing must not repeat landing events.");
        for (int i = 0; i < 120; ++i)
            motion = motor.Step(dt, default, false, true, default, settings);
        Near(motion.Velocity.X, 0, "Deceleration");
        motor.QueueJump(settings.JumpBufferTime);
        motion = motor.Step(dt, Vector3.Forward, true, true, default, settings);
        Check(motion.Jumped && !motion.Grounded && motion.Velocity.Y > 0, "Jump must leave the grounded state.");
        motion = motor.Step(dt, Vector3.Forward, false, true, motion.Velocity, settings);
        Check(!motion.Grounded && !motion.Landed, "Stale takeoff contact must not cause a landing.");
        motion = motor.Step(dt, Vector3.Forward, false, false, motion.Velocity, settings);
        motor.QueueJump(0);
        motion = motor.Step(dt, Vector3.Forward, false, false, motion.Velocity, settings);
        Check(!motion.Jumped, "A second jump in the air must be rejected.");
        for (int i = 0; i < 40; ++i)
            motion = motor.Step(dt, Vector3.Forward, false, false, motion.Velocity, settings);
        motion = motor.Step(dt, Vector3.Forward, false, true, default, settings);
        Check(motion.Landed && motion.LandingSpeed > 0 && motion.Velocity.Z != 0,
            "Landing must report impact without losing horizontal movement.");
        motion = motor.Step(dt, Vector3.Forward, false, true, default, settings);
        Check(!motion.Landed, "Landing is a transition, not a continuous event.");

        motor.Reset();
        motor.Step(dt, default, false, true, default, settings);
        motor.Step(dt, default, false, false, default, settings);
        motor.QueueJump(settings.JumpBufferTime);
        Check(motor.Step(dt, default, false, false, default, settings).Jumped, "Coyote jump must work.");
        motor.Reset();
        motor.Step(dt, default, false, false, default, settings);
        motor.QueueJump(settings.JumpBufferTime);
        Check(!motor.Step(dt, default, false, false, default, settings).Jumped, "Buffered input waits for contact.");
        motion = motor.Step(dt, default, false, true, default, settings);
        Check(motion.Jumped && motion.Landed, "Buffered landing must report impact and consume the jump once.");
        motor.Reset();
        motor.QueueJump(dt);
        motor.Step(dt * 2, default, false, false, default, settings);
        Check(!motor.Step(dt, default, false, true, default, settings).Jumped, "Expired input must not jump.");

        motor.RestoreState(new() { VerticalSpeed = 4, Initialized = true });
        Check(motor.Step(dt, default, false, false, default, settings).Velocity.Y < 0,
            "A ceiling collision must cancel upward momentum.");
        for (int i = 0; i < 600; ++i)
            motion = motor.Step(dt, default, false, false, default, settings);
        Near(motion.Velocity.Y, -settings.TerminalSpeed, "Terminal velocity");
        var before = motor.CaptureState();
        motor.Step(0, default, false, true, default, settings);
        Check(before.Equals(motor.CaptureState()), "Zero time must preserve state.");
        Reject(() => motor.Step(-1, default, false, true, default, settings));
        Reject(() => motor.RestoreState(new() { VerticalSpeed = float.NaN }));
        Check(before.Equals(motor.CaptureState()), "Rejected input must preserve state.");
        Reject(() => (settings with { AirControl = 2 }).Validate());
        motor.StopHorizontalMovement();
        Check(motor.CaptureState().HorizontalVelocity == default && !motor.CaptureState().JumpPending,
            "Disabling controls must clear horizontal drift and queued input.");

        var look = FirstPersonLookSettings.Default;
        var mouse = FirstPersonLook.Step(0, new(10, -10), default, dt, look);
        Check(mouse.Pitch < 0, "Mouse up must look up.");
        Near(mouse.Pitch, FirstPersonLook.Step(0, new(10, -10), default, dt * 2, look).Pitch,
            "Mouse input must not depend on frame time");
        Check(FirstPersonLook.Step(0, new(0, -10), default, dt, look with { InvertY = true }).Pitch > 0,
            "Invert Y must reverse mouse pitch.");
        Check(FirstPersonLook.Step(0, default, new(0, 1), dt, look).Pitch < 0, "Stick up must look up.");
        Near(FirstPersonLook.Step(0, default, new(1, 0), dt * 2, look).YawDelta,
            2 * FirstPersonLook.Step(0, default, new(1, 0), dt, look).YawDelta, "Stick input integrates time");
        Near(FirstPersonLook.Step(0, new(0, 10000), default, dt, look).Pitch, 89, "Pitch clamp");
        Reject(() => (look with { MinimumPitch = 90 }).Validate());
        Reject(() => FirstPersonLook.Step(0, new(float.NaN, 0), default, dt, look));
        InputOwnership();
        var restored = new FirstPersonController
        {
            RuntimeSerializedState = """
                {"version":1,"fields":[{"Name":"_walkSpeed","StableId":"d7e0f0a1-bfe2-46d2-bc47-4083f77a8102","Type":"System.Single","Value":7.0}]}
                """
        };
        restored.RuntimeRestorePersistentState();
        Near(restored.MovementSettings.WalkSpeed, 7, "Existing scene speed survives upgrade");
        Near(restored.MovementSettings.Acceleration, settings.Acceleration, "Absent fields retain new defaults");
        restored.MovementSettings.Validate();
        restored.LookSettings.Validate();
        var stateField = typeof(FirstPersonController).GetField("_motionState",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
        var savedMotion = new FirstPersonMotorState
        {
            HorizontalVelocity = new(1, 0, 2), VerticalSpeed = -3, Initialized = true, CoyoteRemaining = 0.05f
        };
        stateField.SetValue(restored, savedMotion);
        restored.RuntimeCaptureReloadState();
        var reloaded = new FirstPersonController { RuntimeSerializedState = restored.RuntimeSerializedState };
        reloaded.RuntimeRestoreReloadState();
        Check(savedMotion.Equals(stateField.GetValue(reloaded)), "Hot reload must preserve modular motion state.");
    }

    private static unsafe void InputOwnership()
    {
        NativeInputFixture.Install();
        NativeRuntime.InstallManagedAssetsForTests(8101, 4, 2, null);
        try
        {
            var asset = Asset.FromId<InputActionAsset>(new AssetId(31, 37))!;
            var source = new FirstPersonActionInput(asset);
            source.Dispose();
            source.Dispose();
            Check(NativeInputFixture.ReleaseContextCalls == 1 && source.Read() == default,
                "Disposed FPS input must be inert and release its owned context exactly once.");
            NativeInputFixture.FailActionLookup = true;
            try
            {
                _ = new FirstPersonActionInput(asset);
                throw new Exception("Missing FPS actions were accepted.");
            }
            catch (InvalidOperationException error)
            {
                Check(error.Message.Contains("Player/Move"), "Missing action diagnostics must name the repair target.");
            }
            Check(NativeInputFixture.ReleaseContextCalls == 2,
                "Failed FPS input construction must roll back its native context.");
        }
        finally
        {
            NativeInputFixture.Uninstall();
            _ = NativeRuntime.ResetManagedAssets(8101);
        }
    }

    private static void Near(float actual, float expected, string message) =>
        Check(MathF.Abs(actual - expected) < 0.001f, $"{message}: expected {expected}, got {actual}.");
    private static void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (ArgumentException) { return; }
        throw new InvalidOperationException("Invalid FPS input was accepted.");
    }
}
