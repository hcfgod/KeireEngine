using Keire;
using System.Runtime.InteropServices;

internal static unsafe class RigidBodyMotionSettingsTests
{
    private static RigidBodyMotionSettings _settings;
    private static int _setCalls;
    private static bool _reject;

    internal static void Run()
    {
        var previousGet = NativeRuntime.GetRigidBodyMotionSettingsIcall;
        var previousSet = NativeRuntime.SetRigidBodyMotionSettingsIcall;
        NativeRuntime.GetRigidBodyMotionSettingsIcall = &Get;
        NativeRuntime.SetRigidBodyMotionSettingsIcall = &Set;
        try
        {
            _reject = false;
            _setCalls = 0;
            var body = new RigidBody(new Entity(12, new EntityId(34, 56)));
            var desired = new RigidBodyMotionSettings(0, 0, 20000, 100);
            body.MotionSettings = desired;
            if (body.MotionSettings != desired || _setCalls != 1)
            {
                throw new InvalidOperationException("Motion settings did not round trip through the bridge.");
            }
            foreach (var invalid in new[]
            {
                desired with { LinearDamping = -1 },
                desired with { AngularDamping = float.NaN },
                desired with { MaximumLinearSpeed = float.PositiveInfinity },
                desired with { MaximumAngularSpeed = 0 }
            })
            {
                Reject<ArgumentOutOfRangeException>(() => body.MotionSettings = invalid);
            }
            if (_setCalls != 1 || body.MotionSettings != desired)
            {
                throw new InvalidOperationException("Invalid settings reached the native bridge.");
            }
            RigidBodyMotionSettings.Default.Validate();
            _reject = true;
            Reject<InvalidOperationException>(() => body.MotionSettings = desired);
            Reject<InvalidOperationException>(() => { _ = body.MotionSettings; });
        }
        finally
        {
            NativeRuntime.GetRigidBodyMotionSettingsIcall = previousGet;
            NativeRuntime.SetRigidBodyMotionSettingsIcall = previousSet;
        }
    }

    [UnmanagedCallersOnly]
    private static byte Get(ulong world, ulong high, ulong low, float* linearDamping, float* angularDamping,
                            float* linearSpeed, float* angularSpeed)
    {
        if (_reject || world != 12 || high != 34 || low != 56)
        {
            return 0;
        }
        *linearDamping = _settings.LinearDamping;
        *angularDamping = _settings.AngularDamping;
        *linearSpeed = _settings.MaximumLinearSpeed;
        *angularSpeed = _settings.MaximumAngularSpeed;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static byte Set(ulong world, ulong high, ulong low, float linearDamping, float angularDamping,
                            float linearSpeed, float angularSpeed)
    {
        if (_reject || world != 12 || high != 34 || low != 56)
        {
            return 0;
        }
        _settings = new(linearDamping, angularDamping, linearSpeed, angularSpeed);
        ++_setCalls;
        return 1;
    }

    private static void Reject<T>(Action action) where T : Exception
    {
        try
        {
            action();
        }
        catch (T)
        {
            return;
        }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
}
